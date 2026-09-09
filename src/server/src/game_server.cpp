#include "obf2/server/game_server.h"

#include "obf2/server/soldier_move.h"

#include <algorithm>
#include <cmath>

namespace obf2::server {
namespace {

// The buffer size for one packet. World updates are cut into batches so they fit
// into it.
constexpr std::size_t kPacketBytes = 1200;

// How many objects go into one spawn packet.
//
// One spawn costs ~77 bytes: 64 for the template's name, the rest for the id,
// the compressed position and the angles. About fifteen fit into 1200 bytes (a
// typical safe UDP packet size), so we take twelve with room to spare.
constexpr std::size_t kSpawnsPerPacket = 12;

}  // namespace

void GameServer::loadWorld(const level::Level& level, std::size_t limit) {
  objects_.clear();
  for (const level::StaticObject& object : level.objects) {
    if (limit != 0 && objects_.size() >= limit) break;
    // Vegetation does not travel over the network: in the original the client
    // draws it from the level's data, and the server needs it only for collision.
    if (object.isOvergrowth) continue;
    WorldObject world;
    world.id = nextObjectId_++;
    world.templateName = object.templateName;
    world.position = object.position;
    world.rotation = object.rotation;
    objects_.push_back(std::move(world));
  }
  log_.push_back("world loaded: " + std::to_string(objects_.size()) + " objects from level " +
                 level.name);
  // The statics were just overwritten — the spawners' vehicles have to be placed again.
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
      // The checks come before any state change: first we decide whether to let them in.
      if (request->protocolVersion != net::kProtocolVersion) {
        net::writeConnectionDenied(writer, net::DenyReason::WrongVersion);
        log_.push_back("denied: version " + std::to_string(request->protocolVersion));
      } else if (static_cast<int>(players_.size()) > settings_.maxPlayers) {
        net::writeConnectionDenied(writer, net::DenyReason::ServerFull);
        log_.push_back("denied: the server is full");
      } else {
        player.name = request->playerName;
        net::ConnectionAccept accept;
        accept.playerId = player.id;
        accept.levelName = settings_.levelName;
        accept.gameMode = settings_.gameMode;
        net::writeConnectionAccept(writer, accept);
        log_.push_back("connected \"" + player.name + "\" (id " + std::to_string(player.id) + ")");
      }
      sendTo(player, std::span(buffer).first(writer.byteSize()));
      return;
    }

    case net::PacketType::ConnectionAcknowledge:
      player.acknowledged = true;
      log_.push_back("acknowledgement from \"" + player.name + "\"");
      return;

    case net::PacketType::Data: {
      // From a client the Data type means input (subtype 1 in the header).
      if (header->subtype != 1) return;
      const auto input = net::readPlayerInput(reader);
      if (!input) return;

      // The sequence number is 16-bit and wraps; we take that into account,
      // otherwise after tick 65535 the player would be stuck for good.
      const std::uint32_t previous = player.lastSequence;
      const std::uint32_t delta = (input->sequence - previous) & 0xFFFFu;
      if (previous != 0 && (delta == 0 || delta > 0x8000u)) return;  // a stale packet

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
      log_.push_back("disconnected \"" + player.name + "\"");
      return;

    default:
      return;
  }
}

void GameServer::sendWorld(Player& player) {
  // The compression origin is the coordinate origin: the level is centred on it,
  // so the differences stay within the map.
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
  log_.push_back("world sent to player \"" + player.name + "\"");
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

    // The flag has to be visible, so a control point also travels to the client
    // as an ordinary object — the original does the same: in its world stream
    // these templates arrive as `CreateObjectEvent`s alongside the vehicles.
    // The template itself has no geometry, it is in a child (`addTemplate
    // flagpole`), and object assembly takes that into account.
    if (!point.templateName.empty()) {
      WorldObject flag;
      flag.id = nextObjectId_++;
      flag.templateName = point.templateName;
      flag.position = point.position;
      objects_.push_back(std::move(flag));
    }
  }

  // The tickets are set up at the round's start — just as gpm_cq.py does in
  // response to the transition into the Playing state.
  for (int team = 1; team <= 2; ++team) {
    teams_[team] = TeamState{};
    teams_[team].tickets =
        static_cast<int>(settings_.defaultTickets[team] * (settings_.ticketRatio / 100.0f));
  }
  status_ = GameStatus::Playing;
  winner_ = 0;
  updateTicketLoss();
  spawnVehicles();
  log_.push_back("mode logic: " + std::to_string(controlPoints_.size()) +
                 " control points, " + std::to_string(gameplay_.spawners.size()) +
                 " spawners");
}

