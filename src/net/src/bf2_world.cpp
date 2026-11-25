#include "obf2/net/bf2_world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

#include "obf2/net/bf2_protocol.h"

namespace obf2::net::bf2 {

std::optional<GhostPose> WorldView::poseOf(std::uint16_t id, float fraction) const {
  return poseAtTime(id, (static_cast<float>(gameTick_) - 1.0f + fraction) * kGhostTickMs);
}

std::optional<GhostPose> WorldView::poseAtTime(std::uint16_t id, float nowMs) const {
  const auto found = objects_.find(id);
  if (found == objects_.end()) return std::nullopt;
  if (auto pose = found->second.track.poseAt(nowMs)) {
    return pose;
  }
  GhostPose still;
  still.position = found->second.position;
  still.rotation = found->second.rotation;
  still.bodyYaw = found->second.yaw.value_or(0.0f);
  still.stance = found->second.lastPose;
  return still;
}

void WorldView::resetMotion() {
  newestPacketTick_ = 0;
  gameTick_ = 0;
  tickSet_ = false;
  seatAim_.clear();
  weaponSeat_ = 0;
  seatWeaponTick_ = 0;
  seatWeapons_ = {};
  for (auto& [id, player] : players_) {
    player.seatFire = {};
  }
  for (auto& [id, object] : objects_) object.track = {};
}

bool WorldView::feedControlledVehicle(std::uint16_t id, std::uint32_t tick,
                                      const SimpleObjectState& state) {
  auto found = objects_.find(id);
  if (found == objects_.end() || (state.position && !looksSane(*state.position, false))) return false;
  auto& object = found->second;
  const float time = static_cast<float>(tick) * kGhostTickMs;
  if (object.track.newest() && time < object.track.newest()->timeMs) return false;
  if (state.position) object.position = *state.position;
  const bool validRotation = state.rotation && std::abs(state.rotationLength - 1.0f) < 0.01f;
  if (validRotation) {
    object.rotation = state.rotation;
  }
  auto& sample = object.track.push(time, state.position.has_value(), validRotation);
  sample.position = object.position;
  sample.rotation = object.rotation;
  if (state.velocity) sample.velocity = state.velocity;
  object.netClass = GhostClass::SimpleObject;
  object.fromGhostStream = true;
  return true;
}

const WorldView::SeatAim* WorldView::seatAim(std::uint16_t id) const {
  const auto found = seatAim_.find(id);
  return found != seatAim_.end() ? &found->second.back() : nullptr;
}

const WorldView::SeatAim* WorldView::seatAimAt(std::uint16_t id, std::uint32_t tick) const {
  const auto found = seatAim_.find(id);
  if (found == seatAim_.end()) return nullptr;
  const auto& samples = found->second;
  for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
    if (it->tick <= tick) return &*it;
  }
  return &samples.front();
}

void WorldView::eraseSeatAim(std::uint16_t object) {
  const auto aim = seatAim_.find(weaponSeat_);
  if (object == weaponSeat_ || (aim != seatAim_.end() && aim->second.back().root == object)) {
    weaponSeat_ = 0;
    seatWeapons_ = {};
  }
  std::erase_if(seatAim_, [object](const auto& entry) {
    return entry.first == object || entry.second.back().root == object;
  });
}

void WorldView::applySeatRotations(std::uint16_t id, SeatRotationLayout layout, std::uint32_t tick,
                                  const std::vector<SeatRotation>& rotations, bool controlled) {
  auto& samples = seatAim_[id];
  if (!samples.empty() && tick < samples.back().tick) return;
  if (samples.empty()) {
    samples.emplace_back();
  } else if (tick != samples.back().tick) {
    samples.push_back(samples.back());
    if (samples.size() > GhostTrack::kSlots) samples.pop_front();
  }
  auto& aim = samples.back();
  aim.root = layout.root;
  aim.tick = tick;
  aim.controlled = controlled;
  aim.angles.resize(rotations.size());
  aim.normalizedRates.resize(rotations.size());
  aim.excessInputs.resize(rotations.size());
  for (std::size_t i = 0; i < rotations.size(); ++i) {
    const auto& angles = rotations[i];
    auto& current = aim.angles[i];
    current = {angles[0].value_or(current.x), angles[1].value_or(current.y),
               angles[2].value_or(current.z)};
    auto& rate = aim.normalizedRates[i];
    rate = {angles.normalizedRates[0].value_or(rate.x),angles.normalizedRates[1].value_or(rate.y),
            angles.normalizedRates[2].value_or(rate.z)};
    aim.excessInputs[i] = angles.excessInput.value_or(aim.excessInputs[i]);
  }
}

GhostClass WorldView::classOf(std::uint16_t id) const {
  // Not "an object a player entered": players enter jeeps too, and a jeep read
  // with the soldier's layout is rubbish. The class comes from the object's first
  // full record (bf2_events.h).
  if (playerOf_.count(id) != 0) return GhostClass::Player;
  if (controlPoints_.count(id) != 0) return GhostClass::ControlPoint;
  for (const CreateSpawnGroup& group : spawnGroups_) {
    if (group.networkId == id) return GhostClass::SpawnGroup;
  }
  const auto found = objects_.find(id);
  return found == objects_.end() ? GhostClass::Unknown : found->second.netClass;
}

