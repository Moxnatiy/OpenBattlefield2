#pragma once
// The world's state, assembled from the server's packets.
//
// The client learns about the world by three different routes, and all three
// converge here:
//
//   * **events** — who is playing (`CreatePlayerEvent`), which objects were
//     created (`CreateObjectEvent`), who controls what (`EnterVehicleEvent`);
//   * **the controlled-object state** — the compression reference point per packet;
//   * **the ghost stream's records** — where the moving objects are now.
//
// Keeping this in the frame loop makes no sense: there is no window here, no
// time and no input — only packets. So it lives separately and is checked by a
// test on captured traffic (`tests/data/bf2-spawned.bin`) rather than "by eye in the game".
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>

#include "obf2/core/math.h"
#include "obf2/net/bf2_events.h"
#include "obf2/net/ghost_track.h"

namespace obf2::net::bf2 {

// An object the server created while in the game: another player's soldier or a vehicle.
struct RemoteObject {
  Vec3f position;
  Vec3f createdAt;  // the position `CreateObjectEvent` gave it
  // The rotation it gave, when it gave one (bf2_events.h, CreateObject::rotation).
  std::optional<Vec3f> createdRotation;
  std::optional<float> yaw;  // degrees, when a yaw arrived: body yaw plus aim (soldier_state.h)
  // What the object is on the network, from its first full ghost record
  // (bf2_events.h, GhostClass). A soldier is a soldier by class — a jeep with a
  // player in it stays a jeep.
  GhostClass netClass = GhostClass::Unknown;
  // The team of the player who entered it (`EnterVehicleEvent`). Zero when nobody
  // did, or when that event came before we joined and was not repeated.
  int team = 0;
  bool fromGhostStream = false;  // the position is refined by the stream, not only by an event
  // The number `CreateObjectEvent` gave the object's template — creation order in
  // the server's registry (bf2_events.h). Zero until the create event arrives.
  // Stable per template across objects and runs: on the live server every
  // `trestle01_dest` came as 3763 and every barrel as 3741.
  std::uint32_t templateId = 0;

  // The track is a measure of the parsing, not decoration. A soldier stands on
  // the ground, so a constant difference from the terrain means the object's
  // origin is offset, while a growing difference means a parsing error. For a
  // motionless player `travelled` has to stay close to zero.
  Vec3f firstSeen;
  int updates = 0;
  float travelled = 0.0f;    // the largest displacement from the first position
  float aboveGround = 0.0f;  // the sum of the heights above the terrain
  // How many of a soldier's records carried each thing the animation needs: the
  // yaw (0x2), the velocity (0x80), and how many were read to their end. A yaw
  // that never arrives leaves the soldier facing north while he runs, and the
  // movement direction the triggers read is then measured against the wrong axis.
  int soldierRecords = 0;
  int withYaw = 0;
  int withVelocity = 0;
  int complete = 0;
  // The last value of each angle the soldier's records carried (soldier_state.h),
  // kept whether or not the newest record had it: 0x2, 0x4, 0x8, 0x10.
  float lastBodyYaw = 0.0f, lastAimYaw = 0.0f, lastAngle8 = 0.0f, lastPitch = 0.0f;
  // The last pose the records carried (0x20, `Soldier::getPose`): 0 stand,
  // 1 crouch, 2 prone, 3 swim. A full record always has it.
  std::uint32_t lastPose = 0;
  int poseRecords[5] = {0, 0, 0, 0, 0};  // records seen per pose, for the report
  // The weapon he has out (0x1000): the `itemIndex` of the kit's child, -1 until
  // a record carrying it is read. Only read when his inventory size is known.
  int lastWeaponIndex = -1;
  int weaponRecords[12] = {};  // records seen per item index 0..11, for the report
  int withoutInventory = 0;    // records not read to the end while his size was unknown
  int ragdollRecords = 0;      // records with the ragdoll branch (0x8000)
  int ragdollExact = 0;        // of them, read whole and ending exactly at their length
  // The ragdoll's particles from the newest record that carried them, in world
  // space (soldier_state.h); empty while he is alive.
  std::vector<Vec3f> ragdollParticles;
  Vec3f lastVelocity;