bool GameServer::spawnPointActive(const level::SpawnPoint& spawn, int team,
                                  bool forHuman) const {
  // The order of the checks is as in the engine's `SpawnPoint::getActive`.
  if (!spawn.active) return false;
  if (forHuman ? spawn.onlyForAI : spawn.onlyForHuman) return false;

  // A point bound to a flag only works while the flag is ours.
  if (spawn.controlPointId != 0) {
    const ControlPointState* point = nullptr;
    for (const ControlPointState& candidate : controlPoints_) {
      if (candidate.id == spawn.controlPointId) { point = &candidate; break; }
    }
    if (point == nullptr || point->team != team) return false;
  }

  // Occupied: somebody has just spawned here. The delay is zero by default, so
  // in practice this only kicks in where the level set one.
  if (spawn.spawnPreventionDelay > 0.0f) {
    const auto found = lastSpawnTime_.find(&spawn);
    if (found != lastSpawnTime_.end() &&
        worldTime_ < found->second + spawn.spawnPreventionDelay) {
      return false;
    }
  }

  // Too high above the ground is no good either (disabled by default, -1).
  if (spawn.minSpawnHeight >= 0.0f) {
    const float ground = groundHeightAt(spawn.position);
    if (spawn.position.y - ground < spawn.minSpawnHeight) return false;
  }

  // And the main one: nobody is standing nearby. The engine looks for objects within 1 m.
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
  // First every suitable one is collected, then a random one taken — exactly as
  // `SpawnGroup::getSpawnPoint` does it, rather than in a circle.
  std::vector<const level::SpawnPoint*> usable;
  for (const level::SpawnPoint& spawn : gameplay_.spawnPoints) {
    // The player picked a flag — we take only its group. In the engine the
    // choice goes by group too: the player sends its number, and the server
    // picks a point within it (docs/functions/spawn.md).
    if (spawnGroup != 0 && spawn.controlPointId != spawnGroup) continue;
    if (spawnPointActive(spawn, team, forHuman)) usable.push_back(&spawn);
  }
  // The chosen group has no suitable point — we take any of ours, otherwise the
  // player would be stuck on the spawn screen forever.
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

  // There are no spawn points — we simply stand on the flag.
  for (const ControlPointState& point : controlPoints_) {
    if (point.team == team) return point.position;
  }
  return settings_.spawnPosition;
}

void GameServer::spawnVehicles() {
  // Remove what already stands from the spawners: a point's owner may have changed.
  objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                [](const WorldObject& object) { return object.spawnerIndex >= 0; }),
                 objects_.end());

  int placed = 0;
  for (std::size_t i = 0; i < gameplay_.spawners.size(); ++i) {
    const level::ObjectSpawner& spawner = gameplay_.spawners[i];

    // Which vehicle to issue is decided by the team owning the bound point.
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
  log_.push_back("vehicles from spawners: " + std::to_string(placed) + " of " +
                 std::to_string(gameplay_.spawners.size()));
}

