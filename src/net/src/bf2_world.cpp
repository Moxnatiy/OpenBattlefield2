#include "obf2/net/bf2_world.h"

#include <algorithm>
#include <cmath>

namespace obf2::net::bf2 {

bool WorldView::isSoldier(std::uint16_t id) const {
  // A soldier is an object a player occupied. The server says so itself with the
  // `EnterVehicleEvent` (type 9), so there is no guesswork here.
  return owners_.find(id) != owners_.end();
}

Vec3f WorldView::referenceFor(std::uint16_t id) const {
  // A soldier's position is packed relative to the **stream's compression
  // vector**: `BF2.exe`, 0x62bd60 reads the vector from the field `stream+0x54`,
  // where the controlled-object state put it.
  if (isSoldier(id)) return compressionReference_;
  const auto found = objects_.find(id);
  if (found != objects_.end()) return found->second.position;
  return compressionReference_;
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
  const auto soldier = [this](std::uint16_t id) { return isSoldier(id); };
  const auto reference = [this](std::uint16_t id) { return referenceFor(id); };
  for (const GhostRecord& record : readGhostRecords(packet, reference, soldier)) {
    // Kind 3 — the object is gone (GhostManager::readData, 0x445820: the branch
    // calls disableObject and removeActiveDescriptor).
    if (record.kind == 3) {
      objects_.erase(record.networkId);
      continue;
    }
    if (!record.position) continue;
    if (!looksSane(*record.position, isSoldier(record.networkId))) {
      ++rejected_;
      continue;
    }
    RemoteObject& object = objects_[record.networkId];
    if (object.updates == 0) object.firstSeen = *record.position;
    ++object.updates;
    const Vec3f moved = *record.position - object.firstSeen;
    object.travelled = std::max(
        object.travelled, std::sqrt(moved.x * moved.x + moved.y * moved.y + moved.z * moved.z));
    if (ground_) object.aboveGround += record.position->y - ground_(*record.position);
    object.position = *record.position;
    object.fromGhostStream = true;
    if (record.yaw) object.yaw = *record.yaw;
    const auto owner = owners_.find(record.networkId);
    if (owner != owners_.end() && players_.count(owner->second)) {
      object.team = players_[owner->second].team;
    }
    ++positionUpdates_;
  }
}

}  // namespace obf2::net::bf2
