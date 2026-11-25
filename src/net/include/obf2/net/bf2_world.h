#pragma once
#include <chrono>
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
#include <array>
#include <cstdint>
#include <deque>
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
  std::optional<StickyProjectile> attachment;
  Vec3f position;
  Vec3f createdAt;  // the position `CreateObjectEvent` gave it
  bool hasCreationPosition = false;
  // The rotation it gave, when it gave one (bf2_events.h, CreateObject::rotation).
  std::optional<Vec3f> createdRotation;
  std::optional<float> yaw;  // degrees, when a yaw arrived: body yaw plus aim (soldier_state.h)
  // A simple object's rotation from its record (x, y, z, w), the newest.
  std::optional<std::array<float, 4>> rotation;
  std::optional<int> vehicleHealth;
  // What the object is on the network, from its first full ghost record
  // (bf2_events.h, GhostClass). A soldier is a soldier by class — a jeep with a
  // player in it stays a jeep.
  GhostClass netClass = GhostClass::Unknown;
  // The team of the player who entered it (`EnterVehicleEvent`). Zero when nobody
  // did, or when that event came before we joined and was not repeated.
  int team = 0;
  bool fromGhostStream = false;  // the position is refined by the stream, not only by an event
  bool detonated = false;
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

  // Recent updates stamped with the server's time, to draw from
  // (ghost_track.h). `position` above is the newest as it arrived.
  GhostTrack track;
  std::optional<bool> overheadCameraEnabled;
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
  bool squadLeader = false;
  std::optional<std::uint32_t> squadInviteMask;  // 0x800000: the squads that invited him (OpenBF2 Player.cpp)
  bool aiPlayer = false;
  std::optional<bool> freeCamera;
  std::optional<int> unlockLevel;
  std::optional<Event::Unlocks> availableUnlocks;
  bool uavReady = false;
  bool scanReady = false;
  bool artilleryAvailable = false;
  std::array<float, 3> commanderArmor{};
  std::array<bool, 5> commanderFlags{};
  std::optional<bool> sprint;
  // The firing state, 0x40 (`setFiringState`, Linux 0x340): on while the player
  // holds his weapon's trigger — the newest the server said. How the client turns
  // it into rounds is not read.
  std::optional<bool> firing;
  int firingStates = 0;  // records that carried it, for the report
  struct SeatFire {
    std::uint32_t start = 0;
    std::uint32_t stop = 0;
    std::uint32_t update = 0;
  };
  std::array<SeatFire, 2> seatFire;
  std::optional<std::uint32_t> spawnGroup;   // 0x2, `getSpawnGroup`
  std::optional<std::uint32_t> spawnAtTick;  // 0x20
  std::optional<bool> manDown;               // 0x20
};

class WorldView {
 public:
  // One data packet from the server. The order inside matters: the events first
  // (from them we learn which object is a soldier), then the controlled-object
  // state (the reference point), and only then the ghost records.
  using Clock = std::chrono::steady_clock;
  void feed(std::span<const std::byte> packet, Clock::time_point now = Clock::now());
  std::vector<UnitSpot> visibleSpots(Clock::time_point now = Clock::now()) const;
  void clearSpots() { spots_.clear(); }
  unsigned squadInvitation() const { return squadInvitation_; }
  void dismissSquadInvitation() { squadInvitation_ = 0; }
  std::vector<SquadOrder> ownOrders() const;
  std::vector<SquadOrder> commanderRequests() const;
  std::optional<Vec3f> commanderCameraPosition() const;
  std::optional<Vec3f> orderPosition(const SquadOrder& order) const;
  void clearSquadTactics() { squadOrders_.clear(); squadInvitation_ = 0; }
  void resetMotion();
  bool feedControlledVehicle(std::uint16_t id, std::uint32_t tick, const SimpleObjectState& state);
  struct SeatRotationLayout {
    std::uint16_t root;
    unsigned bundles;
    // The root seat's decodable descriptor members (rotational bundles, an
    // engine, springs); empty reads `bundles` rotational bundles only.
    std::vector<DescriptorMember> members;
  };
  struct SeatAim {
    std::uint16_t root = 0;
    std::uint32_t tick = 0;
    std::vector<Vec3f> angles;
    std::vector<Vec3f> normalizedRates;
    std::vector<float> excessInputs;
    bool controlled = false;
  };
  void setSeatRotationLayout(std::function<std::optional<SeatRotationLayout>(std::uint16_t)> layout) {
    seatRotationLayout_ = std::move(layout);
  }
  // A vehicle's EngineBase fields as its ghost's EngineNetworkable set them
  // (setNetUpdate, Linux 0x5ca4b8…: each only when its mask bit came).
  // The first engine of the root group; nullptr before one arrives.
  struct VehicleEngine {
    std::uint32_t tick = 0;
    unsigned gear = 1;      // +0x2fc (EngineBase constructor 0x63098d: 1)
    float rpm = 0;          // +0x2d8
    float load = 0;         // +0x2f8
    float field2e8 = 0;     // +0x2e8, purpose not established
    float field2dc = 0;     // +0x2dc, purpose not established
    float flag328 = 0;      // +0x328 from mask 0x200, purpose not established
  };
  const VehicleEngine* vehicleEngine(std::uint16_t id) const {
    const auto found = vehicleEngines_.find(id);
    return found == vehicleEngines_.end() ? nullptr : &found->second;
  }
  const SeatAim* seatAim(std::uint16_t id) const;
  const SeatAim* seatAimAt(std::uint16_t id, std::uint32_t tick) const;
  const SeatWeaponState* controlledSeatWeapon(std::uint16_t id, std::size_t slot = 0) const {
    if (id != weaponSeat_ || slot >= seatWeapons_.size()) return nullptr;
    const auto& weapon = seatWeapons_[slot];
    return weapon ? &*weapon : nullptr;
  }
  bool explicitlyDestroyed(std::uint16_t id) const { return destroyed_.contains(id); }
  // The two halves of `feed`, for a caller that orders the events itself (the
  // live session: the server's blocks in their order, `Link::receiveBlock`).
  // Events before ghosts, as `feed` does.
  void feedEvents(const std::vector<Event>& events, Clock::time_point now = Clock::now());
  void feedGhosts(std::span<const std::byte> packet);

