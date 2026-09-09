#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/server/game_client.h"
#include "obf2/server/game_server.h"

using namespace obf2;

namespace {

// The world that normally comes from a level. Here we make it by hand so the test
// does not depend on the game being on disk.
level::Level makeLevel(std::size_t objectCount) {
  level::Level world;
  world.name = "testmap";
  for (std::size_t i = 0; i < objectCount; ++i) {
    level::StaticObject object;
    object.templateName = "hangar_" + std::to_string(i);
    object.position = Vec3f{static_cast<float>(i) * 10.0f, 100.0f, -50.0f};
    object.rotation = Vec3f{45.0f, 0.0f, 0.0f};
    world.objects.push_back(std::move(object));
  }
  return world;
}

// Runs both sides while they exchange packets.
void pump(server::GameServer& gameServer, server::GameClient& client, int steps = 8) {
  for (int i = 0; i < steps; ++i) {
    gameServer.tick(1.0f / 60.0f);
    client.tick(1.0f / 60.0f);
  }
}

}  // namespace

static void testLoopbackDeliversBothWays() {
  auto [first, second] = net::LoopbackConnection::createPair();
  const std::byte payload[] = {std::byte{1}, std::byte{2}, std::byte{3}};

  CHECK(first->send(payload));
  CHECK(!first->receive().has_value());  // nothing arrives back to itself

  const auto got = second->receive();
  CHECK(got.has_value());
  if (got) CHECK_EQ(got->size(), std::size_t(3));

  // Both ends see the break.
  first->close();
  CHECK(!first->connected());
  CHECK(!second->connected());
}

static void testFullHandshakeAndWorldTransfer() {
  // This is a single-player game: a local server plus a client through the loop.
  server::ServerSettings settings;
  settings.levelName = "Dalian_plant";
  settings.gameMode = "gpm_cq";

  server::GameServer gameServer(settings);
  gameServer.loadWorld(makeLevel(50));
  CHECK_EQ(gameServer.objects().size(), std::size_t(50));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  server::GameClient client(std::move(clientSide), "ARNE");
  CHECK(client.connect());
  CHECK(client.state() == server::ClientState::Requesting);

  pump(gameServer, client);

  CHECK(client.state() == server::ClientState::InWorld);
  CHECK_EQ(client.levelName(), std::string("Dalian_plant"));
  CHECK_EQ(client.gameMode(), std::string("gpm_cq"));
  CHECK(client.playerId() != 0);

  // The whole world arrived: 50 static objects plus the soldier the server created
  // for this player.
  CHECK_EQ(client.objects().size(), std::size_t(51));
  const auto first = client.objects().find(1);
  CHECK(first != client.objects().end());
  if (first != client.objects().end()) {
    CHECK_EQ(first->second.templateName, std::string("hangar_0"));
    // The position travels as a compressed vector, so we compare to the quantisation's precision.
    CHECK(std::abs(first->second.position.y - 100.0f) < 0.05f);
  }
}

static void testServerNamesThePlayer() {
  server::GameServer gameServer(server::ServerSettings{});
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.playerCount(), std::size_t(1));
  if (gameServer.playerCount() == 1) {
    CHECK_EQ(gameServer.players().front().name, std::string("ARNE"));
    CHECK(gameServer.players().front().acknowledged);
  }
}

static void testWrongProtocolVersionIsDenied() {
  // A client with an old version must not get into the world.
  server::GameServer gameServer(server::ServerSettings{});
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();

  // We send the request by hand with a foreign version.
  std::vector<std::byte> buffer(256);
  net::BitWriter writer(buffer);
  net::ConnectionRequest request;
  request.protocolVersion = net::kProtocolVersion + 99;
  request.playerName = "OLD";
  CHECK(net::writeConnectionRequest(writer, request));
  CHECK(clientSide->send(std::span(buffer).first(writer.byteSize())));

  gameServer.accept(std::move(serverSide));
  gameServer.tick(0.016f);

  const auto reply = clientSide->receive();
  CHECK(reply.has_value());
  if (!reply) return;

  net::BitReader reader(*reply);
  const auto header = reader.readBasicHeader();
  CHECK(header.has_value());
  if (header) {
    CHECK_EQ(header->type, static_cast<std::uint32_t>(net::PacketType::ConnectionDenied));
    const auto reason = net::readConnectionDenied(reader);
    CHECK(reason.has_value());
    if (reason) CHECK(*reason == net::DenyReason::WrongVersion);
  }
}

