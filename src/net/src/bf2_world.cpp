#include "obf2/net/bf2_world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace obf2::net::bf2 {

std::optional<GhostPose> WorldView::poseOf(std::uint16_t id, float fraction) const {
  const auto found = objects_.find(id);
  if (found == objects_.end()) return std::nullopt;
  const float nowMs = (static_cast<float>(gameTick_) - 1.0f + fraction) * kGhostTickMs;
  if (auto pose = found->second.track.poseAt(nowMs)) return pose;
  GhostPose still;
  still.position = found->second.position;
  still.bodyYaw = found->second.yaw.value_or(0.0f);
  return still;
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

  // The ground check is done **for a soldier only**: he stands on it. Vehicles
  // and property are found on roofs and cranes too — dozens of metres above the
  // ground is ordinary there, and rejecting them would be a mistake.
  if (!soldier || !ground_) return true;
  const float above = at.y - ground_(at);
  return above > -5.0f && above < 5.0f;
}

void WorldView::feed(std::span<const std::byte> packet) {
  feedEvents(readEvents(packet));
  feedGhosts(packet);
}

void WorldView::feedEvents(const std::vector<Event>& events) {
  // 1. The events: who is playing, which objects exist, who controls what.
  for (const Event& event : events) {
    if (event.player) {
      RemotePlayer& player = players_[event.player->id];
      player.name = event.player->name;
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
        ownTeam_ = player.team;
      }
    }
    if (event.object && event.object->position) {
      RemoteObject& object = objects_[event.object->networkId];
      // The position from the create event is the initial one; the stream refines it later.
      if (!object.fromGhostStream) object.position = *event.object->position;
      object.createdAt = *event.object->position;
      object.createdRotation = event.object->rotation;
      object.templateId = event.object->templateId;
      // A projectile is told by its template's class: its 5-bit mask names
      // nothing on its own (projectile_state.h).
      if (templateNet_) {
        if (const auto net = templateNet_(object.templateId); net && net->projectile) {
          object.netClass = GhostClass::Projectile;
          ++projectilesCreated_;
        }
      }
    }
    if (event.enter) {
      owners_[event.enter->object] = event.enter->player;
      players_[event.enter->player].object = event.enter->object;
      objects_[event.enter->object].team =
          players_.count(event.enter->player) ? players_[event.enter->player].team : 0;
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
      objects_.erase(*event.destroyObject);
      if (*event.destroyObject == ownObject_) ownObject_ = 0;
      ++destroyedObjects_;
    }
    if (event.destroyPlayer) {
      const std::uint32_t gone = *event.destroyPlayer;
      players_.erase(gone);
      for (auto it = playerOf_.begin(); it != playerOf_.end();) {
        it = it->second == gone ? playerOf_.erase(it) : std::next(it);
      }
      ++destroyedPlayers_;
    }
    if (event.squadName && event.squadName->team < 3 && event.squadName->squad < 10) {
      squadNames_[event.squadName->team][event.squadName->squad] = event.squadName->name;
      ++squadRevision_;
    }
    if (event.squadPrivate && event.squadPrivate->team < 3 && event.squadPrivate->squad < 10) {
      squadPrivate_[event.squadPrivate->team][event.squadPrivate->squad] =
          event.squadPrivate->isPrivate;
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
    if (event.kit) kits_[event.kit->networkId] = *event.kit;
    if (event.pickup) {
      // The first id is the kit and the second the soldier that took it —
      // `handlePickup(picked up, player, taker)`; measured on the live server: in
      // 21 pickups of 21 the first id was a kit `CreateKitEvent` had named.
      const auto kit = kits_.find(event.pickup->first);
      if (kit != kits_.end()) kitOf_[event.pickup->second] = kit->second.templateId;
    }
    if (event.exitPlayer) {
      const auto found = players_.find(*event.exitPlayer);
      if (found != players_.end()) {
        owners_.erase(found->second.object);
        found->second.object = 0;
      }
      if (ownPlayer_ >= 0 && *event.exitPlayer == static_cast<std::uint32_t>(ownPlayer_)) {
        ownObject_ = 0;
      }
    }
  }
}

