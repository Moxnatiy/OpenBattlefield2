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

  // Увесь світ доїхав, і назви шаблонів збереглися.
  CHECK_EQ(client.objects().size(), std::size_t(50));
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

TEST_MAIN({
  testLoopbackDeliversBothWays();
  testFullHandshakeAndWorldTransfer();
  testServerNamesThePlayer();
  testWrongProtocolVersionIsDenied();
  testWorldIsNotSentBeforeAcknowledge();
  testDisconnectRemovesPlayer();
})