  // The name we recognise ourselves by. The server assembles it as "clan tag,
  // space, name", so we compare by the tail.
  void setOwnName(std::string name) { ownName_ = std::move(name); }
  // The connection id `ConnectAccept` gave us. A human player's id is that id:
  // measured on the live server over seven runs (connection 0/2/3 → player
  // 0/2/3), including one where a stale session of ours held the name.
  void setOwnConnection(int connection) { ownConnection_ = connection; }
  void setPlayerSpawned(bool spawned) { playerSpawned_ = spawned; }
  void retainLocalSoldier(std::uint16_t id) { localSoldier_ = id; }

  int ownPlayer() const { return ownPlayer_; }
  struct SquadDetails {
    std::string name;
    bool locked = false;
    bool operator==(const SquadDetails&) const = default;
  };
  const auto& squads() const { return squads_; }
  int ownTeam() const { return ownTeam_; }
  std::optional<int> tickets(int team) const {
    if (!score_.known || team < 1 || team > 2) return std::nullopt;
    return score_.teams[team - 1].tickets;
  }

  std::uint16_t ownObject() const { return ownObject_; }

  const std::map<std::uint32_t, RemotePlayer>& players() const { return players_; }
  // The player currently controlling this object (`EnterVehicleEvent`), if any.
  const RemotePlayer* ownerOf(std::uint16_t object) const {
    const auto found = owners_.find(object);
    if (found == owners_.end()) return nullptr;
    const auto player = players_.find(found->second);
    return player == players_.end() ? nullptr : &player->second;
  }
  // The player's infantry object remains theirs while they control a vehicle seat.
  const RemotePlayer* infantryOwnerOf(std::uint16_t soldier) const;
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
  std::uint16_t carriedKit(std::uint16_t object) const {
    const auto found = kitOf_.find(object);
    return found == kitOf_.end() ? 0 : found->second;
  }
  std::uint32_t kitTemplateOf(std::uint16_t object) const {
    const auto kit = kits_.find(carriedKit(object));
    return kit == kits_.end() ? 0 : kit->second.templateId;
  }
  const std::map<std::uint16_t, CreateKit>& droppedKits() const { return droppedKits_; }

