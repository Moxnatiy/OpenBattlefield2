#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/mesh/collision.h"
#include "obf2/server/collision_world.h"
#include "obf2/server/game_client.h"
#include "obf2/server/game_server.h"

using namespace obf2;

namespace {

// Two points: one already ours, the other neutral.
level::GameplayObjects makeGameplay() {
  level::GameplayObjects gameplay;

  level::ControlPoint ours;
  ours.id = 401;
  ours.nameKey = "CPNAME_base";
  ours.position = Vec3f{100.0f, 0.0f, 0.0f};
  ours.radius = 10.0f;
  ours.team = 1;
  gameplay.controlPoints.push_back(ours);

  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.nameKey = "CPNAME_middle";
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.team = 0;
  gameplay.controlPoints.push_back(neutral);

  return gameplay;
}

// A wall across the way: two triangles in the plane X = wallX.
mesh::CollisionLayer makeWall(float wallX) {
  mesh::CollisionLayer layer;
  layer.type = mesh::ColType::Soldier;
  layer.vertices = {
      mesh::Vec3{wallX, -5.0f, -10.0f}, mesh::Vec3{wallX, 10.0f, -10.0f},
      mesh::Vec3{wallX, 10.0f, 10.0f},  mesh::Vec3{wallX, -5.0f, 10.0f},
  };
  layer.faces = {mesh::CollisionFace{0, 1, 2, 0}, mesh::CollisionFace{0, 2, 3, 0}};
  layer.bounds.min = mesh::Vec3{wallX - 0.1f, -5.0f, -10.0f};
  layer.bounds.max = mesh::Vec3{wallX + 0.1f, 10.0f, 10.0f};
  return layer;
}

void pump(server::GameServer& gameServer, server::GameClient& client, int steps = 8) {
  for (int i = 0; i < steps; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
}

}  // namespace

static void testSpawnUsesOwnedControlPoint() {
  // The player has to spawn at their own team's point rather than at the default
  // one — the same as in the original.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{-999.0f, 0.0f, -999.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(makeGameplay());

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  // Team 1's point stands at x = 100.
  CHECK(std::abs(gameServer.objects().front().position.x - 100.0f) < 1.0f);
}

static void testSpawnUsesRealSpawnPoint() {
  // There is a real spawn point bound to our control point — the soldier has to
  // stand on exactly it rather than in the flag's centre.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{-999.0f, 0.0f, -999.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay = makeGameplay();
  level::SpawnPoint spawn;
  spawn.templateName = "base_1";
  spawn.position = Vec3f{123.0f, 10.0f, 45.0f};
  spawn.offset = Vec3f{0.0f, 1.25f, 0.0f};
  spawn.controlPointId = 401;  // team 1's point
  gameplay.spawnPoints.push_back(spawn);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  const Vec3f position = gameServer.objects().front().position;
  CHECK(std::abs(position.x - 123.0f) < 0.01f);
  CHECK(std::abs(position.z - 45.0f) < 0.01f);
  // The spawn offset (setSpawnPositionOffset) is added to the point's position:
  // without it the soldier would start from 10.0 and fall lower over these ticks.
  CHECK(position.y > 10.5f);
}

static void testControlPointsAreLoaded() {
  server::GameServer gameServer(server::ServerSettings{});
  gameServer.setGameplay(makeGameplay());

  CHECK_EQ(gameServer.controlPoints().size(), std::size_t(2));
  if (gameServer.controlPoints().size() < 2) return;
  CHECK_EQ(gameServer.controlPoints()[0].id, 401);
  CHECK_EQ(gameServer.controlPoints()[0].team, 1);
  CHECK_EQ(gameServer.controlPoints()[1].team, 0);
  CHECK_EQ(gameServer.controlPoints()[1].nameKey, std::string("CPNAME_middle"));
}

static void testStandingOnNeutralPointCapturesIt() {
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 0.0f, 0.0f};  // straight onto the neutral point

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.team = 0;
  neutral.timeToGetControl = 1.0f;
  neutral.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(neutral);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.controlPoints().front().team, 0);

  // We stand on the point for a second and a half.
  for (int i = 0; i < 45; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 1);
}

