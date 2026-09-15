#include "obf2/net/bf2_world.h"

#include <algorithm>
#include <cmath>

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
  // 1. The events: who is playing, which objects exist, who controls what.
  for (const Event& event : readEvents(packet)) {
    if (event.player) {
      RemotePlayer& player = players_[event.player->id];
      player.name = event.player->name;
      player.team = static_cast<int>(event.player->team);
      // We recognise ourselves by the name's tail: the server assembles it as
      // "clan tag, space, name", and on a server without ranking the tag is empty.
      const std::string& name = event.player->name;
      if (ownPlayer_ < 0 && !ownName_.empty() && name.size() >= ownName_.size() &&
          name.compare(name.size() - ownName_.size(), ownName_.size(), ownName_) == 0) {
        ownPlayer_ = static_cast<int>(event.player->id);
        ownTeam_ = player.team;
      }
    }
    if (event.object && event.object->position) {
      RemoteObject& object = objects_[event.object->networkId];
      // The position from the create event is the initial one; the stream refines it later.
      if (!object.fromGhostStream) object.position = *event.object->position;
      object.createdAt = *event.object->position;
      object.templateId = event.object->templateId;
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

  // 2. The controlled-object state: the compression reference point for the whole
  //    packet (`BF2.exe` / the Linux server, GhostManager::readControlObjectState,
  //    0x445c30 — the three numbers before the network id).
  if (const auto state = readControlObjectState(packet)) {
    compressionReference_ = state->compressionReference;
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
  for (const GhostRecord& record : readGhostRecords(packet, compressionReference_, known)) {
    // Kind 3 — the object is gone (GhostManager::readData, 0x445820: the branch
    // calls disableObject and removeActiveDescriptor).
    if (record.kind == 3) {
      objects_.erase(record.networkId);
      continue;
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
        if (state.velocity) sample.velocity = *state.velocity;
        if (state.bodyYaw) sample.bodyYaw = *state.bodyYaw + state.aimYaw.value_or(0.0f);
        if (state.pitch) sample.pitch = -*state.pitch;
      }
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

}  // namespace obf2::net::bf2
