#include "obf2/server/game_server.h"

#include <algorithm>
#include <cmath>

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
    // Рослинність мережею не ходить: в оригіналі її малює клієнт сам за
    // даними рівня, а серверу вона потрібна тільки для зіткнень.
    if (object.isOvergrowth) continue;
    WorldObject world;
    world.id = nextObjectId_++;
    world.templateName = object.templateName;
    world.position = object.position;
    world.rotation = object.rotation;
    objects_.push_back(std::move(world));
  }
  log_.push_back("світ завантажено: " + std::to_string(objects_.size()) + " об'єктів з рівня " +
                 level.name);
  // Статику щойно перезаписали — техніку зі спавнерів треба поставити знову.
  spawnVehicles();
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

    case net::PacketType::Data: {
      // Від клієнта тип Data означає ввід (підтип 1 у заголовку).
      if (header->subtype != 1) return;
      const auto input = net::readPlayerInput(reader);
      if (!input) return;

      // Порядковий номер 16-бітний і перевертається; враховуємо це, інакше
      // після 65535 такту гравець застряг би назавжди.
      const std::uint32_t previous = player.lastSequence;
      const std::uint32_t delta = (input->sequence - previous) & 0xFFFFu;
      if (previous != 0 && (delta == 0 || delta > 0x8000u)) return;  // старий пакет

      player.input = *input;
      player.lastSequence = input->sequence;
      return;
    }

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

WorldObject* GameServer::findObject(std::uint32_t id) {
  for (WorldObject& object : objects_) {
    if (object.id == id) return &object;
  }
  return nullptr;
}

void GameServer::setGameplay(level::GameplayObjects gameplay) {
  gameplay_ = std::move(gameplay);
  controlPoints_.clear();
  for (const level::ControlPoint& point : gameplay_.controlPoints) {
    ControlPointState state;
    state.id = point.id;
    state.nameKey = point.nameKey;
    state.position = point.position;
    state.radius = point.radius;
    state.team = point.team;
    state.flagTeam = point.team;
    state.takeOver = point.team != 0 ? 1.0f : 0.0f;
    state.flagPosition = point.team != 0 ? FlagPosition::Top : FlagPosition::Bottom;
    state.timeToGetControl = point.timeToGetControl;
    state.timeToLoseControl = point.timeToLoseControl;
    state.areaValueTeam1 = point.areaValueTeam1;
    state.areaValueTeam2 = point.areaValueTeam2;
    state.unableToChangeTeam = point.unableToChangeTeam;
    state.onlyTakeableByTeam = point.onlyTakeableByTeam;
    state.enemyTicketLossWhenCaptured = point.enemyTicketLossWhenCaptured;
    controlPoints_.push_back(std::move(state));

    // Прапор має бути видимим, тож контрольна точка їде клієнтові ще й
    // звичайним об'єктом — так само робить оригінал: у його потоці світу
    // ці шаблони приходять `CreateObjectEvent`-ами нарівні з технікою.
    // Геометрії в самому шаблоні немає, вона в нащадка (`addTemplate
    // flagpole`), і збирання об'єкта це враховує.
    if (!point.templateName.empty()) {
      WorldObject flag;
      flag.id = nextObjectId_++;
      flag.templateName = point.templateName;
      flag.position = point.position;
      objects_.push_back(std::move(flag));
    }
  }

  // Квитки заводяться на початку раунду — так само, як це робить
  // gpm_cq.py у відповідь на перехід у стан Playing.
  for (int team = 1; team <= 2; ++team) {
    teams_[team] = TeamState{};
    teams_[team].tickets =
        static_cast<int>(settings_.defaultTickets[team] * (settings_.ticketRatio / 100.0f));
  }
  status_ = GameStatus::Playing;
  winner_ = 0;
  updateTicketLoss();
  spawnVehicles();
  log_.push_back("логіка режиму: " + std::to_string(controlPoints_.size()) +
                 " контрольних точок, " + std::to_string(gameplay_.spawners.size()) +
                 " спавнерів");
}