void GameServer::refreshTakeOver(ControlPointState& point) {
  // The point cannot be taken at all — the flag stands.
  if (point.unableToChangeTeam) {
    point.takeOverChangePerSecond = 0.0f;
    return;
  }

  const int overweight = point.occupantsTeam1 - point.occupantsTeam2;
  const int attackingTeam = overweight > 0 ? 1 : (overweight < 0 ? 2 : 0);

  float attackOverWeight = 0.0f;
  float timeToChange = point.timeToLoseControl;

  if (point.occupantsTeam1 == 0 && point.occupantsTeam2 == 0) {
    // Nobody is there: a neutral point slowly lowers its flag, while somebody's
    // raises its own back just as slowly.
    attackOverWeight = point.team == 0 ? -0.5f : 0.5f;
  } else if (point.flagTeam == attackingTeam ||
             (point.flagPosition == FlagPosition::Bottom && point.team == 0)) {
    // The flag is already ours (or the pole is empty) — we raise it.
    attackOverWeight = static_cast<float>(std::abs(overweight));
    timeToChange = point.timeToGetControl;
  } else {
    // Somebody else's flag is on the pole: it has to be lowered first.
    attackOverWeight = -static_cast<float>(std::abs(overweight));
  }

  if (point.onlyTakeableByTeam != 0 && point.onlyTakeableByTeam != attackingTeam) return;

  // The flag's owner can only change at the bottom.
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
    // Somebody else's flag was lowered — the point becomes nobody's.
    if (!top) newTeam = 0;
  } else if (top) {
    newTeam = point.flagTeam;
  }
  if (newTeam < 0) return;

  // A capture takes tickets off the enemy at once.
  if (newTeam > 0 && point.enemyTicketLossWhenCaptured > 0) {
    const int punished = newTeam == 1 ? 2 : 1;
    teams_[punished].tickets -= point.enemyTicketLossWhenCaptured;
  }

  if (point.team != newTeam) {
    point.team = newTeam;
    log_.push_back(newTeam == 0
                       ? "point " + std::to_string(point.id) + " neutralised"
                       : "point " + std::to_string(point.id) + " captured by team " +
                             std::to_string(newTeam));
  }
  updateTicketLoss();
  spawnVehicles();
}