bool WorldView::looksSane(const Vec3f& at, bool soldier) const {
  if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) return false;
  // BF2's maps are no larger than 2048 metres across (`GLSWorldSizeX`, 2048 by
  // default — see spawnGroupWorldPos), so anything outside that is rubbish.
  if (std::abs(at.x) > 1024.0f || std::abs(at.z) > 1024.0f) return false;

  // Roofs, parachutes, and steep banks are legitimate. Keep the lower bound
  // to reject invalid positions underground, and allow the map's airspace.
  if (std::abs(at.y) > 2048.0f) return false;
  return !soldier || !ground_ || at.y - ground_(at) > -5.0f;
}

std::vector<UnitSpot> WorldView::visibleSpots(Clock::time_point now) const {
  std::vector<UnitSpot> out;
  for (const auto& [key, timed] : spots_) {
    if (timed.expires <= now || timed.spot.team != ownTeam_) continue;
    auto spot = timed.spot;
    if (spot.target) {
      const auto* owner = ownerOf(spot.target);
      if (owner && (owner->team == ownTeam_ || owner->manDown.value_or(false) ||
                    (owner->alive && !*owner->alive))) continue;
      if (const auto found = objects_.find(spot.target);
          found != objects_.end() && found->second.fromGhostStream) {
        spot.x = found->second.position.x;
        spot.z = found->second.position.z;
        spot.followsObject = true;
      }
    }
    out.push_back(spot);
  }
  return out;
}

void WorldView::feed(std::span<const std::byte> packet, Clock::time_point now) {
  feedEvents(readEvents(packet), now);
  feedGhosts(packet);
}

const RemotePlayer* WorldView::infantryOwnerOf(std::uint16_t soldier) const {
  const auto link = infantryOwners_.find(soldier);
  if (link == infantryOwners_.end()) return nullptr;
  const auto player = players_.find(link->second);
  return player == players_.end() ? nullptr : &player->second;
}

void WorldView::rememberInfantryOwner(std::uint16_t soldier) {
  const auto owner = owners_.find(soldier);
  if (owner == owners_.end()) return;
  const auto known = infantryOwners_.find(soldier);
  if (known != infantryOwners_.end() && known->second == owner->second) return;
  std::erase_if(infantryOwners_, [&](const auto& link) { return link.second == owner->second; });
  infantryOwners_.insert_or_assign(soldier, owner->second);
}

std::vector<SquadOrder> WorldView::ownOrders() const {
  std::vector<SquadOrder> orders;
  const auto self = players_.find(static_cast<unsigned>(ownPlayer_));
  if (self == players_.end()) return orders;
  const bool commander = self->second.commander.value_or(false);
  for (const auto& order : squadOrders_) {
    if (static_cast<int>(order.team) != ownTeam_) continue;
    if (commander || (order.squad == self->second.squad &&
        (order.route != OrderRoute::Commander || self->second.squadLeader))) {
      orders.push_back(order);
    }
  }
  return orders;
}

std::optional<Vec3f> WorldView::commanderCameraPosition() const {
  for (const auto& [id, object] : objects_) {
    if (object.netClass == GhostClass::OverheadCamera && object.team == ownTeam_ &&
        object.overheadCameraEnabled.value_or(false) && object.fromGhostStream) {
      return object.position;
    }
  }
  return std::nullopt;
}

std::vector<SquadOrder> WorldView::commanderRequests() const {
  std::vector<SquadOrder> requests;
  const auto self = players_.find(static_cast<unsigned>(ownPlayer_));
  if (self == players_.end() || !self->second.commander.value_or(false)) return requests;
  for (const auto& order : squadOrders_) {
    if (order.route == OrderRoute::Request && static_cast<int>(order.team) == ownTeam_) {
      requests.push_back(order);
    }
  }
  return requests;
}

std::optional<Vec3f> WorldView::orderPosition(const SquadOrder& order) const {
  if (order.target) {
    const auto object = objects_.find(order.target);
    if (object == objects_.end() ||
        (!object->second.fromGhostStream && !object->second.hasCreationPosition)) return std::nullopt;
    return object->second.position;
  }
  return looksSane(order.points[0], false) ? std::optional(order.points[0]) : std::nullopt;
}