bool GameServer::spawnPointActive(const level::SpawnPoint& spawn, int team,
                                  bool forHuman) const {
  // Порядок перевірок — як у `SpawnPoint::getActive` рушія.
  if (!spawn.active) return false;
  if (forHuman ? spawn.onlyForAI : spawn.onlyForHuman) return false;

  // Точка, прив'язана до прапора, працює лише поки прапор наш.
  if (spawn.controlPointId != 0) {
    const ControlPointState* point = nullptr;
    for (const ControlPointState& candidate : controlPoints_) {
      if (candidate.id == spawn.controlPointId) { point = &candidate; break; }
    }
    if (point == nullptr || point->team != team) return false;
  }

  // Зайнята: хтось щойно з'явився тут. Затримка типово нульова, тож
  // насправді це вмикається лише там, де рівень її задав.
  if (spawn.spawnPreventionDelay > 0.0f) {
    const auto found = lastSpawnTime_.find(&spawn);
    if (found != lastSpawnTime_.end() &&
        worldTime_ < found->second + spawn.spawnPreventionDelay) {
      return false;
    }
  }

  // Зависоко над землею — теж не годиться (типово вимкнено, -1).
  if (spawn.minSpawnHeight >= 0.0f) {
    const float ground = groundHeightAt(spawn.position);
    if (spawn.position.y - ground < spawn.minSpawnHeight) return false;
  }

  // І головне: поруч ніхто не стоїть. Рушій шукає об'єкти в радіусі 1 м.
  for (const Player& player : players_) {
    if (!player.alive || player.soldierId == 0) continue;
    for (const WorldObject& object : objects_) {
      if (object.id != player.soldierId) continue;
      const Vec3f delta = object.position - (spawn.position + spawn.offset);
      if (length(delta) < 1.0f) return false;
      break;
    }
  }
  return true;
}

const level::SpawnPoint* GameServer::pickSpawnPoint(int team, bool forHuman,
                                                    int spawnGroup) const {
  // Спершу збираємо всі придатні, потім беремо випадкову — рівно так це
  // робить `SpawnGroup::getSpawnPoint`, а не по колу.
  std::vector<const level::SpawnPoint*> usable;
  for (const level::SpawnPoint& spawn : gameplay_.spawnPoints) {
    // Гравець обрав прапор — беремо лише його групу. У рушії вибір і
    // йде по групі: гравець шле її номер, а точку в ній сервер добирає
    // сам (docs/functions/spawn.md).
    if (spawnGroup != 0 && spawn.controlPointId != spawnGroup) continue;
    if (spawnPointActive(spawn, team, forHuman)) usable.push_back(&spawn);
  }
  // Обрана група не має жодної придатної точки — беремо будь-яку свою,
  // інакше гравець застряг би на екрані появи назавжди.
  if (usable.empty() && spawnGroup != 0) return pickSpawnPoint(team, forHuman, 0);
  if (usable.empty()) return nullptr;

  std::uniform_int_distribution<std::size_t> pick(0, usable.size() - 1);
  return usable[pick(random_)];
}

Vec3f GameServer::chooseSpawn(int team, int spawnGroup) {
  if (const level::SpawnPoint* spawn = pickSpawnPoint(team, true, spawnGroup)) {
    lastSpawnTime_[spawn] = worldTime_;
    return spawn->position + spawn->offset;
  }

  // Точок появи немає — стаємо просто на прапор.
  for (const ControlPointState& point : controlPoints_) {
    if (point.team == team) return point.position;
  }
  return settings_.spawnPosition;
}