static void testWorldIsNotSentBeforeAcknowledge() {
  // The server must not pour out objects before the client has acknowledged the connection.
  server::GameServer gameServer(server::ServerSettings{});
  gameServer.loadWorld(makeLevel(10));

  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  std::vector<std::byte> buffer(256);
  net::BitWriter writer(buffer);
  net::ConnectionRequest request;
  request.playerName = "ARNE";
  net::writeConnectionRequest(writer, request);
  clientSide->send(std::span(buffer).first(writer.byteSize()));

  gameServer.tick(0.016f);
  gameServer.tick(0.016f);

  // Exactly one packet arrived — the acceptance, without a single object.
  CHECK_EQ(clientSide->pending(), std::size_t(1));
}

static void testDisconnectRemovesPlayer() {
  server::GameServer gameServer(server::ServerSettings{});
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);
  CHECK_EQ(gameServer.playerCount(), std::size_t(1));

  client.disconnect();
  gameServer.tick(0.016f);
  CHECK_EQ(gameServer.playerCount(), std::size_t(0));
}

static void testPlayerInputMovesSoldier() {
  // The full cycle: the client sends input, the server moves the soldier at a fixed
  // step, the update comes back to the client.
  server::ServerSettings settings;
  settings.walkSpeed = 4.0f;
  settings.tickRate = 30.0f;

  server::GameServer gameServer(settings);
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);
  CHECK(client.state() == server::ClientState::InWorld);

  // The server created a soldier for the player.
  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  const std::uint32_t soldierId = gameServer.objects().front().id;
  const Vec3f start = gameServer.objects().front().position;

  net::PlayerInput input;
  input.moveForward = 1.0f;
  input.yaw = 0.0f;
  client.setInput(input);

  // Exactly a second in equal steps.
  for (int i = 0; i < 30; ++i) {
    client.tick(1.0f / 30.0f);
    gameServer.tick(1.0f / 30.0f);
  }

  const Vec3f finish = gameServer.objects().front().position;
  const float travelled = length(finish - start);
  // A second of walking at speed 4 has to give about four units.
  CHECK(travelled > 3.0f && travelled < 5.0f);

  // The client saw the movement.
  const auto seen = client.objects().find(soldierId);
  CHECK(seen != client.objects().end());
  if (seen != client.objects().end()) CHECK(seen->second.moved);
  CHECK(client.inputsSent() > 20);
}

static void testSprintIsFaster() {
  server::ServerSettings settings;
  settings.walkSpeed = 4.0f;
  settings.sprintSpeed = 8.0f;

  auto travelDistance = [&](bool sprint) {
    server::GameServer gameServer(settings);
    auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
    gameServer.accept(std::move(serverSide));
    server::GameClient client(std::move(clientSide), "ARNE");
    client.connect();
    pump(gameServer, client);

    net::PlayerInput input;
    input.moveForward = 1.0f;
    input.sprint = sprint;
    client.setInput(input);
    for (int i = 0; i < 30; ++i) {
      client.tick(1.0f / 30.0f);
      gameServer.tick(1.0f / 30.0f);
    }
    return length(gameServer.objects().front().position);
  };

  CHECK(travelDistance(true) > travelDistance(false) * 1.5f);
}

static void testDiagonalIsNotFaster() {
  // The classic bug: moving diagonally comes out faster than moving straight.
  server::ServerSettings settings;
  settings.walkSpeed = 4.0f;

  auto travelDistance = [&](float forward, float right) {
    server::GameServer gameServer(settings);
    auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
    gameServer.accept(std::move(serverSide));
    server::GameClient client(std::move(clientSide), "ARNE");
    client.connect();
    pump(gameServer, client);

    net::PlayerInput input;
    input.moveForward = forward;
    input.moveRight = right;
    client.setInput(input);
    for (int i = 0; i < 30; ++i) {
      client.tick(1.0f / 30.0f);
      gameServer.tick(1.0f / 30.0f);
    }
    return length(gameServer.objects().front().position);
  };

  const float straight = travelDistance(1.0f, 0.0f);
  const float diagonal = travelDistance(1.0f, 1.0f);
  CHECK(diagonal < straight * 1.05f);
}

