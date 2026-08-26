#include "obf2/server/game_server.h"

#include <algorithm>

namespace obf2::server {
namespace {

// Розмір буфера під один пакет. Оновлення світу ріжуться на порції, щоб
// сюди вміщатися.
constexpr std::size_t kPacketBytes = 1200;

// Скільки об'єктів іде в одному пакеті появи.
//
// Одна поява коштує ~77 байтів: 64 на ім'я шаблону, решта на id, стиснену
// позицію й кути. У 1200 байтів (типовий безпечний розмір UDP-пакета)
// вміщається близько п'ятнадцяти, тож беремо дванадцять із запасом.
constexpr std::size_t kSpawnsPerPacket = 12;

}  // namespace

void GameServer::loadWorld(const level::Level& level, std::size_t limit) {
  objects_.clear();
  for (const level::StaticObject& object : level.objects) {
    if (limit != 0 && objects_.size() >= limit) break;
    WorldObject world;
    world.id = nextObjectId_++;
    world.templateName = object.templateName;
    world.position = object.position;
    world.rotation = object.rotation;
    objects_.push_back(std::move(world));
  }
  log_.push_back("світ завантажено: " + std::to_string(objects_.size()) + " об'єктів з рівня " +
                 level.name);
}

void GameServer::accept(std::unique_ptr<net::Connection> connection) {
  Player player;
  player.id = nextPlayerId_++;
  player.connection = std::move(connection);
  players_.push_back(std::move(player));
}

bool GameServer::sendTo(Player& player, std::span<const std::byte> data) {
  if (player.connection == nullptr || !player.connection->send(data)) return false;
  ++packetsSent_;
  return true;
}

void GameServer::handlePacket(Player& player, const net::Packet& packet) {
  net::BitReader reader(packet);
  const auto header = reader.readBasicHeader();
  if (!header) return;

  const auto type = static_cast<net::PacketType>(header->type);
  std::vector<std::byte> buffer(kPacketBytes);

  switch (type) {
    case net::PacketType::ConnectionRequest: {
      const auto request = net::readConnectionRequest(reader);
      if (!request) return;

      net::BitWriter writer(buffer);
      // Перевірки — до будь-якої зміни стану: спершу вирішуємо, чи пускаємо.
      if (request->protocolVersion != net::kProtocolVersion) {
        net::writeConnectionDenied(writer, net::DenyReason::WrongVersion);
        log_.push_back("відмова: версія " + std::to_string(request->protocolVersion));
      } else if (static_cast<int>(players_.size()) > settings_.maxPlayers) {
        net::writeConnectionDenied(writer, net::DenyReason::ServerFull);
        log_.push_back("відмова: сервер заповнений");
      } else {
        player.name = request->playerName;
        net::ConnectionAccept accept;
        accept.playerId = player.id;
        accept.levelName = settings_.levelName;
        accept.gameMode = settings_.gameMode;
        net::writeConnectionAccept(writer, accept);
        log_.push_back("під'єднався \"" + player.name + "\" (id " + std::to_string(player.id) + ")");
      }
      sendTo(player, std::span(buffer).first(writer.byteSize()));
      return;
    }

    case net::PacketType::ConnectionAcknowledge:
      player.acknowledged = true;
      log_.push_back("підтвердження від \"" + player.name + "\"");
      return;

    case net::PacketType::PingRequest: {
      net::BitWriter writer(buffer);
      writer.writeBasicHeader(
          net::BasicHeader{static_cast<std::uint32_t>(net::PacketType::PingResponse), 0});
      sendTo(player, std::span(buffer).first(writer.byteSize()));
      return;
    }

    case net::PacketType::Disconnect:
      if (player.connection != nullptr) player.connection->close();
      log_.push_back("від'єднався \"" + player.name + "\"");
      return;

    default:
      return;
  }
}

void GameServer::sendWorld(Player& player) {
  // Опорна точка стиснення — початок координат: рівень центрований на ньому,
  // тому різниці лишаються в межах карти.
  const Vec3f reference{0.0f, 0.0f, 0.0f};

  for (std::size_t start = 0; start < objects_.size(); start += kSpawnsPerPacket) {
    const std::size_t end = std::min(start + kSpawnsPerPacket, objects_.size());

    std::vector<net::ObjectUpdate> updates;
    updates.reserve(end - start);
    for (std::size_t i = start; i < end; ++i) {
      net::ObjectUpdate update;
      update.objectId = objects_[i].id;
      update.templateName = objects_[i].templateName;
      update.position = objects_[i].position;
      update.rotation = objects_[i].rotation;
      update.spawn = true;
      updates.push_back(std::move(update));
    }

    std::vector<std::byte> buffer(kPacketBytes);
    net::BitWriter writer(buffer);
    if (!net::writeObjectUpdates(writer, updates, reference)) break;
    if (!sendTo(player, std::span(buffer).first(writer.byteSize()))) break;
  }

  player.worldSent = true;
  log_.push_back("світ надіслано гравцеві \"" + player.name + "\"");
}

void GameServer::tick(float) {
  for (Player& player : players_) {
    if (player.connection == nullptr) continue;

    while (auto packet = player.connection->receive()) {
      ++packetsReceived_;
      handlePacket(player, *packet);
    }

    // Стан світу йде лише після підтвердження — інакше клієнт отримав би
    // об'єкти ще до того, як дізнався свій id і рівень.
    if (player.acknowledged && !player.worldSent) sendWorld(player);
  }

  // Прибираємо тих, хто відвалився.
  players_.erase(std::remove_if(players_.begin(), players_.end(),
                                [](const Player& player) {
                                  return player.connection == nullptr ||
                                         !player.connection->connected();
                                }),
                 players_.end());
}

}  // namespace obf2::server
