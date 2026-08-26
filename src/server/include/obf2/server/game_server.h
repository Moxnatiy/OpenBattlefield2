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
#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"
#include "obf2/net/connection.h"
#include "obf2/net/session.h"
#include "obf2/server/collision_world.h"
#include "obf2/server/physics.h"

namespace obf2::server {

// Об'єкт у світі сервера.
struct WorldObject {
  std::uint32_t id = 0;
  std::string templateName;
  Vec3f position;
  Vec3f rotation;
  Vec3f velocity;
  bool onGround = false;

  // Статику надсилаємо раз при появі, рухоме — щотакту. Без цього поділу
  // 907 будинків їхали б у мережу шістдесят разів на секунду.
  bool dynamic = false;
  std::uint32_t ownerPlayerId = 0;  // 0 = нічий
};

struct Player {
  std::uint32_t id = 0;
  std::string name;
  std::unique_ptr<net::Connection> connection;
  bool acknowledged = false;
  // Чи надіслали ми цьому гравцеві початковий стан світу.
  bool worldSent = false;

  net::PlayerInput input;          // останній отриманий ввід
  std::uint32_t lastSequence = 0;  // щоб не застосувати старий пакет двічі
  std::uint32_t soldierId = 0;     // об'єкт, яким гравець керує

  int team = 1;
  bool alive = false;
  float respawnTimer = 0.0f;  // скільки лишилося чекати до появи
};

struct ServerSettings {
  std::string levelName = "Dalian_plant";
  std::string gameMode = "gpm_cq";
  int maxPlayers = 16;

  // Частота симуляції. BF2 крутив сервер на 30 тактах; фіксований крок
  // потрібен, щоб рух не залежав від навантаження машини.
  float tickRate = 30.0f;

  // Швидкості солдата у світових одиницях за секунду.
  float walkSpeed = 4.0f;
  float sprintSpeed = 7.0f;

  // Радіус солдата для зіткнень. У BF2 це капсула; сфера трохи грубіша,
  // але вже не пускає крізь стіни.
  float soldierRadius = 0.4f;
  Vec3f spawnPosition{0.0f, 0.0f, 0.0f};
  std::string soldierTemplate = "player_soldier";

  // Константи руху з даних гри (Vars.Set phy-soldier-*).
  PhysicsConstants physics;

  // Скільки секунд гравець чекає до появи. В оригіналі це залежить від
  // режиму й квитків; поки що стала.
  float respawnDelay = 3.0f;
  // За скільки секунд нейтральна точка переходить до команди, яка її тримає.
  float captureSeconds = 10.0f;
};

class GameServer {
 public:
  explicit GameServer(ServerSettings settings) : settings_(std::move(settings)) {}

  // Наповнює світ статичними об'єктами рівня. Саме сервер вирішує, що у
  // світі є, — клієнт про це дізнається лише з мережі.
  void loadWorld(const level::Level& level, std::size_t limit = 0);

  // Рельєф для зіткнення з землею. Без нього солдат падає без кінця.
  void setTerrain(const level::Level* level) { terrain_ = level; }

  // Геометрія зіткнень рівня. Будує її застосунок (у нього є VFS і реєстр),
  // а володіє сервер — бо саме він вирішує, куди гравець дійшов.
  void setCollision(std::unique_ptr<CollisionWorld> world) { collision_ = std::move(world); }
  const CollisionWorld* collision() const { return collision_.get(); }

  // Логіка режиму: контрольні точки й спавнери техніки.
  void setGameplay(level::GameplayObjects gameplay);

  // Стан захоплення однієї точки.
  struct ControlPointState {
    int id = 0;
    std::string nameKey;
    Vec3f position;
    float radius = 10.0f;
    int team = 0;
    float progress = 0.0f;   // 0..1 у бік команди, що захоплює
    int capturingTeam = 0;
  };
  const std::vector<ControlPointState>& controlPoints() const { return controlPoints_; }
  float groundHeightAt(const Vec3f& position) const;

  // Приймає нове під'єднання. Сервер бере канал у власність.
  void accept(std::unique_ptr<net::Connection> connection);

  // Розбирає вхідні пакети й крутить симуляцію фіксованим кроком.
  // deltaSeconds — реальний час кадру; всередині він накопичується.
  void tick(float deltaSeconds);

  std::uint64_t tickCount() const { return tickCount_; }
  float tickInterval() const { return 1.0f / settings_.tickRate; }

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
  void simulate(float step);
  void broadcastDynamic();
  WorldObject* findObject(std::uint32_t id);
  std::uint32_t spawnSoldier(Player& player);
  void updateControlPoints(float step);
  // Де з'явитися гравцеві: найближча точка своєї команди, інакше стартова.
  Vec3f chooseSpawn(int team) const;
  bool sendTo(Player& player, std::span<const std::byte> data);

  ServerSettings settings_;
  const level::Level* terrain_ = nullptr;
  std::unique_ptr<CollisionWorld> collision_;
  level::GameplayObjects gameplay_;
  std::vector<ControlPointState> controlPoints_;
  std::vector<WorldObject> objects_;
  std::vector<Player> players_;
  std::uint32_t nextPlayerId_ = 1;
  std::uint32_t nextObjectId_ = 1;
  float accumulator_ = 0.0f;
  std::uint64_t tickCount_ = 0;
  long long packetsSent_ = 0;
  long long packetsReceived_ = 0;
  std::vector<std::string> log_;
};

}  // namespace obf2::server