void GameServer::spawnVehicles() {
  // Прибираємо те, що вже стоїть від спавнерів: власник точки міг змінитися.
  objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                [](const WorldObject& object) { return object.spawnerIndex >= 0; }),
                 objects_.end());

  int placed = 0;
  for (std::size_t i = 0; i < gameplay_.spawners.size(); ++i) {
    const level::ObjectSpawner& spawner = gameplay_.spawners[i];

    // Яку саме машину видати, вирішує команда — власник прив'язаної точки.
    int team = spawner.teamOnVehicle;
    for (const ControlPointState& point : controlPoints_) {
      if (point.id == spawner.controlPointId) { team = point.team; break; }
    }
    const auto found = spawner.templateByTeam.find(team);
    if (found == spawner.templateByTeam.end() || found->second.empty()) continue;

    WorldObject vehicle;
    vehicle.id = nextObjectId_++;
    vehicle.templateName = found->second;
    vehicle.position = spawner.position;
    vehicle.rotation = spawner.rotation;
    vehicle.spawnerIndex = static_cast<int>(i);
    objects_.push_back(std::move(vehicle));
    ++placed;
  }
  log_.push_back("техніка зі спавнерів: " + std::to_string(placed) + " з " +
                 std::to_string(gameplay_.spawners.size()));
}

void GameServer::refreshTakeOver(ControlPointState& point) {
  // Точку взагалі не можна перебрати — прапор стоїть.
  if (point.unableToChangeTeam) {
    point.takeOverChangePerSecond = 0.0f;
    return;
  }

  const int overweight = point.occupantsTeam1 - point.occupantsTeam2;
  const int attackingTeam = overweight > 0 ? 1 : (overweight < 0 ? 2 : 0);

  float attackOverWeight = 0.0f;
  float timeToChange = point.timeToLoseControl;

  if (point.occupantsTeam1 == 0 && point.occupantsTeam2 == 0) {
    // Нікого немає: нейтральна точка повільно опускає прапор, а чиясь —
    // так само повільно піднімає свій назад.
    attackOverWeight = point.team == 0 ? -0.5f : 0.5f;
  } else if (point.flagTeam == attackingTeam ||
             (point.flagPosition == FlagPosition::Bottom && point.team == 0)) {
    // Прапор уже наш (або щогла порожня) — піднімаємо.
    attackOverWeight = static_cast<float>(std::abs(overweight));
    timeToChange = point.timeToGetControl;
  } else {
    // На щоглі чужий прапор: спершу його треба спустити.
    attackOverWeight = -static_cast<float>(std::abs(overweight));
  }

  if (point.onlyTakeableByTeam != 0 && point.onlyTakeableByTeam != attackingTeam) return;

  // Змінити власника прапора можна тільки внизу.
  if (point.flagPosition == FlagPosition::Bottom) point.flagTeam = attackingTeam;

  float rate = timeToChange > 0.0f ? attackOverWeight / timeToChange : 0.0f;
  if ((point.flagPosition == FlagPosition::Top && rate > 0.0f) ||
      (point.flagPosition == FlagPosition::Bottom && rate < 0.0f)) {
    rate = 0.0f;
  }
  if (rate != 0.0f) point.flagPosition = FlagPosition::Middle;
  point.takeOverChangePerSecond = rate;
}

void GameServer::onFlagReachedEnd(ControlPointState& point, bool top) {
  point.flagPosition = top ? FlagPosition::Top : FlagPosition::Bottom;

  int newTeam = -1;
  if (point.team != 0) {
    // Чужий прапор спустили — точка стає нічия.
    if (!top) newTeam = 0;
  } else if (top) {
    newTeam = point.flagTeam;
  }
  if (newTeam < 0) return;

  // Захоплення одразу знімає квитки в противника.
  if (newTeam > 0 && point.enemyTicketLossWhenCaptured > 0) {
    const int punished = newTeam == 1 ? 2 : 1;
    teams_[punished].tickets -= point.enemyTicketLossWhenCaptured;
  }

  if (point.team != newTeam) {
    point.team = newTeam;
    log_.push_back(newTeam == 0
                       ? "точку " + std::to_string(point.id) + " нейтралізовано"
                       : "точку " + std::to_string(point.id) + " захопила команда " +
                             std::to_string(newTeam));
  }
  updateTicketLoss();
  spawnVehicles();
}