void WorldView::feedEvents(const std::vector<Event>& events, Clock::time_point now) {
  std::erase_if(spots_, [now](const auto& entry) { return entry.second.expires <= now; });
  const auto releaseControl = [this](std::uint32_t player) {
    if (const auto found = players_.find(player); found != players_.end()) {
      const auto owner = owners_.find(found->second.object);
      if (owner != owners_.end() && owner->second == player) owners_.erase(owner);
      found->second.object = 0;
      found->second.seatFire = {};
    }
    if (ownPlayer_ >= 0 && player == static_cast<std::uint32_t>(ownPlayer_)) {
      ownObject_ = 0;
      weaponSeat_ = 0;
      seatWeapons_ = {};
    }
  };
  // 1. The events: who is playing, which objects exist, who controls what.
  for (const Event& event : events) {
    if (event.remote) {
      const auto& remote = *event.remote;
      if ((remote.category == kGameLogicCategory && remote.number == kGameLogicReset) ||
          (remote.category == kNetworkCategory && remote.number == kNetReset)) {
        kits_.clear();
        kitOf_.clear();
        droppedKits_.clear();
        clearSpots();
        clearSquadTactics();
      }
      if (remote.category == 2 && remote.number == 16 && remote.data.size() == 4 &&
          remote.data[0] >= 1 && remote.data[0] <= 9 && remote.data[1] == 0 &&
          remote.data[2] == 0 && remote.data[3] == 0) {
        squadInvitation_ = remote.data[0];
      }
      if (const auto spot = readUnitSpot(remote); spot && spot->team == ownTeam_) {
        // Bf2MenuMapNode::handleEvent uses a twelve-second timed marker for a spot.
        const auto key = spot->target ? spot->target : 0x10000u + spot->spotter;
        spots_[key] = {*spot, now + std::chrono::seconds(12)};
      }
      if (const auto revived = readRevivedSoldier(remote); revived && !destroyed_.contains(*revived)) {
        if (auto soldier = objects_.find(*revived); soldier != objects_.end()) {
          auto& object = soldier->second;
          object.ragdollParticles.clear();
          object.lastPose = kRevivedSoldierPose;
          object.lastVelocity = {};
          // GameLogic 14 switches the presentation out of the ragdoll at
          // once. Seed interpolation from the revive pose while waiting for
          // living soldier updates (FuShe Pass, server tick 1807).
          if (const auto* newest = object.track.newest()) {
            auto sample = *newest;
            sample.stance = object.lastPose;
            sample.velocity.reset();
            object.track = {};
            object.track.push(sample.timeMs, sample.hasPosition, sample.hasRotation) = sample;
          }
        }
        if (const auto owner = owners_.find(*revived); owner != owners_.end()) {
          if (auto player = players_.find(owner->second); player != players_.end()) {
            player->second.alive = true;
            player->second.manDown = false;
          }
        }
      }
    }
    if (event.squadLeader) {
      const auto& change = *event.squadLeader;
      for (auto& [id, player] : players_) {
        if (player.team == static_cast<int>(change.team) && player.squad == change.squad) {
          player.squadLeader = id == change.player;
        }
      }
    }
    if (event.squadOrder && event.squadOrder->squad >= 1 && event.squadOrder->squad <= 9 &&
        event.squadOrder->team >= 1 && event.squadOrder->team <= 2 &&
        (event.squadOrder->route != OrderRoute::Request || event.squadOrder->id <= 3)) {
      const auto& order = *event.squadOrder;
      std::erase_if(squadOrders_, [&](const auto& old) {
        return old.team == order.team && old.squad == order.squad && old.route == order.route &&
               (order.route != OrderRoute::Request || old.id == order.id);
      });
      squadOrders_.push_back(order);
    }
    if (event.commanderAssignment) {
      const auto [team, commander] = *event.commanderAssignment;
      for (auto& [id, player] : players_) {
        if (player.team != static_cast<int>(team)) continue;
        player.commander = commander >= 0 && id == static_cast<unsigned>(commander);
        if (!*player.commander) {
          player.uavReady = player.scanReady = player.artilleryAvailable = false;
          player.commanderArmor = {};
          player.commanderFlags = {};
        }
      }
    }
    if (event.orderResponse) {
      const auto& response = *event.orderResponse;
      if (!response.accepted && (response.route == OrderRoute::Commander ||
                                 response.route == OrderRoute::Request)) {
        std::erase_if(squadOrders_, [&](const auto& order) {
          return order.team == response.team && order.squad == response.squad &&
                 order.route == response.route &&
                 (response.route != OrderRoute::Request || order.id == response.id);
        });
      }
      for (auto& order : squadOrders_) {
        if (order.team == response.team && order.squad == response.squad &&
            order.route == response.route &&
            (response.route != OrderRoute::Request || order.id == response.id)) {
          order.accepted = response.accepted;
        }
      }
    }
    if (event.rank && event.rank->kind == 1) {
      if (auto player = players_.find(event.rank->player); player != players_.end()) {
        player->second.rank = event.rank->rank;
      }
    }
    if (event.unlocks) {
      if (auto player = players_.find(event.unlocks->player); player != players_.end()) {
        player->second.availableUnlocks = event.unlocks;
      }
    }
    if (event.player) {
      RemotePlayer& player = players_[event.player->id];
      player.name = event.player->name;
      player.aiPlayer = event.player->aiPlayer;
      player.team = static_cast<int>(event.player->team);
      player.networkId = event.player->networkId;
      playerOf_[event.player->networkId] = event.player->id;
      // We are the player whose id is our connection's (setOwnConnection). By name
      // only when that is not known — a recorded capture: a name is not unique,
      // a stale session of ours keeps it and the new one is renamed "OpenBF2_0".
      const std::string& name = event.player->name;
      const bool byConnection =
          ownConnection_ >= 0 && static_cast<int>(event.player->id) == ownConnection_;
      const bool byName = ownConnection_ < 0 && !ownName_.empty() && name.size() >= ownName_.size() &&
                          name.compare(name.size() - ownName_.size(), ownName_.size(), ownName_) == 0;
      if (ownPlayer_ < 0 && (byConnection || byName)) {
        ownPlayer_ = static_cast<int>(event.player->id);
        if (ownTeam_ != player.team) clearSpots();
        ownTeam_ = player.team;
      }
    }
    if (event.object) {
      const auto& created = *event.object;
      // The transform-less form can name several consecutive instances of the
      // same template. Keep their identities while waiting for positions.
      const auto count = std::max(1u, static_cast<unsigned>(created.field8.value_or(1)));
      for (unsigned i = 0; i < count && created.networkId + i <= 65535; ++i) {
        const auto id = static_cast<std::uint16_t>(created.networkId + i);
        destroyed_.erase(id);
        eraseSeatAim(id);
        auto& object = objects_[id];
        object.templateId = created.templateId;
        // A projectile is told by its template's class: its 5-bit mask names
        // nothing on its own (projectile_state.h).
        if (templateNet_) {
          if (const auto net = templateNet_(object.templateId)) {
            if (net->projectile) {
              object.netClass = GhostClass::Projectile;
              ++projectilesCreated_;
            } else if (net->overheadCamera) {
              object.netClass = GhostClass::OverheadCamera;
            } else if (net->soldier) {
              object.netClass = GhostClass::Soldier;
              rememberInfantryOwner(id);
            }
          }
        }

        if (created.position) {
          object.hasCreationPosition = true;
          object.createdAt = *created.position;
          if (!object.fromGhostStream) object.position = *created.position;
        }
        object.createdRotation = created.rotation;
        if (const auto* owner = ownerOf(id)) object.team = owner->team;
      }
    }
    if (event.stickyProjectile && event.stickyProjectile->projectile != 0) {
      const auto& attachment = *event.stickyProjectile;
      if (!destroyed_.contains(attachment.projectile)) {
        objects_[attachment.projectile].attachment = attachment;
      }
    }
    if (event.enter) {
      releaseControl(event.enter->player);
      if (const auto owner = owners_.find(event.enter->object); owner != owners_.end()) {
        releaseControl(owner->second);
      }
      owners_[event.enter->object] = event.enter->player;
      players_[event.enter->player].object = event.enter->object;
      if (auto object = objects_.find(event.enter->object); object != objects_.end()) {
        object->second.team = players_[event.enter->player].team;
        if (object->second.netClass == GhostClass::Soldier) rememberInfantryOwner(event.enter->object);
      }
      if (ownPlayer_ >= 0 && event.enter->player == static_cast<std::uint32_t>(ownPlayer_)) {
        ownObject_ = event.enter->object;
      }
    }
    if (event.spawnGroup) {
      // `getCreateSpawnGroup(id, …, true)` (Linux 0x4ba600): the same number is the
      // same group, updated in place. What only its ghost said (the squad) stays.
      const auto same = std::find_if(spawnGroups_.begin(), spawnGroups_.end(),
                                     [&](const auto& g) { return g.id == event.spawnGroup->id; });
      if (same != spawnGroups_.end()) {
        const int squad = same->squad;
        *same = *event.spawnGroup;
        same->squad = squad;
      } else {
        spawnGroups_.push_back(*event.spawnGroup);
      }
      ++spawnGroupRevision_;
    }
    if (event.destroyObject) {
      // The object is gone for good; our own soldier too (a new life is a new
      // object), unlike a ghost stopped by kind 3.
      destroyed_.insert(*event.destroyObject);
      eraseSeatAim(*event.destroyObject);
      vehicleEngines_.erase(*event.destroyObject);
      objects_.erase(*event.destroyObject);
      infantryOwners_.erase(*event.destroyObject);
      kitOf_.erase(*event.destroyObject);
      droppedKits_.erase(*event.destroyObject);
      kits_.erase(*event.destroyObject);
      if (*event.destroyObject == ownObject_) ownObject_ = 0;
      ++destroyedObjects_;
    }
    if (event.destroyPlayer) {
      const std::uint32_t gone = *event.destroyPlayer;
      releaseControl(gone);
      std::erase_if(infantryOwners_, [gone](const auto& link) { return link.second == gone; });
      players_.erase(gone);
      for (auto it = playerOf_.begin(); it != playerOf_.end();) {
        it = it->second == gone ? playerOf_.erase(it) : std::next(it);
      }
      ++destroyedPlayers_;
    }
    if (event.squadName && event.squadName->team < 3 && event.squadName->squad < 10) {
      squads_[event.squadName->team][event.squadName->squad].name = event.squadName->name;
      ++squadRevision_;
    }
    if (event.squadPrivate && event.squadPrivate->team < 3 && event.squadPrivate->squad < 10) {
      squads_[event.squadPrivate->team][event.squadPrivate->squad].locked = event.squadPrivate->isPrivate;
      ++squadRevision_;
    }
    if (event.removeSpawnGroup) {
      const auto gone = std::remove_if(spawnGroups_.begin(), spawnGroups_.end(), [&](const auto& g) {
        return g.id == *event.removeSpawnGroup;
      });
      if (gone != spawnGroups_.end()) {
        spawnGroups_.erase(gone, spawnGroups_.end());
        ++spawnGroupRevision_;
      }
    }
    if (event.kit) {
      const auto id = event.kit->networkId;
      destroyed_.erase(id);
      kits_[id] = *event.kit;
      droppedKits_[id] = *event.kit;
    }
    if (event.drop) {
      const auto& drop = *event.drop;
      const auto kit = kits_.find(drop.item);
      if (kit != kits_.end()) {
        auto& groundKit = droppedKits_.insert_or_assign(drop.item, kit->second).first->second;
        groundKit.position = drop.position;  // BF2.exe 0x53d618..0x53d620
        if (carriedKit(drop.dropper) == drop.item) kitOf_.erase(drop.dropper);
      }
    }
    if (event.pickup) {
      // The first id is the kit and the second the soldier that took it —
      // `handlePickup(picked up, player, taker)`; measured on the live server: in
      // 21 pickups of 21 the first id was a kit `CreateKitEvent` had named.
      const auto kit = kits_.find(event.pickup->first);
      if (kit != kits_.end()) {
        const auto soldier = event.pickup->second;
        if (carriedKit(soldier) != kit->first) {
          if (auto object = objects_.find(soldier); object != objects_.end()) {
            object->second.lastWeaponIndex = -1;
          }
          kitOf_[soldier] = kit->first;
        }
        droppedKits_.erase(kit->first);
      }
    }
    if (event.exitPlayer) releaseControl(*event.exitPlayer);
  }
}