static void testPointIsNotCapturedFromAfar() {
  server::ServerSettings settings;
  // The player spawns far from the point.
  settings.spawnPosition = Vec3f{500.0f, 0.0f, 500.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.timeToGetControl = 1.0f;
  neutral.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(neutral);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();

  for (int i = 0; i < 90; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 0);
}

static void testHeightDoesNotBlockCapture() {
  // The capture radius is horizontal: a point is taken from a building's roof too.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 30.0f, 0.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint neutral;
  neutral.id = 402;
  neutral.position = Vec3f{0.0f, 0.0f, 0.0f};
  neutral.radius = 10.0f;
  neutral.timeToGetControl = 1.0f;
  neutral.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(neutral);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();

  for (int i = 0; i < 60; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 1);
}

static void testEnemyFlagIsNeutralizedBeforeCapture() {
  // An enemy point cannot be taken at once: first the flag goes down (the point
  // becomes neutral) and only then ours goes up.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint enemy;
  enemy.id = 402;
  enemy.position = Vec3f{0.0f, 0.0f, 0.0f};
  enemy.radius = 10.0f;
  enemy.team = 2;  // the enemy's point
  enemy.timeToGetControl = 1.0f;
  enemy.timeToLoseControl = 1.0f;
  gameplay.controlPoints.push_back(enemy);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  // After ~1.2 s the flag has to fall: the point is neutral but not ours yet.
  for (int i = 0; i < 36; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 0);

  // Another second — and it is ours.
  for (int i = 0; i < 45; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK_EQ(gameServer.controlPoints().front().team, 1);
}

static void testTicketsStartFromDefaults() {
  server::ServerSettings settings;
  settings.defaultTickets[1] = 250;
  settings.defaultTickets[2] = 250;
  settings.ticketRatio = 50.0f;  // sv.ticketRatio in per cent

  server::GameServer gameServer(settings);
  gameServer.setGameplay(makeGameplay());

  CHECK_EQ(gameServer.tickets(1), 125);
  CHECK_EQ(gameServer.tickets(2), 125);
  CHECK(gameServer.status() == server::GameStatus::Playing);
}

static void testTicketsBleedForTeamWithoutArea() {
  // One point of weight 140 for team 1: the enemy bleeds, we do not.
  server::ServerSettings settings;
  settings.defaultTickets[1] = 100;
  settings.defaultTickets[2] = 100;
  settings.ticketLossPerMin[2] = 60.0f;  // exactly 1 ticket per second at full advantage

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint ours;
  ours.id = 401;
  ours.position = Vec3f{0.0f, 0.0f, 0.0f};
  ours.radius = 10.0f;
  ours.team = 1;
  ours.areaValueTeam1 = 140.0f;
  ours.unableToChangeTeam = true;  // so nobody takes it over during the test
  gameplay.controlPoints.push_back(ours);

  // The enemy has to keep a base: a team with no points at all bleeds at a
  // completely different, "final" rate, and another test checks that.
  level::ControlPoint theirs;
  theirs.id = 402;
  theirs.position = Vec3f{500.0f, 0.0f, 0.0f};
  theirs.radius = 10.0f;
  theirs.team = 2;
  theirs.unableToChangeTeam = true;
  gameplay.controlPoints.push_back(theirs);
  gameServer.setGameplay(std::move(gameplay));

  // Ten seconds with not a single player.
  for (int i = 0; i < 300; ++i) gameServer.tick(1.0f / 30.0f);

  CHECK_EQ(gameServer.tickets(1), 100);
  // (60/60) * (140/100) = 1.4 tickets per second -> about 14 over ten.
  const int lost = 100 - gameServer.tickets(2);
  CHECK(lost >= 13 && lost <= 15);
}

static void testRoundEndsWhenTicketsRunOut() {
  server::ServerSettings settings;
  settings.defaultTickets[1] = 100;
  settings.defaultTickets[2] = 2;
  settings.ticketLossPerMin[2] = 600.0f;

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;
  level::ControlPoint ours;
  ours.id = 401;
  ours.position = Vec3f{0.0f, 0.0f, 0.0f};
  ours.radius = 10.0f;
  ours.team = 1;
  ours.areaValueTeam1 = 140.0f;
  ours.unableToChangeTeam = true;
  gameplay.controlPoints.push_back(ours);
  level::ControlPoint theirs;
  theirs.id = 402;
  theirs.position = Vec3f{500.0f, 0.0f, 0.0f};
  theirs.radius = 10.0f;
  theirs.team = 2;
  theirs.unableToChangeTeam = true;
  gameplay.controlPoints.push_back(theirs);
  gameServer.setGameplay(std::move(gameplay));

  for (int i = 0; i < 300; ++i) gameServer.tick(1.0f / 30.0f);

  CHECK_EQ(gameServer.tickets(2), 0);
  CHECK_EQ(gameServer.winner(), 1);
  CHECK(gameServer.status() == server::GameStatus::EndGame);
}

static void testDeathCostsTicketAndRespawns() {
  server::ServerSettings settings;
  settings.defaultTickets[1] = 100;
  settings.defaultTickets[2] = 100;
  settings.respawnDelay = 0.5f;
  settings.spawnPosition = Vec3f{0.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(makeGameplay());

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.tickets(1), 100);
  CHECK_EQ(gameServer.players().size(), std::size_t(1));
  if (gameServer.players().empty()) return;
  CHECK(gameServer.players().front().alive);

  gameServer.killPlayer(gameServer.players().front().id, "a test");
  CHECK_EQ(gameServer.tickets(1), 99);
  CHECK(!gameServer.players().front().alive);

  // After half a second the player has to spawn again at full health.
  for (int i = 0; i < 30; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK(gameServer.players().front().alive);
  CHECK(gameServer.players().front().health > 99.0f);
  CHECK_EQ(gameServer.tickets(1), 99);
}

static void testCollisionStillWorksAfterRespawn() {
  // A reported bug: after spawning the player walked through objects.
  server::ServerSettings settings;
  settings.respawnDelay = 0.2f;
  settings.spawnPosition = Vec3f{-3.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(level::GameplayObjects{});

  auto world = std::make_unique<server::CollisionWorld>();
  world->addLayer(makeWall(0.0f), Mat4::identity());
  gameServer.setCollision(std::move(world));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  const auto pushForward = [&]() {
    net::PlayerInput input;
    // Right of the zero angle is +X, that is straight into the wall.
    input.moveRight = 1.0f;
    for (int i = 0; i < 120; ++i) {
      client.setInput(input);
      gameServer.tick(1.0f / 30.0f);
      client.tick(1.0f / 30.0f);
    }
  };

  pushForward();
  CHECK(!gameServer.objects().empty());
  if (gameServer.objects().empty()) return;
  // Before spawning the wall holds.
  CHECK(gameServer.objects().front().position.x < 0.2f);

  // Now death, a spawn — and into the wall again.
  gameServer.killPlayer(gameServer.players().front().id, "a test");
  for (int i = 0; i < 30; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }
  CHECK(gameServer.players().front().alive);

  pushForward();
  CHECK(gameServer.objects().front().position.x < 0.2f);
}

static void testSoldierStandsOnObject() {
  // There was a bug: having stepped onto an object, the player stayed at the
  // terrain's height. The ground has to be computed from the collision geometry too.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{0.0f, 5.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(level::GameplayObjects{});

  // A flat "floor" two metres up under the spawn point.
  mesh::CollisionLayer floorLayer;
  floorLayer.type = mesh::ColType::Soldier;
  floorLayer.vertices = {
      mesh::Vec3{-5.0f, 2.0f, -5.0f}, mesh::Vec3{5.0f, 2.0f, -5.0f},
      mesh::Vec3{5.0f, 2.0f, 5.0f},   mesh::Vec3{-5.0f, 2.0f, 5.0f},
  };
  // The winding is such that the normal points up by the engine's convention.
  floorLayer.faces = {mesh::CollisionFace{0, 1, 2, 0}, mesh::CollisionFace{0, 2, 3, 0}};
  floorLayer.bounds.min = mesh::Vec3{-5.0f, 1.9f, -5.0f};
  floorLayer.bounds.max = mesh::Vec3{5.0f, 2.1f, 5.0f};

  auto world = std::make_unique<server::CollisionWorld>();
  world->addLayer(floorLayer, Mat4::identity());
  gameServer.setCollision(std::move(world));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();

  for (int i = 0; i < 90; ++i) {
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }

  CHECK(!gameServer.objects().empty());
  if (gameServer.objects().empty()) return;
  // He has to stand on the object (y = 2) rather than fall through to zero.
  CHECK(std::abs(gameServer.objects().front().position.y - 2.0f) < 0.2f);
}

static void testSoldierWalksUpStep() {
  // A step lower than the soldier's bottom sphere has to be walked over, not block him.
  server::ServerSettings settings;
  settings.spawnPosition = Vec3f{-2.0f, 0.0f, 0.0f};

  server::GameServer gameServer(settings);
  gameServer.setGameplay(level::GameplayObjects{});

  // The step's top plane is 0.3 m high and starts at x = 0.
  mesh::CollisionLayer step;
  step.type = mesh::ColType::Soldier;
  step.vertices = {
      mesh::Vec3{0.0f, 0.3f, -5.0f}, mesh::Vec3{5.0f, 0.3f, -5.0f},
      mesh::Vec3{5.0f, 0.3f, 5.0f},  mesh::Vec3{0.0f, 0.3f, 5.0f},
  };
  step.faces = {mesh::CollisionFace{0, 1, 2, 0}, mesh::CollisionFace{0, 2, 3, 0}};
  step.bounds.min = mesh::Vec3{0.0f, 0.2f, -5.0f};
  step.bounds.max = mesh::Vec3{5.0f, 0.4f, 5.0f};

  auto world = std::make_unique<server::CollisionWorld>();
  world->addLayer(step, Mat4::identity());
  gameServer.setCollision(std::move(world));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  net::PlayerInput input;
  input.moveRight = 1.0f;  // east, into the step
  // One tick of running is 3.9 m/s, so over a second the soldier steps onto the
  // platform and stays on it rather than running straight through.
  for (int i = 0; i < 30; ++i) {
    client.setInput(input);
    gameServer.tick(1.0f / 30.0f);
    client.tick(1.0f / 30.0f);
  }

  CHECK(!gameServer.objects().empty());
  if (gameServer.objects().empty()) return;
  const Vec3f position = gameServer.objects().front().position;
  CHECK(position.x > 0.5f);                     // he stepped up rather than stopping
  CHECK(std::abs(position.y - 0.3f) < 0.15f);   // and stands on the step
}

// A chosen spawn point narrows the choice to one group. A group in the engine is
// the set of points of one flag (`SpawnGroup::getControlPointId`), and a point
// within a group is taken at random (docs/functions/spawn.md).
static void testSpawnGroupNarrowsTheChoice() {
  server::ServerSettings settings;
  settings.spawnOnJoin = false;  // we check exactly the path through DONE

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay;

  // Two flags of one team, each with its own spawn point.
  for (int i = 0; i < 2; ++i) {
    level::ControlPoint point;
    point.id = 401 + i;
    point.nameKey = "CPNAME_" + std::to_string(i);
    point.position = Vec3f{static_cast<float>(i) * 500.0f, 0.0f, 0.0f};
    point.team = 1;
    gameplay.controlPoints.push_back(point);

    level::SpawnPoint spawn;
    spawn.templateName = "sp_" + std::to_string(i);
    spawn.position = Vec3f{static_cast<float>(i) * 500.0f, 10.0f, 7.0f};
    spawn.controlPointId = 401 + i;
    gameplay.spawnPoints.push_back(spawn);
  }
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);
  CHECK_EQ(gameServer.objects().size(), std::size_t(0));
  if (gameServer.players().empty()) return;

  // We ask for the second flag — and have to end up on exactly its point.
  CHECK(gameServer.requestSpawn(gameServer.players().front().id, 1, 0, 402));
  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  CHECK(std::abs(gameServer.objects().front().position.x - 500.0f) < 0.01f);
}

// A flag that does not exist must not leave the player without a spawn: we take any
// point of our own.
static void testUnknownSpawnGroupFallsBack() {
  server::ServerSettings settings;
  settings.spawnOnJoin = false;
  settings.spawnPosition = Vec3f{-999.0f, 0.0f, -999.0f};

  server::GameServer gameServer(settings);
  level::GameplayObjects gameplay = makeGameplay();
  level::SpawnPoint spawn;
  spawn.templateName = "base_1";
  spawn.position = Vec3f{123.0f, 10.0f, 45.0f};
  spawn.controlPointId = 401;
  gameplay.spawnPoints.push_back(spawn);
  gameServer.setGameplay(std::move(gameplay));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));
  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);
  if (gameServer.players().empty()) return;

  CHECK(gameServer.requestSpawn(gameServer.players().front().id, 1, 0, 999));
  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  if (gameServer.objects().empty()) return;
  CHECK(std::abs(gameServer.objects().front().position.x - 123.0f) < 0.01f);
}

TEST_MAIN({
  testSpawnGroupNarrowsTheChoice();
  testUnknownSpawnGroupFallsBack();
  testSoldierStandsOnObject();
  testSoldierWalksUpStep();
  testCollisionStillWorksAfterRespawn();
  testDeathCostsTicketAndRespawns();
  testEnemyFlagIsNeutralizedBeforeCapture();
  testTicketsStartFromDefaults();
  testTicketsBleedForTeamWithoutArea();
  testRoundEndsWhenTicketsRunOut();
  testSpawnUsesOwnedControlPoint();
  testSpawnUsesRealSpawnPoint();
  testControlPointsAreLoaded();
  testStandingOnNeutralPointCapturesIt();
  testPointIsNotCapturedFromAfar();
  testHeightDoesNotBlockCapture();
})