void GameServer::updateControlPoints(float step) {
  for (ControlPointState& point : controlPoints_) {
    // Скільки живих із кожної команди стоїть у радіусі. Оригінал рахує це
    // на входах-виходах через тригери; ми — щотакту, результат той самий.
    point.occupantsTeam1 = 0;
    point.occupantsTeam2 = 0;

    for (const Player& player : players_) {
      if (!player.alive || player.soldierId == 0) continue;
      const WorldObject* soldier = nullptr;
      for (const WorldObject& object : objects_) {
        if (object.id == player.soldierId) { soldier = &object; break; }
      }
      if (soldier == nullptr) continue;

      const Vec3f delta = soldier->position - point.position;
      // Радіус горизонтальний: висота не має заважати захопленню.
      const float distance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
      if (distance > point.radius) continue;

      if (player.team == 1) ++point.occupantsTeam1;
      else if (player.team == 2) ++point.occupantsTeam2;
    }

    refreshTakeOver(point);

    if (point.takeOverChangePerSecond == 0.0f) continue;
    point.takeOver += point.takeOverChangePerSecond * step;

    if (point.takeOver >= 1.0f) {
      point.takeOver = 1.0f;
      onFlagReachedEnd(point, true);
    } else if (point.takeOver <= 0.0f) {
      point.takeOver = 0.0f;
      onFlagReachedEnd(point, false);
    }
  }
}

void GameServer::killPlayer(std::uint32_t playerId, std::string_view reason) {
  for (Player& player : players_) {
    if (player.id != playerId || !player.alive) continue;

    player.alive = false;
    player.health = 0.0f;
    player.respawnTimer = settings_.respawnDelay;

    // Смерть коштує команді один квиток — правило режиму, не рушія.
    if (player.team == 1 || player.team == 2) --teams_[player.team].tickets;

    log_.push_back("гравець \"" + player.name + "\" загинув: " + std::string(reason));
    // Востаннє живий у команді без точок — вмикається кінцевий витік.
    updateTicketLoss();
    return;
  }
}

void GameServer::updateTicketLoss() {
  for (int team = 1; team <= 2; ++team) {
    teams_[team].areaValue = 0.0f;
    teams_[team].controlPoints = 0;
  }
  for (const ControlPointState& point : controlPoints_) {
    if (point.team != 1 && point.team != 2) continue;
    teams_[point.team].areaValue +=
        point.team == 1 ? point.areaValueTeam1 : point.areaValueTeam2;
    ++teams_[point.team].controlPoints;
  }

  // Команда без жодної точки й без живих гравців стікає майже миттєво.
  for (int losing = 1; losing <= 2; ++losing) {
    if (teams_[losing].controlPoints != 0) continue;
    bool anyoneAlive = false;
    for (const Player& player : players_) {
      if (player.team == losing && player.alive) { anyoneAlive = true; break; }
    }
    if (anyoneAlive) continue;

    const int winning = losing == 1 ? 2 : 1;
    teams_[losing].ticketChangePerSecond = -settings_.ticketLossAtEndPerMin / 60.0f;
    teams_[winning].ticketChangePerSecond = 0.0f;
    return;
  }

  // Звичайний витік: тече та команда, у якої менша сумарна вага площі,
  // і лише коли противник набрав щонайменше 100.
  const float overweight1 = teams_[1].areaValue - teams_[2].areaValue;
  const auto lossFor = [&](int team, float enemyArea, float enemyOverweight) {
    if (enemyArea < 100.0f || enemyOverweight <= 0.0f) return 0.0f;
    return (settings_.ticketLossPerMin[team] / 60.0f) * (enemyOverweight / 100.0f);
  };
  teams_[1].ticketChangePerSecond = -lossFor(1, teams_[2].areaValue, -overweight1);
  teams_[2].ticketChangePerSecond = -lossFor(2, teams_[1].areaValue, overweight1);
}