void WorldView::feedGhosts(std::span<const std::byte> packet) {
  // 2. The controlled-object state: the compression reference point for the whole
  //    packet (`BF2.exe` / the Linux server, GhostManager::readControlObjectState,
  //    0x445c30 — the three numbers before the network id).
  const Vec3f referenceBefore = compressionReference_;
  bool referenceMoved = false;
  const auto control = readControlObjectState(packet);
  if (control) {
    compressionReference_ = control->compressionReference;
    referenceMoved = true;
    // The player sync carries our own spawn tick and man-down flag. In BF2.exe it
    // and the player state's bit 0x20 write the same two fields: the sync player
    // +0xe4 / +0xe8 (0x5018a0's tail), the state +0xd8 / +0xdc of its +0xc base
    // (0x501567, 0x501573). The newest of the two stands.
    if (control->playerSync && ownPlayer_ >= 0) {
      if (auto own = players_.find(static_cast<std::uint32_t>(ownPlayer_)); own != players_.end()) {
        own->second.spawnAtTick = control->playerSync->spawnAtTick;
        own->second.manDown = control->playerSync->manDown;
      }
    }
  }

  // 3. The ghost stream's records: where everything is moving.
  // Every position in the records is packed against the stream's compression
  // vector (`FUN_0062bd60` reads it from `stream+0x54`), which the latest
  // controlled-object state set.
  // The packet's server time: the header's tick, as 0x5b9ee0 turns it into time.
  const auto header = readGhostHeader(packet);
  const float packetMs = header ? static_cast<float>(header->time) * kGhostTickMs : 0.0f;
  if (header && header->time > newestPacketTick_) {
    newestPacketTick_ = header->time;
  }

  if (header && seatRotationLayout_ && control) {
    const auto id = control->networkId;
    const auto layout = !control->isSoldier && control->stateStart && !destroyed_.contains(id)
        ? seatRotationLayout_(id) : std::nullopt;
    std::array<std::optional<SeatWeaponState>, 2> weapons;
    if (layout) {
      if (id == layout->root) {
        const auto object = objects_.find(id);
        if (object != objects_.end() && vehicleArmor_) {
          if (const auto armor = vehicleArmor_(object->second.templateId)) {
            std::optional<TankWeaponState> tank;
            if (const auto rotations = readTankTurretRotations(packet, *control, *armor, &tank, layout->bundles, layout->members)) {
              applySeatRotations(id, *layout, header->time, *rotations, true);
              if (tank) weapons = {tank->cannon, tank->hasCoaxial ? std::optional(tank->coaxial) : std::nullopt};
            }
          }
        }
      } else if (const auto rotations = readSeatRotations(packet, control->stateStart, control->stateEnd,
                                                          layout->bundles, true, &weapons[0])) {
        applySeatRotations(id, *layout, header->time, *rotations, true);
      }
    }
    if (header->time >= seatWeaponTick_) {
      seatWeaponTick_ = header->time;
      if (weapons[0] || weaponSeat_ != id) {
        weaponSeat_ = weapons[0] ? id : 0;
        seatWeapons_ = weapons;
      }
    }
  }
  const auto known = [this](std::uint16_t id) { return classOf(id); };
  const auto inventoryOf = [this](std::uint16_t id) {
    if (!inventorySize_) return -1;
    const auto found = objects_.find(id);
    return found == objects_.end() || found->second.templateId == 0
               ? -1
               : inventorySize_(found->second.templateId);
  };
  const auto armorOf = [this](std::uint16_t id) -> std::optional<std::optional<ProjectileArmor>> {
    const auto found = objects_.find(id);
    if (!templateNet_ || found == objects_.end()) return std::nullopt;
    const auto net = templateNet_(found->second.templateId);
    if (!net) return std::nullopt;
    return net->armor;
  };
  for (const GhostRecord& record :
       readGhostRecords(packet, compressionReference_, known, inventoryOf, armorOf)) {
    if (destroyed_.contains(record.networkId)) continue;
    if (seatRotationLayout_ && record.kind == 1) {
      if (const auto layout = seatRotationLayout_(record.networkId)) {
        if (record.networkId == layout->root) {
          const auto object = objects_.find(record.networkId);
          if (object != objects_.end() && vehicleArmor_) {
            if (const auto armor = vehicleArmor_(object->second.templateId)) {
              std::vector<EngineNetState> engines;
              if (const auto rotations = readTankTurretRotations(packet, record, *armor, layout->bundles,
                                                                 layout->members, &engines)) {
                applySeatRotations(record.networkId, *layout, header->time, *rotations, false);
                if (!engines.empty()) {
                  // setNetUpdate's apply (Linux 0x5ca4a4…0x5ca660).
                  const auto& net = engines.front();
                  auto& engine = vehicleEngines_[record.networkId];
                  engine.tick = header->time;
                  if (net.gear) engine.gear = *net.gear;
                  if (net.rpm) engine.rpm = *net.rpm;
                  if (net.field2e8) engine.field2e8 = *net.field2e8;
                  if (net.field2dc) engine.field2dc = *net.field2dc;
                  if (net.flag200) engine.flag328 = *net.flag200;
                  if (net.load) engine.load = *net.load;
                }
              }
            }
          }
        } else {
          if (const auto rotations = readSeatRotations(packet, record.payloadStart,
                  record.payloadStart + record.payloadBits, layout->bundles, false)) {
            applySeatRotations(record.networkId, *layout, header->time, *rotations, false);
          }
          continue;
        }
      }
    }
    // Kind 3 — the object is gone (GhostManager::readData, 0x445820: the branch
    // calls disableObject and removeActiveDescriptor).
    if (record.kind == 3) {
      // Kind 3 stops the object's ghost (`disableObject`, `removeActiveDescriptor`);
      // it is not the object's end. Our own soldier gets one when his state moves
      // to the controlled-object state, and erasing him here lost his template and
      // his kit — the weapon in hand and the crosshair with them (seen in 1 spawn
      // of 3, whenever the record came after the create event in the same
      // packet). His object stays; his position is ours, predicted.
      if ((record.networkId == ownObject_ || record.networkId == localSoldier_) &&
          objects_.contains(record.networkId)) {
        if (!ownGhostStopReported_) {
          std::printf("  our object %u: ghost stopped by the server (kind 3), kept\n",
                      static_cast<unsigned>(record.networkId));
          ownGhostStopReported_ = true;
        }
        continue;
      }
      // A seat uses the vehicle ghost for its transform. Keep the soldier's
      // appearance when the server stops sending the seated infantry ghost.
      if (const auto* owner = infantryOwnerOf(record.networkId);
          owner && owner->object != 0 && owner->object != record.networkId) {
        continue;
      }
      objects_.erase(record.networkId);
      continue;
    }
    if (record.netClass == GhostClass::OverheadCamera) {
      const auto found = objects_.find(record.networkId);
      if (record.kind != 1 || found == objects_.end()) continue;
      auto& camera = found->second;
      if (record.cameraTeam) camera.team = *record.cameraTeam;
      if (record.cameraEnabled) camera.overheadCameraEnabled = record.cameraEnabled;
      if (record.position && looksSane(*record.position, false)) {
        camera.position = *record.position;
        camera.fromGhostStream = true;
      }
      continue;
    }
    // A control point's ghost: its owner, its flag and the take-over.
    if (record.netClass == GhostClass::ControlPoint) {
      if (record.kind != 1) continue;
      const auto found = controlPoints_.find(record.networkId);
      if (found == controlPoints_.end()) continue;
      LiveControlPoint& point = found->second;
      ++point.records;
      if (record.controlPoint && record.readBits == record.payloadBits) ++point.recordsExact;
      if (!record.controlPoint) continue;
      const ControlPointState& state = *record.controlPoint;
      point.stateKnown = true;
      if (state.team) point.team = *state.team;
      if (state.neutral) point.neutral = *state.neutral;
      if (state.takeOverCounter) point.takeOverCounter = *state.takeOverCounter;
      if (state.takeOverPerSecond) point.takeOverPerSecond = *state.takeOverPerSecond;
      if (state.captureByTeam1) point.captureByTeam1 = *state.captureByTeam1;
      if (state.captureByTeam2) point.captureByTeam2 = *state.captureByTeam2;
      if (state.flagTeam) point.flagTeam = *state.flagTeam;
      if (state.capturingState) point.capturingState = *state.capturingState;
      ++controlPointRevision_;
      continue;
    }
    // The score manager's ghost: the tickets and the rest of the round's score.
    if (record.netClass == GhostClass::ScoreManager) {
      if (record.kind != 1) continue;
      ++scoreRecords_;
      if (record.score && record.readBits == record.payloadBits) ++scoreRecordsExact_;
      if (record.score) score_.apply(*record.score);
      continue;
    }
    // A spawn group's ghost: the group's later changes (bf2_events.h).
    if (record.netClass == GhostClass::SpawnGroup) {
      if (record.kind != 1) continue;
      ++spawnGroupRecords_;
      if (record.spawnGroup && record.readBits == record.payloadBits) ++spawnGroupRecordsExact_;
      if (!record.spawnGroup) continue;
      for (CreateSpawnGroup& group : spawnGroups_) {
        if (group.networkId != record.networkId) continue;
        const SpawnGroupState& state = *record.spawnGroup;
        if ((state.selectable && *state.selectable != group.selectable) ||
            (state.active && *state.active != group.active)) {
          std::printf("  spawn group %u ghost: selectable %d, active %d\n", group.id,
                      state.selectable ? (*state.selectable ? 1 : 0) : (group.selectable ? 1 : 0),
                      state.active ? (*state.active ? 1 : 0) : (group.active ? 1 : 0));
        }
        if (state.selectable) group.selectable = *state.selectable;
        if (state.team) group.team = *state.team;
        if (state.active) group.active = *state.active;
        if (state.aiOnly) group.aiOnly = *state.aiOnly;
        if (state.worldX) group.worldX = *state.worldX;
        if (state.worldZ) group.worldZ = *state.worldZ;
        if (state.squad) group.squad = *state.squad;
        ++spawnGroupRevision_;
      }
      continue;
    }
    // A player's own networkable: not a thing in the world, only his state
    // (player_state.h). Only the fields its mask names are taken.
    if (record.netClass == GhostClass::Player) {
      if (record.kind != 1) continue;
      ++playerRecords_;
      if (record.player && record.readBits == record.payloadBits) ++playerRecordsExact_;
      const auto owner = playerOf_.find(record.networkId);
      if (!record.player || owner == playerOf_.end()) continue;
      RemotePlayer& player = players_[owner->second];
      const PlayerState& state = *record.player;
      ++player.stateRecords;
      if (state.alive) player.alive = state.alive;
      if (state.score) player.score = state.score;
      if (state.ping) player.ping = state.ping;
      if (state.rank) player.rank = state.rank;
      if (state.squad) player.squad = state.squad;
      player.squadLeader = state.squadLeader;
      if (state.squadInviteMask) player.squadInviteMask = state.squadInviteMask;
      if (state.kit) player.kit = state.kit;
      if (state.unlockLevel) player.unlockLevel = state.unlockLevel;
      if (state.freeCamera) player.freeCamera = state.freeCamera;
      if (state.commander) {
        player.commander = state.commander;
        player.uavReady = *state.commander && state.commanderFlags[0];
        player.scanReady = *state.commander && state.commanderFlags[1];
        player.artilleryAvailable = *state.commander && state.commanderFlags[3];
        player.commanderArmor = state.commanderFractions;
        player.commanderFlags = state.commanderFlags;
      }
      if (state.sprint) player.sprint = state.sprint;
      if (state.firing[0]) {
        player.firing = state.firing[0]->on;
        ++player.firingStates;
      }
      const auto seatLayout = (state.firing[0] || state.firing[1]) && seatRotationLayout_
          ? seatRotationLayout_(player.object) : std::nullopt;
      if (seatLayout) {
        for (std::size_t slot = 0; slot < player.seatFire.size(); ++slot) {
          auto& fire = player.seatFire[slot];
          if (!state.firing[slot] || header->time < fire.update) continue;
          fire.update = header->time;
          if (state.firing[slot]->on) {
            if (!fire.start || fire.stop) {
              fire.start = header->time;
              fire.stop = 0;
            }
          } else if (fire.start && !fire.stop) {
            fire.stop = header->time;
          }
        }
      }
      if (state.spawnGroup) player.spawnGroup = state.spawnGroup;
      if (state.spawnAtTick) player.spawnAtTick = state.spawnAtTick;
      if (state.manDown) player.manDown = state.manDown;
      if (state.team && *state.team > 0) {
        player.team = *state.team;
        if (static_cast<int>(owner->second) == ownPlayer_ && ownTeam_ != player.team) {
          ownTeam_ = player.team;
          clearSpots();
          clearSquadTactics();
        }
      }
      continue;
    }
    // A projectile's record: counted, its detonation kept; its position and
    // rotation go on as any object's below.
    if (record.netClass == GhostClass::Projectile && record.kind == 1) {
      ++projectileRecords_;
      if (record.projectile && record.projectile->complete &&
          record.readBits == record.payloadBits) {
        ++projectileRecordsExact_;
      }
      auto found = objects_.find(record.networkId);
      if (record.projectile && record.projectile->complete &&
          record.readBits == record.payloadBits && record.projectile->detonated.value_or(false) &&
          found != objects_.end() && !found->second.detonated) {
        const ProjectileState& state = *record.projectile;
        found->second.detonated = true;
        Detonation detonation;
        detonation.object = record.networkId;
        detonation.templateId = found->second.templateId;
        detonation.position = state.position.value_or(found->second.position);
        detonation.number = state.detonationNumber.value_or(0);
        detonation.vector = state.detonationVector.value_or(Vec3f{});
        detonation.target = state.target;
        // Nobody taking them (a tool) must not grow the list without end.
        if (detonations_.size() >= 256) detonations_.erase(detonations_.begin());
        detonations_.push_back(detonation);
        ++detonationCount_;
      }
    }
    if (record.kind == 1 && record.netClass != GhostClass::Unknown) {
      objects_[record.networkId].netClass = record.netClass;
      if (record.netClass == GhostClass::Soldier) rememberInfantryOwner(record.networkId);
    }
    const bool soldier = record.netClass == GhostClass::Soldier;
    const auto last = objects_.find(record.networkId);
    if (header && last != objects_.end() && last->second.track.newest() &&
        packetMs < last->second.track.newest()->timeMs) continue;
    if (record.position && !looksSane(*record.position, soldier)) {
      ++rejected_;
      continue;
    }

    // Every update of a known object is a slot in its track, even one that
    // changes nothing (ghost_track.h). A soldier record that could not be read
    // to its end is not one.
    const bool readable = record.kind == 1 && record.netClass != GhostClass::Unknown &&
                          (soldier ? record.soldier.has_value() : record.readBits != 0) &&
                          header.has_value();
    const bool validRotation = record.rotation && std::abs(record.rotationLength - 1.0f) < 0.01f;
    if (readable) {
      RemoteObject& object = objects_[record.networkId];
      const bool first = object.track.count() == 0;
      GhostSample& sample = object.track.push(packetMs, record.position.has_value(), validRotation);
      if (first) sample.position = object.position;
      if (record.position) sample.position = *record.position;
      if (validRotation) sample.rotation = record.rotation;
      if (record.velocity) sample.velocity = record.velocity;
      if (record.netClass == GhostClass::SimpleObject && vehicleArmor_ && object.templateId &&
          (record.stateMask & 0x10u)) {
        if (const auto armor = vehicleArmor_(object.templateId)) {
          if (const auto health = readVehicleHealth(packet, record, *armor)) {
            object.vehicleHealth = *health;
          }
        }
      }
      if (soldier) {
        const SoldierState& state = *record.soldier;
        ++object.soldierRecords;
        if (state.bodyYaw) ++object.withYaw;
        if (state.velocity) ++object.withVelocity;
        if (state.complete) {
          ++object.complete;
          ++soldierRecordsComplete_;
          if (record.readBits == record.payloadBits) ++soldierRecordsExact_;
          if (state.weaponIndex) ++soldierRecordsWithWeapon_;
        }
        if (!state.complete && inventoryOf(record.networkId) < 0) ++object.withoutInventory;
        if (state.ragdoll) {
          ++object.ragdollRecords;
          if (state.complete && record.readBits == record.payloadBits) ++object.ragdollExact;
          if (state.complete) object.ragdollParticles = state.ragdollParticles;
        } else if (state.pose) {
          // A record of a living soldier (a full one always has the pose).
          object.ragdollParticles.clear();
        }
        if (state.bodyYaw) object.lastBodyYaw = *state.bodyYaw;
        if (state.aimYaw) object.lastAimYaw = *state.aimYaw;
        if (state.angle8) object.lastAngle8 = *state.angle8;
        if (state.pitch) object.lastPitch = *state.pitch;
        if (state.pose) {
          object.lastPose = *state.pose;
          sample.stance = *state.pose;
          if (*state.pose < 5) ++object.poseRecords[*state.pose];
        }
        if (state.weaponIndex) {
          object.lastWeaponIndex = *state.weaponIndex;
          if (*state.weaponIndex >= 0 && *state.weaponIndex < 12) {
            ++object.weaponRecords[*state.weaponIndex];
          }
        }
        if (state.velocity) object.lastVelocity = *state.velocity;
        if (state.velocity) sample.velocity = *state.velocity;
        sample.bodyYaw = object.lastBodyYaw + object.lastAimYaw;
        if (state.pitch) sample.pitch = -*state.pitch;
        sample.aimYaw = object.lastAimYaw;
      }
    }

    if (traceObject_ != 0 && record.networkId == traceObject_ && record.position) {
      const float ground = ground_ ? ground_(*record.position) : 0.0f;
      std::printf("    trace %u: tick %u, at %.2f %.2f %.2f, above the ground %.2f, reference %.2f %.2f "
                  "%.2f%s\n",
                  record.networkId, header ? header->time : 0u, record.position->x,
                  record.position->y, record.position->z, record.position->y - ground,
                  compressionReference_.x, compressionReference_.y, compressionReference_.z,
                  referenceMoved ? (compressionReference_.x == referenceBefore.x &&
                                            compressionReference_.y == referenceBefore.y &&
                                            compressionReference_.z == referenceBefore.z
                                        ? ", reference set (same)"
                                        : ", REFERENCE MOVED this packet")
                                 : "");
    }
    if (!record.tail.empty() && tailTrace_.count(record.networkId) != 0) {
      std::string bits;
      for (const std::uint8_t bit : record.tail) bits.push_back(bit != 0 ? '1' : '0');
      std::printf("  tail %u: tick %u, kind %u, baseline %d, mask 0x%x, head %u bits, tail %zu "
                  "bits: %s\n",
                  static_cast<unsigned>(record.networkId), header ? header->time : 0u,
                  record.kind, record.baseline ? 1 : 0, record.stateMask, record.readBits,
                  record.tail.size(), bits.c_str());
    }
    if (record.rotation) {
      if (validRotation) objects_[record.networkId].rotation = record.rotation;
      ++rotations_;
      // The measure: a quaternion read from the right place is 1 long.
      if (validRotation) ++rotationsUnit_;
    }
    if (!record.position) continue;
    RemoteObject& object = objects_[record.networkId];
    if (soldier && record.soldier && record.soldier->bodyYaw) {
      object.yaw = *record.soldier->bodyYaw + record.soldier->aimYaw.value_or(0.0f);
    }
    if (object.updates == 0) object.firstSeen = *record.position;
    ++object.updates;
    const Vec3f moved = *record.position - object.firstSeen;
    object.travelled = std::max(
        object.travelled, std::sqrt(moved.x * moved.x + moved.y * moved.y + moved.z * moved.z));
    if (ground_) object.aboveGround += record.position->y - ground_(*record.position);
    object.position = *record.position;
    object.fromGhostStream = true;
    const auto owner = owners_.find(record.networkId);
    if (owner != owners_.end() && players_.count(owner->second)) {
      object.team = players_[owner->second].team;
    }
    ++positionUpdates_;
  }
}