  // The last four updates stamped with the server's time, to draw from
  // (ghost_track.h). `position` above is the newest as it arrived.
  GhostTrack track;
};

struct RemotePlayer {
  std::string name;
  int team = 0;
  std::uint16_t object = 0;  // the object the player occupied
  // The player's own networkable (`CreatePlayerEvent`) and what its records
  // said, merged: a record carries only the fields its mask names, and the
  // engine applies only those (0x500570, the tail from 0x500f3a).
  std::uint16_t networkId = 0;
  int stateRecords = 0;
  std::optional<bool> alive;
  std::optional<PlayerState::Score> score;
  std::optional<std::uint32_t> ping;
  std::optional<int> rank;
  std::optional<std::uint32_t> squad;
  std::optional<int> kit;
  std::optional<bool> commander;
  std::optional<bool> sprint;
  std::optional<std::uint32_t> spawnGroup;   // 0x2, `getSpawnGroup`
  std::optional<std::uint32_t> spawnAtTick;  // 0x20
  std::optional<bool> manDown;               // 0x20
};

class WorldView {
 public:
  // One data packet from the server. The order inside matters: the events first
  // (from them we learn which object is a soldier), then the controlled-object
  // state (the reference point), and only then the ghost records.
  void feed(std::span<const std::byte> packet);

  // The name we recognise ourselves by. The server assembles it as "clan tag,
  // space, name", so we compare by the tail.
  void setOwnName(std::string name) { ownName_ = std::move(name); }
  // The connection id `ConnectAccept` gave us. A human player's id is that id:
  // measured on the live server over seven runs (connection 0/2/3 → player
  // 0/2/3), including one where a stale session of ours held the name.
  void setOwnConnection(int connection) { ownConnection_ = connection; }
  void setPlayerSpawned(bool spawned) { playerSpawned_ = spawned; }

  int ownPlayer() const { return ownPlayer_; }
  int ownTeam() const { return ownTeam_; }
  std::uint16_t ownObject() const { return ownObject_; }

  const std::map<std::uint32_t, RemotePlayer>& players() const { return players_; }
  const std::map<std::uint16_t, RemoteObject>& objects() const { return objects_; }
  const Vec3f& compressionReference() const { return compressionReference_; }

  // Every record of one object printed as it is read: the packet's tick, the
  // position, the ground under it, and the compression reference the position
  // was decoded against — and whether this packet moved that reference. The
  // measure for a soldier who jumps up and down between records.
  void setTraceObject(std::uint16_t id) { traceObject_ = id; }

  // The kit's template a soldier picked up: `CreateKitEvent` names the kit and
  // `HandlePickupEvent` the object that took it (bf2_events.h). Zero when none is
  // known — a soldier that spawned before we joined.
  std::uint32_t kitTemplateOf(std::uint16_t object) const {
    const auto found = kitOf_.find(object);
    return found == kitOf_.end() ? 0 : found->second;
  }

  // How many positions arrived from the stream and how many we rejected as unreadable.
  int positionUpdates() const { return positionUpdates_; }
  // The server's spawn groups as the client's spawn manager keeps them: made or
  // updated by `CreateSpawnGroupEvent` (the same number is the same group,
  // `getCreateSpawnGroup`), dropped by `RemoveSpawnGroupEvent`, and changed by
  // the group's own ghost (`SpawnGroupState`: a captured flag's new team, a
  // squad's group, a moving vehicle's position). `spawnGroupRevision` moves on
  // every change, for whoever mirrors the list.
  const std::vector<CreateSpawnGroup>& spawnGroups() const { return spawnGroups_; }
  int spawnGroupRevision() const { return spawnGroupRevision_; }

  // The measure for player_state.h: how many player records came, and how many
  // of them the layout read to exactly their length.
  int playerRecords() const { return playerRecords_; }
  int spawnGroupRecords() const { return spawnGroupRecords_; }
  int spawnGroupRecordsExact() const { return spawnGroupRecordsExact_; }
  int playerRecordsExact() const { return playerRecordsExact_; }
  // The same for the soldiers' records read to their end (soldier_state.h): how
  // many were, and how many of them ended exactly at their length — which is what
  // says the weapon index's width (0x1000) is right, since fields follow it.
  int soldierRecordsComplete() const { return soldierRecordsComplete_; }
  int soldierRecordsExact() const { return soldierRecordsExact_; }
  int soldierRecordsWithWeapon() const { return soldierRecordsWithWeapon_; }  // of the complete