void GameServer::updateTickets(float step) {
  for (int team = 1; team <= 2; ++team) {
    TeamState& state = teams_[team];
    if (state.ticketChangePerSecond != 0.0f) {
      state.fraction += state.ticketChangePerSecond * step;
      const int whole = static_cast<int>(state.fraction);
      if (whole != 0) {
        state.tickets += whole;
        state.fraction -= static_cast<float>(whole);
      }
    }
    if (state.tickets <= 0 && status_ == GameStatus::Playing) {
      state.tickets = 0;
      endGame(team == 1 ? 2 : 1);
    }
  }
}

void GameServer::endGame(int winner) {
  status_ = GameStatus::EndGame;
  winner_ = winner;
  for (int team = 1; team <= 2; ++team) teams_[team].ticketChangePerSecond = 0.0f;
  log_.push_back("раунд завершено, перемогла команда " + std::to_string(winner));
}

bool GameServer::requestSpawn(std::uint32_t playerId, int team, int kit, int spawnGroup) {
  for (Player& player : players_) {
    if (player.id != playerId) continue;
    player.team = team == 2 ? 2 : 1;
    player.kit = kit;
    // Нуль тут означав би «ще не обрав», а гравець уже натиснув DONE.
    // Тому «будь-яка своя точка» лишається нулем у `spawnGroup`, але
    // сам факт вибору позначаємо появою солдата наступним тактом.
    player.spawnGroup = spawnGroup;
    if (player.soldierId == 0) {
      player.soldierId = spawnSoldier(player);
    } else if (!player.alive) {
      player.respawnTimer = 0.0f;
    }
    log_.push_back("гравець \"" + player.name + "\" просить появу: команда " +
                   std::to_string(player.team) + ", набір " + std::to_string(kit) +
                   ", точка " + std::to_string(spawnGroup));
    return true;
  }
  return false;
}

std::uint32_t GameServer::spawnSoldier(Player& player) {
  WorldObject soldier;
  soldier.id = nextObjectId_++;
  soldier.templateName = settings_.soldierTemplate;
  soldier.position = chooseSpawn(player.team, player.spawnGroup);
  soldier.dynamic = true;
  soldier.ownerPlayerId = player.id;
  objects_.push_back(std::move(soldier));

  player.alive = true;
  log_.push_back("з'явився солдат гравця \"" + player.name + "\" (об'єкт " +
                 std::to_string(objects_.back().id) + ")");
  return objects_.back().id;
}

float GameServer::groundHeightAt(const Vec3f& position) const {
  // Сама вибірка живе в рівні: нею користується і клієнт, коли передбачає
  // рух свого солдата.
  return terrain_ == nullptr ? 0.0f : terrain_->groundHeightAt(position);
}

