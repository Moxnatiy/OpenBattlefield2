#include "obf2/app/hosted_game.h"

#include <cstdio>

#include "obf2/con/interpreter.h"
#include "obf2/engine/console.h"
#include "obf2/level/gameplay.h"
#include "obf2/net/connection.h"
#include "obf2/server/physics.h"

namespace obf2::app {

server::ServerSettings hostedSettings(FileSystem& files, const level::Level& level) {
  server::ServerSettings settings;
  settings.levelName = level.name;
  // The spawn is above the map's centre, slightly above sea level, so as not to
  // end up inside a hill.
  settings.spawnPosition = Vec3f{-40.0f, level.terrain.seaLevel + 40.0f, -200.0f};

  // The movement constants come from the game's data rather than from our heads:
  // the same file the original reads.
  settings.physics = server::loadPhysicsConstants(files);
  std::printf("  physics: acceleration %.2f, deceleration %.2f, air control %.2f\n",
              settings.physics.acceleration, settings.physics.deceleration,
              settings.physics.airMovementFactor);

  // The tickets and the round's settings come from the same files the game reads:
  // GameLogicInit.con (the starting tickets) and Settings/ServerSettings.con.
  engine::Console console;
  console.bind("gameLogic.setDefaultNumberOfTickets", [&](const con::Command& command) {
    const auto team = command.argInt(0);
    const auto count = command.argInt(1);
    if (team && count && *team >= 1 && *team <= 2) settings.defaultTickets[*team] = *count;
  });
  console.bind("sv.ticketRatio", [&](const con::Command& command) {
    settings.ticketRatio = command.argFloat(0).value_or(settings.ticketRatio);
  });
  console.bind("sv.spawnTime", [&](const con::Command& command) {
    settings.respawnDelay = command.argFloat(0).value_or(settings.respawnDelay);
  });
  console.bind("sv.numPlayersNeededToStart", [&](const con::Command& command) {
    settings.playersNeededToStart = command.argInt(0).value_or(settings.playersNeededToStart);
  });
  // We have a spawn screen, so the workaround for headless runs is not needed
  // here: the soldier will spawn only after DONE, as in the engine.
  settings.spawnOnJoin = false;
  con::Interpreter interpreter(files, [&](const con::Command& c) { console.execute(c); });
  interpreter.runFile("GameLogicInit.con");
  interpreter.runFile("Settings/ServerSettings.con");
  std::printf("  tickets: %d against %d (ticketRatio %.0f%%), spawn in %.0f s\n",
              settings.defaultTickets[1], settings.defaultTickets[2], settings.ticketRatio,
              settings.respawnDelay);
  return settings;
}

HostedGame startHostedGame(FileSystem& files, const level::Level& level,
                           server::CollisionLibrary& collision) {
  HostedGame hosted;
  hosted.server = std::make_unique<server::GameServer>(hostedSettings(files, level));
  server::GameServer& gameServer = *hosted.server;

  // The level's statics first, then the game logic — exactly the order the engine
  // does it in. The other way round is not allowed: `loadWorld` starts with a
  // clean object list and would sweep away the flags `setGameplay` places.
  gameServer.loadWorld(level);

  // The mode's game logic: control points and vehicle spawners.
  std::string gameplayError;
  if (auto gameplay = level::loadGameplayObjects(files, level.name, "gpm_cq", 16, &gameplayError)) {
    std::printf("  game logic: %zu control points, %zu vehicle spawners, %zu spawn points\n",
                gameplay->controlPoints.size(), gameplay->spawners.size(),
                gameplay->spawnPoints.size());
    for (const auto& point : gameplay->controlPoints) {
      std::printf("    point %d \"%s\" radius %.0f @ %.0f/%.0f/%.0f\n", point.id,
                  point.nameKey.c_str(), point.radius, point.position.x, point.position.y,
                  point.position.z);
    }
    gameServer.setGameplay(std::move(*gameplay));
  } else {
    std::printf("  game logic: %s\n", gameplayError.c_str());
  }

  // The terrain, for collision with the ground: without it a soldier falls forever.
  gameServer.setTerrain(&level);

  // Vehicles have to stop a soldier too: they stand in the server's world rather
  // than in the level's statics, so they are added separately.
  std::vector<level::StaticObject> collisionObjects = level.objects;
  for (const auto& object : gameServer.objects()) {
    if (object.spawnerIndex < 0) continue;
    level::StaticObject vehicle;
    vehicle.templateName = object.templateName;
    vehicle.position = object.position;
    vehicle.rotation = object.rotation;
    vehicle.hasRotation = true;
    collisionObjects.push_back(std::move(vehicle));
  }
  gameServer.setCollision(server::buildCollisionWorld(collision, collisionObjects));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  hosted.client =
      std::make_unique<server::GameClient>(std::move(clientSide), std::string("player"));
  server::GameClient& client = *hosted.client;
  client.connect();

  // The world is large and is cut into packets, so we spin while new ones arrive.
  std::size_t previous = 0;
  for (int step = 0; step < 4096; ++step) {
    gameServer.tick(1.0f / 60.0f);
    client.tick(1.0f / 60.0f);
    if (client.objects().size() == previous && step > 8) break;
    previous = client.objects().size();
  }

  std::printf("  local server: %s, players %zu, packets %lld/%lld\n",
              std::string(server::clientStateName(client.state())).c_str(),
              gameServer.playerCount(), gameServer.packetsSent(), gameServer.packetsReceived());
  int flags = 0;
  for (const auto& [id, object] : client.objects()) {
    (void)id;
    if (object.templateName.rfind("CPNAME", 0) == 0) ++flags;
  }
  std::printf("  control points that reached the client: %d\n", flags);
  std::printf("  the client received objects: %zu of %zu\n", client.objects().size(),
              level.objects.size());

  // We do not put the player's soldier into the scene: we play as him, not watch him.
  for (const auto& player : gameServer.players()) hosted.localSoldierId = player.soldierId;

  hosted.placement.reserve(client.objects().size());
  for (const auto& [id, object] : client.objects()) {
    if (id == hosted.localSoldierId) continue;
    level::StaticObject staticObject;
    staticObject.templateName = object.templateName;
    staticObject.position = object.position;
    staticObject.rotation = object.rotation;
    staticObject.hasRotation = true;
    hosted.placement.push_back(std::move(staticObject));
  }

  // Vegetation does not travel over the network — the client draws it from the
  // level's data, as the original does. The matrix from the .con carries both
  // its rotation and its scale.
  int overgrowth = 0;
  for (const level::StaticObject& object : level.objects) {
    if (!object.isOvergrowth) continue;
    hosted.placement.push_back(object);
    ++overgrowth;
  }
  std::printf("  vegetation from the level's data: %d instances\n", overgrowth);
  return hosted;
}

}  // namespace obf2::app
