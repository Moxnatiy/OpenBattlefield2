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
#include <map>
#include <memory>
#include <random>
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

// Стан раунду. В оригіналі це `dice::hfe::GameStatus`, і всю гру крутить
// машина станів `ServerGameLogic::update`.
enum class GameStatus { PreGame, Playing, EndGame };

// Положення прапора на щоглі. Змінити власника точки можна лише внизу —
// звідси й «нейтралізація» перед захопленням.
enum class FlagPosition { Bottom, Middle, Top };

// Квитки команди. Втрата дробова (витік за секунду), а показуємо ціле.
struct TeamState {
  int tickets = 0;
  float fraction = 0.0f;              // накопичена дробова частина втрати
  float ticketChangePerSecond = 0.0f;  // < 0 — тече
  int ticketState = 0;                 // рівень попередження для інтерфейсу
  float areaValue = 0.0f;              // сума ваг утримуваних точок
  int controlPoints = 0;
};

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
  // Техніка зі спавнера: щоб знати, що переставляти при зміні власника точки.
  int spawnerIndex = -1;
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
  // Обране місце появи — номер контрольної точки, з якої гравець просив
  // з'явитися. Нуль означає «ще не обрав», і це не наша вигадка: у
  // рушії сервер спавнить рівно тих, у кого `Player::getSpawnGroup() > 0`
  // (`ServerGameLogic::uPlayingSpawning`), а виставляє це поле подія
  // `NESelectSpawnGroup` — див. docs/functions/network-events.md.
  int spawnGroup = 0;
  // Обраний набір (`NESelectKit`). Поки лише запам'ятовуємо: спорядження
  // наборів ще не розібране.
  int kit = 0;
  bool alive = false;
  // Здоров'я з даних солдата (`ObjectTemplate.armor.maxHitPoints 100`).
  float health = 100.0f;
  // Пливе. Перемикається з гістерезисом: пороги входу й виходу різні
  // (`phy-soldier-start-float` / `stop-float`), інакше на межі смикається.
  bool swimming = false;
  float respawnTimer = 0.0f;  // скільки лишилося чекати до появи
};

struct ServerSettings {
  std::string levelName = "Dalian_plant";
  std::string gameMode = "gpm_cq";
  int maxPlayers = 16;

  // Частота симуляції. Не «звична» стала: `WorldPref::mTickTime` лежить
  // у `.data` лінукс-сервера за 0xf68c50 і дорівнює 0.0333333333333333
  // (double), тобто рівно 1/30 с. Див. `obf2::server::kTickTime`.
  float tickRate = 30.0f;

  // Швидкості солдата беруться з констант рушія (`phy-soldier-run-speed`
  // 3.9 і `phy-soldier-sprint-speed` 7), але лишаються тут, щоб тест міг
  // задати свої. Нуль означає «взяти з фізики».
  float walkSpeed = 0.0f;
  float sprintSpeed = 0.0f;

  // Радіус солдата для зіткнень — з `coll-soldier-radius` (0.25).
  // Лишається тут лише для сумісності; форму задає PhysicsConstants.
  float soldierRadius = 0.25f;
  // Здоров'я солдата: у всіх наборах BF2 це рівно 100.
  float soldierMaxHealth = 100.0f;
  Vec3f spawnPosition{0.0f, 0.0f, 0.0f};
  std::string soldierTemplate = "player_soldier";

  // Константи руху з даних гри (Vars.Set phy-soldier-*).
  PhysicsConstants physics;

  // Скільки секунд гравець чекає до появи. В оригіналі це залежить від
  // режиму й квитків; поки що стала.
  float respawnDelay = 3.0f;
  // З'являтися одразу після підтвердження, не чекаючи вибору місця.
  // **Це наше, а не з рушія**: в оригіналі такого шляху немає взагалі,
  // справжній клієнт завжди проходить екран появи і шле
  // `NESelectSpawnGroup`. Прапорець потрібен тестам і безголовим
  // запускам; застосунок гасить його, щойно показує екран появи.
  bool spawnOnJoin = true;

  // Скільки гравців потрібно, щоб раунд почався (`sv.numPlayersNeededToStart`,
  // типово 2). Поки їх менше, гра тримає посеред екрана напис
  // HUD_STARTOFROUND_NRPLAYERSNEEDED.
  int playersNeededToStart = 2;

  // --- квитки (значення з даних і з коду оригіналу) ---
  //
  // Стартова кількість задається `gameLogic.setDefaultNumberOfTickets` у
  // GameLogicInit.con (для bf2 — 250 на команду), у самому рушії за
  // замовчуванням 50. Множник `sv.ticketRatio` — 100 %.
  int defaultTickets[3] = {0, 50, 50};
  float ticketRatio = 100.0f;
  // Швидкість витоку при повній перевазі противника, квитків за хвилину.
  // У ServerGameLogic::reset це 10 на команду.
  float ticketLossPerMin[3] = {0.0f, 10.0f, 10.0f};
  // Коли в команди не лишилось ні точок, ні живих — вона тече ось так
  // (у конструкторі оригіналу 1000 за хвилину, тобто майже миттєво).
  float ticketLossAtEndPerMin = 1000.0f;
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