void GameServer::simulate(float step) {
  ++tickCount_;
  worldTime_ += step;
  if (status_ == GameStatus::Playing) {
    updateControlPoints(step);
    updateTickets(step);
  }

  // Провалився крізь світ — це смерть, інакше гравець падає вічно.
  if (terrain_ != nullptr) {
    const float floorHeight = terrain_->terrain.seaLevel - 100.0f;
    for (Player& player : players_) {
      if (!player.alive) continue;
      const WorldObject* soldier = findObject(player.soldierId);
      if (soldier != nullptr && soldier->position.y < floorHeight) {
        killPlayer(player.id, "провалився за межі світу");
      }
    }
  }

  // Поява після смерті: чекаємо затримку, потім ставимо на точку.
  for (Player& player : players_) {
    if (player.alive || !player.acknowledged) continue;
    player.respawnTimer -= step;
    if (player.respawnTimer <= 0.0f) {
      WorldObject* soldier = findObject(player.soldierId);
      if (soldier != nullptr) {
        soldier->position = chooseSpawn(player.team, player.spawnGroup);
        soldier->velocity = Vec3f{};
        player.alive = true;
        player.health = settings_.soldierMaxHealth;
        log_.push_back("гравець \"" + player.name + "\" з'явився знову");
      }
    }
  }

  for (Player& player : players_) {
    if (!player.acknowledged || player.soldierId == 0) continue;
    WorldObject* soldier = findObject(player.soldierId);
    if (soldier == nullptr) continue;

    // Рух у площині: осі вводу повертаються на кут огляду, тому "вперед"
    // означає туди, куди гравець дивиться.
    constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
    const float yaw = player.input.yaw * kToRadians;
    const float sin = std::sin(yaw);
    const float cos = std::cos(yaw);

    const float sprint = settings_.sprintSpeed > 0.0f ? settings_.sprintSpeed
                                                      : settings_.physics.sprintSpeed;
    const float run = settings_.walkSpeed > 0.0f ? settings_.walkSpeed : settings_.physics.runSpeed;
    const float speed = player.input.sprint ? sprint : run;
    // У BF2 нульовий кут повороту дивиться вздовж **+Z** (це видно з
    // самих даних: у авіаносця носова частина стоїть на більшому Z при
    // нульовому повороті). Вправо в лівій системі — cross(up, forward).
    Vec3f wish{player.input.moveForward * sin + player.input.moveRight * cos, 0.0f,
               player.input.moveForward * cos - player.input.moveRight * sin};

    // Нормуємо, щоб рух по діагоналі не був швидшим за рух прямо.
    const float magnitude = length(wish);
    if (magnitude > 1.0f) wish = wish * (1.0f / magnitude);

    BodyState body;
    body.position = soldier->position;
    body.velocity = soldier->velocity;
    body.onGround = soldier->onGround;

    // Земля — це не тільки рельєф. Рушій шукає опору й на об'єктах, інакше
    // на дах чи сходи не зійти. Беремо вищу з двох.
    const PhysicsConstants& physics = settings_.physics;
    float ground = groundHeightAt(body.position);
    if (collision_ != nullptr) {
      // Починаємо трохи вище ніг, щоб знайти й сходинку перед собою.
      Vec3f from = body.position;
      from.y += physics.stepHeight();
      float surface = 0.0f;
      if (collision_->groundHeight(from, physics.stepHeight() + 2.0f,
                                   physics.feetContactNormal, &surface)) {
        if (surface > ground) ground = surface;
      }
    }

    // Вода. Рушій міряє, наскільки солдат занурений, і з певної частки
    // висоти той спливає (`phy-soldier-start-float`), а назад стає на дно
    // вже з іншої (`stop-float`) — щоб не смикався на межі.
    const float waterLevel = terrain_ != nullptr ? terrain_->terrain.seaLevel : 0.0f;
    const float submersion =
        physics.standHeight > 0.0f ? (waterLevel - body.position.y) / physics.standHeight : 0.0f;
    if (player.swimming) {
      if (submersion <= physics.stopFloat) player.swimming = false;
    } else if (submersion >= physics.startFloat) {
      player.swimming = true;
    }

    if (player.swimming) {
      // Пливемо: тяжіння не діє, солдат тримається біля поверхні, а
      // швидкість своя (`phy-soldier-swim-speed`).
      const float surface = waterLevel - physics.standHeight * physics.startFloat;
      body.position = body.position + wish * (physics.swimSpeed * step);
      body.position.y += (surface - body.position.y) * std::min(1.0f, step * 4.0f);
      body.velocity = Vec3f{};
      body.onGround = false;
    } else {
      stepSoldier(body, wish, speed, player.input.jump, physics, ground, step);
    }

    // Зіткнення зі стінами: солдат у BF2 це стовпчик сфер, а не одна сфера
    // на рівні грудей (SoldierResponsePhysics::getSoldierHeight). Саме
    // тому він може зійти на сходинку: нижче за stepHeight ми не
    // штовхаємо взагалі, а вище перевіряємо кожну сферу.
    if (collision_ != nullptr) {
      const std::vector<float> centers = soldierSphereHeights(physics);
      Vec3f offset{};
      for (const float center : centers) {
        if (center < physics.stepHeight()) continue;
        Vec3f probe = body.position + offset;
        probe.y += center;
        const Vec3f before = probe;
        if (collision_->resolveSphere(probe, physics.radius) > 0) {
          offset.x += probe.x - before.x;
          offset.z += probe.z - before.z;
        }
      }
      if (length(offset) > 1e-4f) {
        body.position.x += offset.x;
        body.position.z += offset.z;
        const Vec3f direction = normalize(offset);
        const float into = dot(body.velocity, direction);
        if (into < 0.0f) body.velocity = body.velocity - direction * into;
      }
    }

    soldier->position = body.position;
    soldier->velocity = body.velocity;
    soldier->onGround = body.onGround;
    soldier->rotation = Vec3f{player.input.yaw, player.input.pitch, 0.0f};
  }
}

