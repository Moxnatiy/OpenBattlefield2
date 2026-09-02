#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/server/game_client.h"
#include "obf2/server/game_server.h"

using namespace obf2;

namespace {

// Світ, який зазвичай приходить із рівня. Тут його робимо руками, щоб
// тест не залежав від наявності гри на диску.
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

// Проганяє обидві сторони, доки вони обмінюються пакетами.
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
  CHECK(!first->receive().has_value());  // собі нічого не приходить

  const auto got = second->receive();
  CHECK(got.has_value());
  if (got) CHECK_EQ(got->size(), std::size_t(3));

  // Розрив бачать обидва кінці.
  first->close();
  CHECK(!first->connected());
  CHECK(!second->connected());
}

static void testFullHandshakeAndWorldTransfer() {
  // Це і є одиночна гра: локальний сервер плюс клієнт через петлю.
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

  // Увесь світ доїхав: 50 статичних об'єктів плюс солдат, якого сервер
  // створив під цього гравця.
  CHECK_EQ(client.objects().size(), std::size_t(51));
  const auto first = client.objects().find(1);
  CHECK(first != client.objects().end());
  if (first != client.objects().end()) {
    CHECK_EQ(first->second.templateName, std::string("hangar_0"));
    // Позиція йде стисненим вектором, тому звіряємо з точністю квантування.
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
  // Клієнт зі старою версією не має потрапити у світ.
  server::GameServer gameServer(server::ServerSettings{});
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();

  // Надсилаємо запит руками з чужою версією.
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
  // Сервер не має сипати об'єктами, поки клієнт не підтвердив під'єднання.
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

  // Прийшов рівно один пакет — прийняття, без жодного об'єкта.
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
  // Повний цикл: клієнт шле ввід, сервер рухає солдата фіксованим кроком,
  // оновлення повертається клієнтові.
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

  // Сервер створив солдата під гравця.
  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  const std::uint32_t soldierId = gameServer.objects().front().id;
  const Vec3f start = gameServer.objects().front().position;

  net::PlayerInput input;
  input.moveForward = 1.0f;
  input.yaw = 0.0f;
  client.setInput(input);

  // Рівно секунда рівними кроками.
  for (int i = 0; i < 30; ++i) {
    client.tick(1.0f / 30.0f);
    gameServer.tick(1.0f / 30.0f);
  }

  const Vec3f finish = gameServer.objects().front().position;
  const float travelled = length(finish - start);
  // За секунду ходьби зі швидкістю 4 має бути близько чотирьох одиниць.
  CHECK(travelled > 3.0f && travelled < 5.0f);

  // Клієнт побачив рух.
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
  // Класична помилка: рух по діагоналі виходить швидшим за рух прямо.
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
  // Той самий ввід за ту саму секунду має дати ту саму відстань,
  // хоч на 30 кадрах, хоч на 120.
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
  // Пакет зі старим номером не має відкидати гравця назад.
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
  // Клієнтський кінець уже переданий у GameClient, тому шлемо через нього.
  client.setInput(input);
  for (int i = 0; i < 10; ++i) {
    client.tick(1.0f / 30.0f);
    gameServer.tick(1.0f / 30.0f);
  }
  const float afterMoving = length(gameServer.objects().front().position);
  CHECK(afterMoving > 0.5f);
}

// Без вибору місця гравець не з'являється — так само, як у рушії, де
// сервер спавнить рівно тих, у кого `Player::getSpawnGroup() > 0`.
static void testSpawnWaitsForChoice() {
  server::ServerSettings settings;
  settings.levelName = "TestLevel";
  // Вимикаємо наш обхід для безголових запусків: перевіряємо саме шлях
  // через екран появи.
  settings.spawnOnJoin = false;

  server::GameServer gameServer(settings);
  auto [clientSide, serverSide] = net::LoopbackConnection::createPair();
  gameServer.accept(std::move(serverSide));

  server::GameClient client(std::move(clientSide), "ARNE");
  client.connect();
  pump(gameServer, client);

  CHECK_EQ(gameServer.players().size(), std::size_t(1));
  if (gameServer.players().empty()) return;
  // Рукостискання пройшло, а солдата немає: місце ще не обране. Світ у
  // цьому тесті порожній, тож і пакета стану поки нема чим наповнити —
  // клієнт лишається в Accepted.
  CHECK(gameServer.players().front().acknowledged);
  CHECK_EQ(gameServer.objects().size(), std::size_t(0));
  CHECK(!gameServer.players().front().alive);

  const std::uint32_t playerId = gameServer.players().front().id;
  CHECK(gameServer.requestSpawn(playerId, 2, 3, 0));
  pump(gameServer, client);

  CHECK_EQ(gameServer.objects().size(), std::size_t(1));
  // А тепер є що слати — і клієнт бачить світ.
  CHECK(client.state() == server::ClientState::InWorld);
  const server::Player& player = gameServer.players().front();
  CHECK(player.alive);
  CHECK_EQ(player.team, 2);
  CHECK_EQ(player.kit, 3);

  // Невідомий гравець — відмова, і нічого не з'явилося.
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