  // Стан захоплення однієї точки. Модель — як у gpm_cq.py: прапор їде
  // вгору-вниз, власник змінюється лише коли прапор унизу.
  struct ControlPointState {
    int id = 0;
    std::string nameKey;
    Vec3f position;
    float radius = 10.0f;
    int team = 0;  // власник; 0 — нейтральна

    int flagTeam = 0;        // чий прапор зараз на щоглі
    float takeOver = 0.0f;   // 0 — низ, 1 — верх
    float takeOverChangePerSecond = 0.0f;
    FlagPosition flagPosition = FlagPosition::Bottom;
    int occupantsTeam1 = 0;
    int occupantsTeam2 = 0;

    // З шаблону ControlPoint у GamePlayObjects.con.
    float timeToGetControl = 20.0f;
    float timeToLoseControl = 20.0f;
    float areaValueTeam1 = 0.0f;
    float areaValueTeam2 = 0.0f;
    bool unableToChangeTeam = false;
    int onlyTakeableByTeam = 0;
    int enemyTicketLossWhenCaptured = 0;

    // Сумісність із попереднім виглядом: скільки лишилось до зміни.
    int capturingTeam() const { return flagTeam; }
    float progress() const { return takeOver; }
  };
  const std::vector<ControlPointState>& controlPoints() const { return controlPoints_; }

  GameStatus status() const { return status_; }
  const TeamState& team(int index) const { return teams_[index == 2 ? 2 : 1]; }
  int tickets(int index) const { return team(index).tickets; }
  // 0 — раунд триває, інакше номер команди-переможця.
  int winner() const { return winner_; }
  float groundHeightAt(const Vec3f& position) const;

  // Вбиває гравця: команда втрачає квиток, далі чекання й нова поява.
  // Так само, як onPlayerDeath у gpm_cq.py.
  void killPlayer(std::uint32_t playerId, std::string_view reason);

  // Гравець натиснув DONE на екрані появи: команда, набір і номер
  // контрольної точки, з якої він хоче з'явитися. Нуль у `spawnGroup`
  // означає «будь-яка своя точка». Сам солдат з'явиться наступним
  // тактом — так само, як у рушії, де це робить прохід
  // `ServerGameLogic::uPlayingSpawning`, а не сама подія.
  bool requestSpawn(std::uint32_t playerId, int team, int kit, int spawnGroup);

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
  // Ставить техніку зі спавнерів: шаблон залежить від того, чия точка,
  // до якої спавнер прив'язаний.
  void spawnVehicles();
  void updateControlPoints(float step);
  // Перерахунок швидкості підйому прапора однієї точки.
  void refreshTakeOver(ControlPointState& point);
  // Прапор дійшов до краю: захоплення або нейтралізація.
  void onFlagReachedEnd(ControlPointState& point, bool top);
  // Витік квитків залежно від того, хто скільки точок тримає.
  void updateTicketLoss();
  void updateTickets(float step);
  void endGame(int winner);
  // Де з'явитися гравцеві: найближча точка своєї команди, інакше стартова.
  // Вибір точки появи за логікою рушія (див. docs/functions/spawn.md).
  // `spawnGroup` — номер контрольної точки, якою обмежений вибір. Нуль
  // знімає обмеження. Група в рушії — це і є набір точок одного прапора
  // (`SpawnGroup::getControlPointId`), а всередині групи точка береться
  // випадково (`SpawnGroup::getSpawnPoint`).
  const level::SpawnPoint* pickSpawnPoint(int team, bool forHuman, int spawnGroup) const;
  bool spawnPointActive(const level::SpawnPoint& spawn, int team, bool forHuman) const;
  Vec3f chooseSpawn(int team, int spawnGroup);
  bool sendTo(Player& player, std::span<const std::byte> data);

  ServerSettings settings_;
  const level::Level* terrain_ = nullptr;
  std::unique_ptr<CollisionWorld> collision_;
  level::GameplayObjects gameplay_;
  std::vector<ControlPointState> controlPoints_;
  GameStatus status_ = GameStatus::PreGame;
  TeamState teams_[3];
  int winner_ = 0;
  std::vector<WorldObject> objects_;
  std::vector<Player> players_;
  std::uint32_t nextPlayerId_ = 1;
  std::uint32_t nextObjectId_ = 1;
  float accumulator_ = 0.0f;
  std::uint64_t tickCount_ = 0;
  long long packetsSent_ = 0;
  long long packetsReceived_ = 0;
  std::vector<std::string> log_;

  // Час у світі — потрібен для затримки повторної появи на тій самій точці
  // (`spawnPreventionDelay`), і скільки його минуло з останньої появи.
  float worldTime_ = 0.0f;
  std::map<const level::SpawnPoint*, float> lastSpawnTime_;
  // Вибір точки в оригіналі випадковий (`rand() % кількість`). Свій
  // генератор тримаємо, щоб тести лишалися відтворюваними.
  mutable std::minstd_rand random_{12345};
};

}  // namespace obf2::server