void WorldView::feedGhosts(std::span<const std::byte> packet) {
  // 2. The controlled-object state: the compression reference point for the whole
  //    packet (`BF2.exe` / the Linux server, GhostManager::readControlObjectState,
  //    0x445c30 — the three numbers before the network id).
  const Vec3f referenceBefore = compressionReference_;
  bool referenceMoved = false;
  if (const auto state = readControlObjectState(packet)) {
    compressionReference_ = state->compressionReference;
    referenceMoved = true;
  }

  // 3. The ghost stream's records: where everything is moving.
  // Every position in the records is packed against the stream's compression
  // vector (`FUN_0062bd60` reads it from `stream+0x54`), which the latest
  // controlled-object state set.
  // The packet's server time: the header's tick, as 0x5b9ee0 turns it into time.
  const auto header = readGhostHeader(packet);
  const float packetMs = header ? static_cast<float>(header->time) * kGhostTickMs : 0.0f;
  if (header) newestPacketTick_ = std::max(newestPacketTick_, header->time);

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
    // Kind 3 — the object is gone (GhostManager::readData, 0x445820: the branch
    // calls disableObject and removeActiveDescriptor).
    if (record.kind == 3) {
      // Kind 3 stops the object's ghost (`disableObject`, `removeActiveDescriptor`);
      // it is not the object's end. Our own soldier gets one when his state moves
      // to the controlled-object state, and erasing him here lost his template and
      // his kit — the weapon in hand and the crosshair with them (seen in 1 spawn
      // of 3, whenever the record came after the create event in the same
      // packet). His object stays; his position is ours, predicted.
      if (record.networkId == ownObject_ && objects_.count(record.networkId) != 0) {
        if (!ownGhostStopReported_) {
          std::printf("  our object %u: ghost stopped by the server (kind 3), kept\n",
                      static_cast<unsigned>(record.networkId));
          ownGhostStopReported_ = true;
        }
        continue;
      }
      objects_.erase(record.networkId);
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
      if (state.commander) player.commander = state.commander;
      if (state.sprint) player.sprint = state.sprint;
      if (state.firing[0]) {
        player.firing = state.firing[0]->on;
        ++player.firingStates;
      }
      if (state.spawnGroup) player.spawnGroup = state.spawnGroup;
      if (state.spawnAtTick) player.spawnAtTick = state.spawnAtTick;
      if (state.manDown) player.manDown = state.manDown;
      if (state.team && *state.team > 0) player.team = *state.team;
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
      if (record.projectile && record.projectile->detonated.value_or(false)) {
        const ProjectileState& state = *record.projectile;
        const auto found = objects_.find(record.networkId);
        Detonation detonation;
        detonation.object = record.networkId;
        detonation.templateId = found != objects_.end() ? found->second.templateId : 0;
        detonation.position = state.position.value_or(
            found != objects_.end() ? found->second.position : Vec3f{});
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
    }
    const bool soldier = record.netClass == GhostClass::Soldier;
    if (record.position && !looksSane(*record.position, soldier)) {
      ++rejected_;
      continue;
    }

    // Every update of a known object is a slot in its track, even one that
    // changes nothing (ghost_track.h). A soldier record that could not be read
    // to its end is not one.
    const bool readable = record.kind == 1 && record.netClass != GhostClass::Unknown &&
                          (!soldier || record.soldier.has_value()) && header.has_value();
    if (readable) {
      RemoteObject& object = objects_[record.networkId];
      const bool first = object.track.count() == 0;
      GhostSample& sample = object.track.push(packetMs);
      if (first) sample.position = object.position;
      if (record.position) sample.position = *record.position;
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
        if (state.bodyYaw) sample.bodyYaw = *state.bodyYaw + state.aimYaw.value_or(0.0f);
        if (state.pitch) sample.pitch = -*state.pitch;
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
      objects_[record.networkId].rotation = record.rotation;
      ++rotations_;
      // The measure: a quaternion read from the right place is 1 long.
      if (std::abs(record.rotationLength - 1.0f) < 0.01f) ++rotationsUnit_;
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
  return squadNames_[team][squad];
}

bool WorldView::squadPrivate(int team, int squad) const {
  if (team < 0 || team > 2 || squad < 0 || squad > 9) return false;
  return squadPrivate_[team][squad];
}

void WorldView::leaveLevel() {
  objects_.clear();
  detonations_.clear();
  owners_.clear();
  kits_.clear();
  kitOf_.clear();
  ownObject_ = 0;
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