  // The game tick other objects are predicted at — the client's own clock:
  //
  //   * `FUN_005c0460`, the client's tick, calls `FUN_004d5460`, which calls
  //     `predict(gameTime * 1000)` on every active networkable but our controlled
  //     object and its vehicle; the game time (0x9a7420) is the game tick
  //     (0x9a7428) times 1/30 (0x970398) — whole ticks, no frame fraction;
  //   * the tick is set by the prediction component of every controlled-object
  //     state, `FUN_004d4b30`: to the server tick of that packet
  //     (`GhostManager +0x1080`, from the ghost header, 0x5b9ee0) — 0x4c4440 at
  //     0x4d4bc9 — and advanced by one for every action it plays again (0x4c4470
  //     at 0x4d4c15);
  //   * between states the client's tick loop advances it by one per tick.
  //
  // So the client runs ahead of the newest state by the actions not answered yet.
  void setGameTick(std::uint32_t tick) { gameTick_ = tick; tickSet_ = true; }
  void advanceGameTick() { if (tickSet_) ++gameTick_; }
  std::uint32_t gameTick() const { return gameTick_; }
  std::uint32_t newestPacketTick() const { return newestPacketTick_; }
  // Where an object is drawn now: its track at the game time, `fraction` of a
  // tick past the last tick. Blending between ticks by the frame is ours: the
  // original has `BF2FrameInterpolator` (0x89d770's file) for it, not reversed.
  std::optional<GhostPose> poseOf(std::uint16_t id, float fraction = 0.0f) const;
  int rejected() const { return rejected_; }

  // A check on the parsing: a soldier stands on the ground, so a constant
  // difference from the terrain is an offset of the object's origin, while a
  // growing divergence is a parsing error. Whoever calls knows the terrain.
  void setGroundProbe(std::function<float(const Vec3f&)> probe) { ground_ = std::move(probe); }

  // A soldier template's inventory size (`SoldierTemplate::getInventorySize`,
  // the template's `inventorySize`) by its template number, or -1 when not known.
  // It is the width of the weapon index in his state (0x1000, soldier_state.h);
  // without it a soldier's record is read only up to that field.
  void setInventorySize(std::function<int(std::uint32_t templateId)> size) {
    inventorySize_ = std::move(size);
  }

 private:
  GhostClass classOf(std::uint16_t id) const;
  bool looksSane(const Vec3f& at, bool soldier) const;

  std::string ownName_;
  int ownConnection_ = -1;
  int ownPlayer_ = -1;
  int ownTeam_ = 0;
  std::uint16_t ownObject_ = 0;
  bool playerSpawned_ = false;

  std::map<std::uint32_t, RemotePlayer> players_;
  std::map<std::uint16_t, RemoteObject> objects_;
  std::map<std::uint16_t, std::uint32_t> owners_;  // object -> player
  std::map<std::uint16_t, CreateKit> kits_;        // kit network id -> its creation
  std::map<std::uint16_t, std::uint32_t> kitOf_;   // object -> kit template
  Vec3f compressionReference_;
  std::uint16_t traceObject_ = 0;
  std::function<float(const Vec3f&)> ground_;
  std::function<int(std::uint32_t)> inventorySize_;
  int positionUpdates_ = 0;
  int rejected_ = 0;
  std::map<std::uint16_t, std::uint32_t> playerOf_;  // a player's network id -> his index
  std::vector<CreateSpawnGroup> spawnGroups_;
  int spawnGroupRevision_ = 0;
  int spawnGroupRecords_ = 0;
  int spawnGroupRecordsExact_ = 0;
  int soldierRecordsComplete_ = 0;
  int soldierRecordsExact_ = 0;
  int soldierRecordsWithWeapon_ = 0;
  int playerRecords_ = 0;       // player records seen
  int playerRecordsExact_ = 0;  // of them, read to exactly their length
  std::uint32_t gameTick_ = 0;
  std::uint32_t newestPacketTick_ = 0;
  bool tickSet_ = false;
};

}  // namespace obf2::net::bf2