const std::string& WorldView::squadName(int team, int squad) const {
  static const std::string empty;
  if (team < 0 || team > 2 || squad < 0 || squad > 9) return empty;
  return squads_[team][squad].name;
}

bool WorldView::squadPrivate(int team, int squad) const {
  if (team < 0 || team > 2 || squad < 0 || squad > 9) return false;
  return squads_[team][squad].locked;
}

void WorldView::leaveLevel() {
  objects_.clear();
  destroyed_.clear();
  clearSpots();
  clearSquadTactics();
  resetMotion();
  detonations_.clear();
  owners_.clear();
  infantryOwners_.clear();
  kits_.clear();
  kitOf_.clear();
  droppedKits_.clear();
  ownObject_ = 0;
  localSoldier_ = 0;
  playerSpawned_ = false;
  ownGhostStopReported_ = false;
  spawnGroups_.clear();
  ++spawnGroupRevision_;
  score_ = Score{};  // the next map's score manager starts its own
  controlPoints_.clear();
  ++controlPointRevision_;
}

void WorldView::markControlPoint(std::uint16_t id, int levelId, std::string templateName) {
  LiveControlPoint& point = controlPoints_[id];
  point.levelId = levelId;
  point.templateName = std::move(templateName);
  ++controlPointRevision_;
}

}  // namespace obf2::net::bf2