  // How many positions arrived from the stream and how many we rejected as unreadable.
  int positionUpdates() const { return positionUpdates_; }
  // Simple objects' rotations read, and how many of them were 1 long as read.
  int rotations() const { return rotations_; }
  int rotationsUnit() const { return rotationsUnit_; }
  // `--trace-tails <id>`: print the bits after the head of every record of that
  // simple object (bf2_events.h, GhostRecord::tail).
  void traceTails(std::uint16_t id) { tailTrace_.insert(id); }
  // The server's spawn groups as the client's spawn manager keeps them: made or
  // updated by `CreateSpawnGroupEvent` (the same number is the same group,
  // `getCreateSpawnGroup`), dropped by `RemoveSpawnGroupEvent`, and changed by
  // the group's own ghost (`SpawnGroupState`: a captured flag's new team, a
  // squad's group, a moving vehicle's position). `spawnGroupRevision` moves on
  // every change, for whoever mirrors the list.
  // The squads' names and locks, by team (1, 2) and squad (1..9): what
  // `ChangeSquadNameEvent` and `SetPrivateSquadEvent` said; empty is the default
  // name (Alpha..India, OpenBF2 SquadManager.cpp `kSquadNames`). Who is in a squad
  // is each player's own state (0x2000 and the leader bit), not kept here.
  const std::string& squadName(int team, int squad) const;
  bool squadPrivate(int team, int squad) const;
  int squadRevision() const { return squadRevision_; }
  // How many objects and players the server's destroy events took away.
  int destroyedObjects() const { return destroyedObjects_; }
  int destroyedPlayers() const { return destroyedPlayers_; }
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
  std::optional<GhostPose> poseAtTime(std::uint16_t id, float nowMs) const;
  int rejected() const { return rejected_; }

  // A check on the parsing: a soldier stands on the ground, so a constant
  // difference from the terrain is an offset of the object's origin, while a
  // growing divergence is a parsing error. Whoever calls knows the terrain.
  void setGroundProbe(std::function<float(const Vec3f&)> probe) { ground_ = std::move(probe); }

  // A soldier template's inventory size (`SoldierTemplate::getInventorySize`,
  // the template's `inventorySize`) by its template number, or -1 when not known.
  // It is the width of the weapon index in his state (0x1000, soldier_state.h);
  // without it a soldier's record is read only up to that field.
  void setVehicleArmor(std::function<std::optional<VehicleArmor>(std::uint32_t)> armor) {
    vehicleArmor_ = std::move(armor);
  }
  void setInventorySize(std::function<int(std::uint32_t templateId)> size) {
    inventorySize_ = std::move(size);
  }

  // What a template is on the network, by its number: whether its objects are
  // projectiles (`GenericProjectile` — their ghosts are projectile_state.h's) and,
  // for one, its armor (nothing when it has none). Unknown templates: nothing.
  struct TemplateNet {
    bool projectile = false;
    std::optional<ProjectileArmor> armor;
    bool soldier = false;
    bool overheadCamera = false;
  };
  void setTemplateNet(std::function<std::optional<TemplateNet>(std::uint32_t templateId)> net) {
    templateNet_ = std::move(net);
  }

  // A projectile's detonation as its record said it (projectile_state.h), in the
  // order they came; whoever draws takes them and clears the list.
  struct Detonation {
    std::uint16_t object = 0;
    std::uint32_t templateId = 0;
    Vec3f position;
    // The surface material and the surface normal at the hit (Linux
    // `DefaultDetonationComp::detonate` 0x61e0a0, docs/functions/projectiles.md).
    int number = 0;     // the projectile's +0x1c4
    Vec3f vector;       // its +0x1cc
    std::optional<std::uint16_t> target;
  };
  std::vector<Detonation>& detonations() { return detonations_; }
  int projectileRecords() const { return projectileRecords_; }
  int projectileRecordsExact() const { return projectileRecordsExact_; }
  int projectilesCreated() const { return projectilesCreated_; }
  int detonationCount() const { return detonationCount_; }

  // The server moved to another map (a new `MapInfo`, block 2): the client
  // unloads its level before loading the next (`GameClient::handleDataBlock`
  // case 2 → `load`), and every object of the old one goes with it — the
  // objects, their owners and kits, the spawn groups, our own soldier. The
  // players and their squads are the connection's and stay.
  void leaveLevel();

  // The round's score from the score manager's ghost (score_state.h): tickets,
  // their bleed, rounds won. `known` is false until its first record.
  const Score& score() const { return score_; }

  // A control point as the client keeps it: the level's point the object is, and
  // the fields of its ghost (control_point_state.h) as they last came.
  struct LiveControlPoint {
    int levelId = 0;           // the level's `controlPointId`
    std::string templateName;  // the level's template
    bool stateKnown = false;   // a record has come
    int team = 0;
    bool neutral = false;
    float takeOverCounter = 0.0f;
    float takeOverPerSecond = 0.0f;
    bool captureByTeam1 = false;
    bool captureByTeam2 = false;
    int flagTeam = 0;
    int capturingState = 0;
    int records = 0, recordsExact = 0;
  };
  // Name an object as one of the level's control points; its ghost records are
  // then read as `ControlPointNetworkable`'s.
  void markControlPoint(std::uint16_t id, int levelId, std::string templateName);
  const std::map<std::uint16_t, LiveControlPoint>& controlPoints() const { return controlPoints_; }
  int controlPointRevision() const { return controlPointRevision_; }
  int scoreRecords() const { return scoreRecords_; }
  int scoreRecordsExact() const { return scoreRecordsExact_; }