void GameServer::broadcastDynamic() {
  std::vector<net::ObjectUpdate> updates;
  for (const WorldObject& object : objects_) {
    if (!object.dynamic) continue;
    net::ObjectUpdate update;
    update.objectId = object.id;
    update.position = object.position;
    update.rotation = object.rotation;
    update.spawn = false;
    updates.push_back(std::move(update));
  }
  if (updates.empty()) return;

  std::vector<std::byte> buffer(kPacketBytes);
  net::BitWriter writer(buffer);
  if (!net::writeObjectUpdates(writer, updates, Vec3f{0.0f, 0.0f, 0.0f})) return;

  for (Player& player : players_) {
    if (!player.worldSent) continue;
    sendTo(player, std::span(buffer).first(writer.byteSize()));
  }
}

void GameServer::tick(float deltaSeconds) {
  for (Player& player : players_) {
    if (player.connection == nullptr) continue;

    while (auto packet = player.connection->receive()) {
      ++packetsReceived_;
      handlePacket(player, *packet);
    }

    // Стан світу йде лише після підтвердження — інакше клієнт отримав би
    // об'єкти ще до того, як дізнався свій id і рівень.
    if (player.acknowledged && !player.worldSent) {
      // З'являємося лише коли гравець обрав місце. Це не наша умова:
      // сервер рушія спавнить рівно тих, у кого
      // `Player::getSpawnGroup() > 0` (`ServerGameLogic::uPlayingSpawning`).
      // `spawnOnJoin` — наш обхід для тестів і безголових запусків, де
      // екрана появи немає.
      if (player.soldierId == 0 && (settings_.spawnOnJoin || player.spawnGroup > 0)) {
        player.soldierId = spawnSoldier(player);
      }
      sendWorld(player);
    }
  }

  // Фіксований крок: скільки б не тривав кадр, симуляція йде рівними
  // тактами. Інакше на повільній машині гравець рухався б інакше.
  const float step = tickInterval();
  accumulator_ += deltaSeconds;

  // Обмеження на випадок довгої паузи: краще відстати, ніж намотати
  // сотні тактів за один кадр.
  constexpr int kMaxStepsPerFrame = 8;
  int steps = 0;
  while (accumulator_ >= step && steps < kMaxStepsPerFrame) {
    simulate(step);
    accumulator_ -= step;
    ++steps;
  }
  if (steps > 0) broadcastDynamic();

  // Прибираємо тих, хто відвалився.
  players_.erase(std::remove_if(players_.begin(), players_.end(),
                                [](const Player& player) {
                                  return player.connection == nullptr ||
                                         !player.connection->connected();
                                }),
                 players_.end());
}

}  // namespace obf2::server