void GameServer::updateControlPoints(float step) {
  for (ControlPointState& point : controlPoints_) {
    // How many alive of each team stand within the radius. The original counts
    // this on entry and exit through triggers; we do it every tick, with the same result.
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
      // The radius is horizontal: height must not get in the way of a capture.
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

    // A death costs the team one ticket — a rule of the mode, not of the engine.
    if (player.team == 1 || player.team == 2) --teams_[player.team].tickets;

    log_.push_back("player \"" + player.name + "\" died: " + std::string(reason));
    // The last one alive in a team with no points — the final bleed kicks in.
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

  // A team with no points and nobody alive bleeds almost instantly.
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

  // The ordinary bleed: the team with the smaller total area weight bleeds, and
  // only when the enemy has gathered at least 100.
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
  log_.push_back("round over, team " + std::to_string(winner) + " won");
}

bool GameServer::requestSpawn(std::uint32_t playerId, int team, int kit, int spawnGroup) {
  for (Player& player : players_) {
    if (player.id != playerId) continue;
    player.team = team == 2 ? 2 : 1;
    player.kit = kit;
    // Zero here would mean "has not chosen yet", while the player has already
    // pressed DONE. So "any point of ours" stays as a zero in `spawnGroup`, but
    // the fact of the choice is marked by the soldier spawning on the next tick.
    player.spawnGroup = spawnGroup;
    if (player.soldierId == 0) {
      player.soldierId = spawnSoldier(player);
    } else if (!player.alive) {
      player.respawnTimer = 0.0f;
    }
    log_.push_back("player \"" + player.name + "\" asks to spawn: team " +
                   std::to_string(player.team) + ", kit " + std::to_string(kit) +
                   ", point " + std::to_string(spawnGroup));
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
  log_.push_back("player \"" + player.name + "\"'s soldier spawned (object " +
                 std::to_string(objects_.back().id) + ")");
  return objects_.back().id;
}

float GameServer::groundHeightAt(const Vec3f& position) const {
  // The sampling itself lives in the level: the client uses it too, when it
  // predicts its own soldier's movement.
  return terrain_ == nullptr ? 0.0f : terrain_->groundHeightAt(position);
}

void GameServer::simulate(float step) {
  ++tickCount_;
  worldTime_ += step;
  if (status_ == GameStatus::Playing) {
    updateControlPoints(step);
    updateTickets(step);
  }

  // Fell through the world — that is death, otherwise the player falls forever.
  if (terrain_ != nullptr) {
    const float floorHeight = terrain_->terrain.seaLevel - 100.0f;
    for (Player& player : players_) {
      if (!player.alive) continue;
      const WorldObject* soldier = findObject(player.soldierId);
      if (soldier != nullptr && soldier->position.y < floorHeight) {
        killPlayer(player.id, "fell out of the world");
      }
    }
  }

  // Spawning after a death: we wait out the delay, then place them at a point.
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
        log_.push_back("player \"" + player.name + "\" spawned again");
      }
    }
  }

  for (Player& player : players_) {
    if (!player.acknowledged || player.soldierId == 0) continue;
    WorldObject* soldier = findObject(player.soldierId);
    if (soldier == nullptr) continue;

    // Movement in the plane: the input's axes are rotated by the look angle, so
    // "forward" means where the player is looking.
    constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
    const float yaw = player.input.yaw * kToRadians;
    const float sin = std::sin(yaw);
    const float cos = std::cos(yaw);

    const float sprint = settings_.sprintSpeed > 0.0f ? settings_.sprintSpeed
                                                      : settings_.physics.sprintSpeed;
    const float run = settings_.walkSpeed > 0.0f ? settings_.walkSpeed : settings_.physics.runSpeed;
    const float speed = player.input.sprint ? sprint : run;
    // In BF2 a zero rotation angle looks along **+Z** (visible from the data
    // itself: a carrier's bow stands at the greater Z at zero rotation).
    // Right in a left-handed system is cross(up, forward).
    Vec3f wish{player.input.moveForward * sin + player.input.moveRight * cos, 0.0f,
               player.input.moveForward * cos - player.input.moveRight * sin};

    // Normalised so that moving diagonally is not faster than moving straight.
    const float magnitude = length(wish);
    if (magnitude > 1.0f) wish = wish * (1.0f / magnitude);

    BodyState body;
    body.position = soldier->position;
    body.velocity = soldier->velocity;
    body.onGround = soldier->onGround;

    // The movement itself goes through the shared function: the client does the
    // same when predicting its own soldier (`obf2/server/soldier_move.h`).
    SwimState swim{player.swimming};
    moveSoldier(body, swim, wish, speed, player.input.jump, settings_.physics, terrain_,
                collision_.get(), step);
    player.swimming = swim.swimming;

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

    // World state goes out only after the acknowledgement — otherwise the client
    // would receive objects before it learned its id and the level.
    if (player.acknowledged && !player.worldSent) {
      // We spawn only once the player has chosen a place. That is not our
      // condition: the engine's server spawns exactly those whose
      // `Player::getSpawnGroup() > 0` (`ServerGameLogic::uPlayingSpawning`).
      // `spawnOnJoin` is our workaround for tests and headless runs, where there
      // is no spawn screen.
      if (player.soldierId == 0 && (settings_.spawnOnJoin || player.spawnGroup > 0)) {
        player.soldierId = spawnSoldier(player);
      }
      sendWorld(player);
    }
  }

  // A fixed step: however long a frame lasts, the simulation runs in equal
  // ticks. Otherwise a player would move differently on a slow machine.
  const float step = tickInterval();
  accumulator_ += deltaSeconds;

  // A limit for a long pause: better to fall behind than to wind up hundreds of
  // ticks in one frame.
  constexpr int kMaxStepsPerFrame = 8;
  int steps = 0;
  while (accumulator_ >= step && steps < kMaxStepsPerFrame) {
    simulate(step);
    accumulator_ -= step;
    ++steps;
  }
  if (steps > 0) broadcastDynamic();

  // Remove those who dropped out.
  players_.erase(std::remove_if(players_.begin(), players_.end(),
                                [](const Player& player) {
                                  return player.connection == nullptr ||
                                         !player.connection->connected();
                                }),
                 players_.end());
}

}  // namespace obf2::server