 private:
  GhostClass classOf(std::uint16_t id) const;

  bool looksSane(const Vec3f& at, bool soldier) const;

  std::string ownName_;
  int ownConnection_ = -1;
  int ownPlayer_ = -1;
  int ownTeam_ = 0;
  unsigned squadInvitation_ = 0;
  std::vector<SquadOrder> squadOrders_;
  struct TimedSpot {
    UnitSpot spot;
    Clock::time_point expires;
  };
  // Objects are keyed by their ID. A position-only report is keyed by its spotter.
  std::map<std::uint32_t, TimedSpot> spots_;
  std::set<std::uint16_t> destroyed_;
  std::array<std::array<SquadDetails, 10>, 3> squads_{};
  std::uint16_t ownObject_ = 0;
  std::uint16_t localSoldier_ = 0;
  bool playerSpawned_ = false;

  std::map<std::uint32_t, RemotePlayer> players_;
  void applySeatRotations(std::uint16_t id, SeatRotationLayout layout, std::uint32_t tick,
                          const std::vector<SeatRotation>& rotations, bool controlled);
  void eraseSeatAim(std::uint16_t object);
  std::function<std::optional<SeatRotationLayout>(std::uint16_t)> seatRotationLayout_;
  std::map<std::uint16_t, std::deque<SeatAim>> seatAim_;
  std::map<std::uint16_t, VehicleEngine> vehicleEngines_;
  std::uint16_t weaponSeat_ = 0;
  std::uint32_t seatWeaponTick_ = 0;
  std::array<std::optional<SeatWeaponState>, 2> seatWeapons_;
  std::map<std::uint16_t, RemoteObject> objects_;
  std::map<std::uint16_t, std::uint32_t> owners_;  // object -> player
  std::map<std::uint16_t, std::uint32_t> infantryOwners_;
  void rememberInfantryOwner(std::uint16_t soldier);
  std::map<std::uint16_t, CreateKit> kits_;        // kit network id -> its creation
  std::map<std::uint16_t, std::uint16_t> kitOf_;   // soldier -> kit network id
  std::map<std::uint16_t, CreateKit> droppedKits_;
  Vec3f compressionReference_;
  std::uint16_t traceObject_ = 0;
  std::function<float(const Vec3f&)> ground_;
  std::function<int(std::uint32_t)> inventorySize_;
  std::function<std::optional<TemplateNet>(std::uint32_t)> templateNet_;
  std::vector<Detonation> detonations_;
  int projectileRecords_ = 0;
  int projectileRecordsExact_ = 0;
  int projectilesCreated_ = 0;
  int detonationCount_ = 0;
  int positionUpdates_ = 0;
  bool ownGhostStopReported_ = false;
  int rotations_ = 0;
  int rotationsUnit_ = 0;
  std::set<std::uint16_t> tailTrace_;
  int rejected_ = 0;
  std::map<std::uint16_t, std::uint32_t> playerOf_;  // a player's network id -> his index
  int destroyedObjects_ = 0;
  int destroyedPlayers_ = 0;
  int squadRevision_ = 0;
  std::vector<CreateSpawnGroup> spawnGroups_;
  Score score_;
  std::map<std::uint16_t, LiveControlPoint> controlPoints_;
  int controlPointRevision_ = 0;
  int scoreRecords_ = 0;
  int scoreRecordsExact_ = 0;
  int spawnGroupRevision_ = 0;
  int spawnGroupRecords_ = 0;
  int spawnGroupRecordsExact_ = 0;
  int soldierRecordsComplete_ = 0;
  int soldierRecordsExact_ = 0;
  int soldierRecordsWithWeapon_ = 0;
  int playerRecords_ = 0;       // player records seen
  int playerRecordsExact_ = 0;  // of them, read to exactly their length
  std::uint32_t gameTick_ = 0;
  std::function<std::optional<VehicleArmor>(std::uint32_t)> vehicleArmor_;
  std::uint32_t newestPacketTick_ = 0;
  bool tickSet_ = false;
};

}  // namespace obf2::net::bf2
