#pragma once
// Ігровий сервер.
//
// У BF2 сервер працює **завжди**, навіть в одиночній грі: рушій піднімає
// локальний сервер і під'єднується до нього петлею в пам'яті. Тому це не
// «мультиплеєрна добавка», а ядро — світом володіє сервер, а клієнт лише
// показує те, що йому надіслали.
//
// Структура повторює оригінал (`BF2/Game/GameServer/` за шляхами з
// вихідників): сервер тримає стан світу, приймає під'єднання, розсилає
// оновлення об'єктів.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "obf2/game/object_template.h"
#include "obf2/level/level.h"
#include "obf2/net/connection.h"
#include "obf2/net/session.h"

namespace obf2::server {

// Об'єкт у світі сервера. Поки що це розстановка з рівня плюс id.
struct WorldObject {
  std::uint32_t id = 0;
  std::string templateName;
  Vec3f position;
  Vec3f rotation;
};

struct Player {
  std::uint32_t id = 0;
  std::string name;
  std::unique_ptr<net::Connection> connection;
  bool acknowledged = false;
  // Чи надіслали ми цьому гравцеві початковий стан світу.
  bool worldSent = false;
};

struct ServerSettings {
  std::string levelName = "Dalian_plant";
  std::string gameMode = "gpm_cq";
  int maxPlayers = 16;
};

class GameServer {
 public:
  explicit GameServer(ServerSettings settings) : settings_(std::move(settings)) {}

  // Наповнює світ статичними об'єктами рівня. Саме сервер вирішує, що у
  // світі є, — клієнт про це дізнається лише з мережі.
  void loadWorld(const level::Level& level, std::size_t limit = 0);

  // Приймає нове під'єднання. Сервер бере канал у власність.
  void accept(std::unique_ptr<net::Connection> connection);

  // Один такт: розбирає вхідні пакети й розсилає оновлення.
  void tick(float deltaSeconds);

  const std::vector<WorldObject>& objects() const { return objects_; }
  std::size_t playerCount() const { return players_.size(); }
  const std::vector<Player>& players() const { return players_; }
  const ServerSettings& settings() const { return settings_; }

  long long packetsSent() const { return packetsSent_; }
  long long packetsReceived() const { return packetsReceived_; }
  const std::vector<std::string>& log() const { return log_; }

 private:
  void handlePacket(Player& player, const net::Packet& packet);
  void sendWorld(Player& player);
  bool sendTo(Player& player, std::span<const std::byte> data);

  ServerSettings settings_;
  std::vector<WorldObject> objects_;
  std::vector<Player> players_;
  std::uint32_t nextPlayerId_ = 1;
  std::uint32_t nextObjectId_ = 1;
  long long packetsSent_ = 0;
  long long packetsReceived_ = 0;
  std::vector<std::string> log_;
};

}  // namespace obf2::server