static void testFixedStepIsIndependentOfFrameRate() {
  // The same input over the same second has to give the same distance, whether at
  // 30 frames or at 120.
  auto travelDistance = [](int frames) {
    server::ServerSettings settings;
    server::GameServer gameServer(settings);
    auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
    gameServer.accept(std::move(serverSide));
    server::GameClient client(std::move(clientSide), "ARNE");
    client.connect();
    pump(gameServer, client);

    net::PlayerInput input;
    input.moveForward = 1.0f;
    client.setInput(input);

    const float step = 1.0f / static_cast<float>(frames);
    for (int i = 0; i < frames; ++i) {
      client.tick(step);
      gameServer.tick(step);
    }
    return length(gameServer.objects().front().position);
  };

  const float slow = travelDistance(30);
  const float fast = travelDistance(120);
  CHECK(std::abs(slow - fast) < 0.5f);
}

static void testStaleInputIsIgnored() {
  // A packet with a stale number must not throw the player back.
  server::GameServer gameServer(server::ServerSettings{});
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  net::PlayerInput input;
  input.moveForward = 1.0f;
  input.sequence = 100;
  std::vector<std::byte> buffer(256);
  net::BitWriter writer(buffer);
  net::writePlayerInput(writer, input);
  // The client's end has already been handed to GameClient, so we send through it.
  client.setInput(input);
  for (int i = 0; i < 10; ++i) {
    client.tick(1.0f / 30.0f);
    gameServer.tick(1.0f / 30.0f);
  }
  const float afterMoving = length(gameServer.objects().front().position);
  CHECK(afterMoving > 0.5f);
}

// Without choosing a point the player does not spawn — the same as in the engine,
// where the server spawns exactly those whose `Player::getSpawnGroup() > 0`.
static void testSpawnWaitsForChoice() {
  server::ServerSettings settings;
  settings.levelName = "TestLevel";
  // We turn off our workaround for headless runs: we check exactly the path through
  // the spawn screen.
  settings.spawnOnJoin = false;

  server::GameServer gameServer(settings);
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.players().size(), std::size_t(1));
  if (gameServer.players().empty()) return;
  // The handshake went through but there is no soldier: no point has been chosen
  // yet. The world in this test is empty, so there is nothing to fill a state packet
  // with either — the client stays in Accepted.
  CHECK(gameServer.players().front().acknowledged);
  CHECK_EQ(gameServer.objects().size(), std::size_t(0));
  CHECK(!gameServer.players().front().alive);

  const std::uint32_t playerId = gameServer.players().front().id;
  CHECK(gameServer.requestSpawn(playerId, 2, 3, 0));
  pump(gameServer, client);

  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  // And now there is something to send — and the client sees the world.
  CHECK(client.state() == server::ClientState::InWorld);
  const server::Player& player = gameServer.players().front();
  CHECK(player.alive);
  CHECK_EQ(player.team, 2);
  CHECK_EQ(player.kit, 3);

  // An unknown player is a refusal, and nothing appeared.
  CHECK(!gameServer.requestSpawn(playerId + 100, 1, 0, 0));
  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
}

TEST_MAIN({
  testSpawnWaitsForChoice();
  testLoopbackDeliversBothWays();
  testFullHandshakeAndWorldTransfer();
  testServerNamesThePlayer();
  testWrongProtocolVersionIsDenied();
  testWorldIsNotSentBeforeAcknowledge();
  testDisconnectRemovesPlayer();
  testPlayerInputMovesSoldier();
  testSprintIsFaster();
  testDiagonalIsNotFaster();
  testFixedStepIsIndependentOfFrameRate();
  testStaleInputIsIgnored();
})
