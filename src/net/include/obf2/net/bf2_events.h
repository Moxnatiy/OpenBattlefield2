#pragma once
// BF2's game events: the size table and the parsing of what we understand.
//
// The table in `bf2_events.inc` is generated from the server's binary
// (`tools/linuxded/gen_events.py`): the type number comes from `getType()` and
// the field sizes from `deSerialize`. It can be re-taken with the same command.
//
// The main thing it exists for is being able to **skip** any event by exactly as
// many bits as it occupies. A packet carries them one after another, and if we
// stumble on an unfamiliar one everything after it is rubbish: an event's type is
// never greater than 69, while a shift produces 80 or 106.
//
// Some events cannot be skipped by the table: their fields sit behind conditions
// and the length depends on the contents. Those are marked separately, and their
// parsing is written by hand.
// Every event type the server sends is walked to its length here; which of them
// are also decoded and acted on is docs/todo/network.md, §4–§7.
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/net/bitstream.h"
#include "obf2/net/player_state.h"
#include "obf2/net/control_point_state.h"
#include "obf2/net/projectile_state.h"
#include "obf2/net/score_state.h"
#include "obf2/net/soldier_state.h"

namespace obf2::net::bf2 {

// An event's name by number; empty when the number is not in the registry.
std::string_view eventName(std::uint32_t type);

// Whether an event's length depends on its contents.
bool eventIsBranchy(std::uint32_t type);

// A world object from `CreateObjectEvent` (type 6).
//
// The layout from `bitfields.py --blocks CreateObjectEvent::deSerialize`: a flag
// after the first three fields separates two **mutually exclusive** branches, and
// the polarity is visible in the code (`cmpl $0x1; jne`).
struct CreateObject {
  // The template's number is the order of its creation, not a hash of its name:
  // `ObjectTemplateManager::createTemplate` does `setId(counter)` and then
  // `counter++`. So to match a number to a name the same files have to be read in
  // the same order as the game does. Verified: between runs the numbers match
  // completely (27 of 27).
  std::uint32_t templateId = 0;
  std::uint16_t networkId = 0;
  std::uint32_t field2 = 0;
  std::optional<std::uint8_t> field8;
  std::optional<Vec3f> position;
  // ZXY Euler angles in degrees — the same form as in the `.con`
  // (`getRotationZXY` is right beside it in `clientSendDatabase`, and the three
  // numbers there also flip sign).
  std::optional<Vec3f> rotation;
};

// A player from `CreatePlayerEvent` (type 5). The server sends the name in 32 bytes.
// `CreatePlayerEvent` (Linux constructor 0x423a90, `serialize` 0x423df0): team
// (`getTeam`) 3 bits, spawn group (`getSpawnGroup`) 4 bits, the constructor's
// second argument 1 bit, index (`getId`) 8 bits, the network id 16 bits,
// 16 bits always 0, `getIsAIPlayer` 1 bit, the name in 32 bytes.
// `executeClient` (0x423fa0) registers the player's own networkable under
// `networkId` — its ghost records are player_state.h.
struct CreatePlayer {
  std::uint32_t team = 0;
  std::uint32_t spawnGroup = 0;
  std::uint32_t id = 0;
  std::uint16_t networkId = 0;
  bool aiPlayer = false;
  std::string name;
};

// A chunk of a data block from `DataBlockEvent` (type 4). A block travels as two
// kinds of event: first a header with the type and the full size, then the chunks.
struct DataBlockPiece {
  bool header = false;
  std::uint32_t blockType = 0;  // in the header only
  std::uint32_t size = 0;       // in the header only
  std::vector<std::byte> chunk;
};

// The server's map rotation — block type 5, sent with the server info right
// after authentication (`GameServer::authOk`; OpenBF2 MapList.cpp,
// `generateDataBlock` / `updateFromDataBlock`): a u16 count, a u16 index of the
// map being played, then per entry a u16-length name, a u16-length game mode and
// a u16 size, all little-endian. It is **not** the level the client loads — that
// is block 2 (`GameClient::handleDataBlock` case 2). We once took this block's
// first entry for the level, which is wrong whenever the rotation has more than
// one map.
struct MapListEntry {
  std::string levelName;
  std::string gameMode;
  int size = 0;
};
struct MapList {
  int current = 0;
  std::vector<MapListEntry> entries;
  // The entry the server is playing, or null when the index is out of range.
  const MapListEntry* currentEntry() const {
    return current >= 0 && static_cast<std::size_t>(current) < entries.size() ? &entries[current]
                                                                               : nullptr;
  }
};

inline constexpr std::uint32_t kMapListBlock = 5;

std::optional<MapList> parseMapList(std::span<const std::byte> block);

// Block type 2 is the **real** `MapInfo`, the one assembled by
// `MapInfo::updateNetBuffer` (0x4168b0). Block 5 beside it is simpler and carries
// only the level's name, the mode and the size.
//
// The field order is taken straight from `updateNetBuffer`; the numbers there are
// written not as a whole word but as a sign and 31 bits:
//
//   u16 length + string   the game mode
//   u16 length + string   the path to the levels
//   u16 length + string   the level's name
//   1 sign bit + 31 bits  how many slots
//   1 bit                 whether there is a commander
//   1 sign bit + 31 bits  **the challenge number**
//
// The challenge number is needed for the content check: the server picks it at
// random when it loads the level (`GameServer::loadPath`, `rand() % 10`) and
// compares our hashes against exactly that line of the fingerprint files.
inline constexpr std::uint32_t kMapInfoNetBuffer = 2;

struct ServerMapInfo {
  std::string gameMode;
  std::string levelPath;
  std::string levelName;
  int maxPlayers = 0;
  bool commanderEnabled = false;
  int challengeOrdinal = -1;
};

std::optional<ServerMapInfo> parseMapInfoNetBuffer(std::span<const std::byte> block);

// Block 0, the server's `ServerInfo` (OpenBF2 GameServer/ServerInfo.cpp,
// `updateFromDataBlock`): its head — the game's name and the welcome lines, each
// a 16-bit length and the bytes — then fixed fields we do not read yet.
inline constexpr std::uint32_t kServerInfoBlock = 0;
struct ServerInfoHead {
  std::string gameName;
  std::vector<std::string> welcome;
  // The fields after them, in the block's order: seven flags, the voice server's
  // address and two ports, the voice quality (sign 1 + 31), the GameSpy port,
  // three radio spam numbers (signed), then these two (`ServerInfoBlock` +0x32,
  // +0x33). Read as far as `votingEnabled`; the rest is not taken yet.
  std::optional<bool> voipEnabled;
  std::optional<bool> contentChecking;
  std::optional<bool> votingEnabled;
};
std::optional<ServerInfoHead> parseServerInfo(std::span<const std::byte> block);

// A spawn group from `CreateSpawnGroupEvent` (type 57). It is its number that
// the `NESelectSpawnGroup` event expects when the player presses DONE.
//
// The layout is taken from `CreateSpawnGroupEvent::deSerialize` (0x424520 in the
// Linux server) and cross-checked against the constructor, whose signature
// survived in the symbols:
//
//   CreateSpawnGroupEvent(unsigned char, unsigned short, int,
//                         bool, bool, bool, unsigned char, unsigned char)
//
// The wire order differs from the constructor's — it is visible from the sequence
// of reads and the offsets they land in:
//
//   8 bits  -> +0x10  u8   the first argument
//   4 bits  -> +0x14  int  the third, from `group->[0x38]()`
//   1 bit   -> +0x18  bool from `group->[0x58](0)`
//   1 bit   -> +0x19  bool the group's field 0x9a
//   1 bit   -> +0x1a  bool the group's field 0xa0
//   8 bits  -> +0x1b  u8   \ together these are `SpawnGroup::getUnsignedWorldPosition`
//   8 bits  -> +0x1c  u8   /  — the group's position packed into two bytes
//   16 bits -> +0x1e  u16  the second argument, the group's field 0x10
//
// Who creates it is visible too: `SpawnManager::createSpawnGroupOnClients`
// (0x4b96e0).
struct CreateSpawnGroup {
  // The group's number as the server calls it. It is exactly what
  // `NESelectSpawnGroup` expects — verified with captured traffic of the original
  // client: it sends `NESelectSpawnGroup = 2` for the second flag, not 516 (the
  // network id) and not 402 (the level's point number).
  std::uint8_t id = 0;         // 8 bits
  std::uint32_t team = 0;      // 4 bits
  // The three bits, in the order `CreateSpawnGroupEvent::serialize` (Linux
  // 0x424670) writes them and `createClientSpawnGroup` (0x4ba600) takes them:
  bool active = false;      // `SpawnGroup::setActive` (vtable 0x50)
  bool aiOnly = false;      // +0x9a: only a bot may have it (`getGroupsForPlayer` 0x4b8790)
  bool selectable = false;  // +0xa0: listed for players at all (the same function)
  std::uint8_t worldX = 0;     // the packed position, axis 1
  std::uint8_t worldZ = 0;     // the packed position, axis 2
  // The group's network id — the object system knows it by this. For choosing a
  // spawn point it is **not** needed: the original does not send it. Its ghost
  // records (`SpawnGroupState` below) carry the group's later changes.
  std::uint16_t networkId = 0;  // 16 bits
  // Not in the event: the squad the group belongs to, -1 for none, which only the
  // group's ghost carries (bit 0x20). A squad's group is its squad's alone
  // (`getGroupsForPlayer`, Linux 0x4b8790).
  int squad = -1;
};

// A spawn group's ghost record — `SpawnGroup::setNetUpdate` (Linux 0x4b9ab0). A
// 6-bit mask, then in this order:
//   0x10  1 bit    selectable (+0xa0)
//   0x2   4 bits   the team (`setTeam`)
//   0x4   1 bit    active (`setActive`)
//   0x8   1 bit    bots only (+0x9a)
//   0x1   8, 8     the position, packed as the event's
//   0x20  8 bits   the squad, stored minus one (`setSquad`, vtable 0x40)
struct SpawnGroupState {
  std::uint32_t mask = 0;
  std::optional<bool> selectable;
  std::optional<std::uint32_t> team;
  std::optional<bool> active;
  std::optional<bool> aiOnly;
  std::optional<std::uint8_t> worldX, worldZ;
  std::optional<int> squad;
};
inline constexpr unsigned kSpawnGroupMaskBits = 6;
std::optional<SpawnGroupState> readSpawnGroupState(BitReader& reader);

// Unpacking a group's position. It is packed by `SpawnGroup::getUnsignedWorldPosition`
// (0x4b94b0), and the arithmetic there is:
//
//   half = GLSWorldSizeX / 2                 (1024 by default, the level sets
//                                             its own)
//   pos >  half  -> 255
//   pos < -half  -> 0
//   otherwise  byte = (int)((pos + half) / (2*half) * 255)
//
// The multiplier 255 sits as a constant at 0xb355bc. So in reverse:
inline float spawnGroupWorldPos(std::uint8_t packed, float worldSize) {
  return static_cast<float>(packed) / 255.0f * worldSize - worldSize * 0.5f;
}

// The id of the spawn group nearest to a given position.
//
// This is what a player presses DONE with: on our screen there is a circle next
// to a flag, and the server has to be told **its** group id. We match by
// position, because there is no other common attribute: the control point ids
// from the level's data (401..404 on Dalian) and the server's group ids
// (515..518) are not related in any way.
//
// An exact match will not happen and should not: a group's position is the
// average of its spawn points, so it is dozens of metres from the flag.
// `distance` (when needed) gives the distance in metres — it shows whether the
// match is meaningful at all.
//
// Zero means "not found": it is exactly what the server understands as "no place
// chosen" (`Player::getSpawnGroup() > 0`).
// team is our team. Other teams' groups are skipped: the server spawns a player
// only in their own, and a request for another team's simply does nothing — which
// is exactly why spawning sometimes "did not work".
//
// Only the level's own groups count: `SpawnManager::createDynamicSpawnGroup`
// (Linux server 0x4ba5a0) numbers the groups it makes at run time from 192 (0xc0)
// to 255, taking the first free one, so a flag's group is always below that.
inline constexpr std::uint8_t kDynamicSpawnGroupFirst = 192;
std::uint8_t nearestSpawnGroup(const std::vector<CreateSpawnGroup>& groups, float worldX,
                               float worldZ, float worldSize, float* distance = nullptr,
                               int team = 0);

// One event from a packet: the type number and what of it we already parse.
// Who controls what (`EnterVehicleEvent`, type 9) and who left
// (`ExitVehicleEvent`, type 10).
//
// A soldier in BF2 is a controlled object too, and the player "occupies" it just
// as they do a vehicle. It is by this event that the client learns which object
// is its own: in the captured traffic the server answers a spawn with one packet
// — `CreateObjectEvent` (id 1794), immediately followed by
// `EnterVehicleEvent: player 1 -> object 1794`, then the kit and
// `NEPlayerSpawned = 1`. Our own player number we know from
// `CreatePlayerEvent`, so the match is unambiguous.
struct EnterVehicle {
  std::uint32_t player = 0;
  std::uint16_t object = 0;
  bool flag = false;
};

// A kit object (`CreateKitEvent`, type 31; `deSerialize` Linux server 0x422b30):
// the template's number (32 bits, +0x10), the network id (16, +0x14), the
// position (three raw floats, +0x18), then two four-bit values (+0x24, and +0x28
// stored minus one) whose purpose is not established.
struct CreateKit {
  std::uint32_t templateId = 0;
  std::uint16_t networkId = 0;
  Vec3f position;
  std::uint32_t value24 = 0;
  std::int32_t value28 = 0;
};

// A pickup (`HandlePickupEvent`, type 14; `deSerialize` 0x428a60, `executeClient`
// 0x428860): a player (8 bits) and two network ids (16 each). The client hands
// them to `GameLogic::handlePickup(object +0x12, player, object +0x14, true)`.
// Measured on the live server: the spawn reply carries it right after the
// soldier's `CreateObjectEvent` and the kit's `CreateKitEvent`.
struct HandlePickup {
  std::uint32_t player = 0;
  std::uint16_t first = 0;   // +0x12
  std::uint16_t second = 0;  // +0x14
};

// A network event the server raised on our side (`PostRemoteEvent`, type 11).
// Those that carry a value carry it in four bytes.
//
// They go to every client, not only the one they are about. `NEPlayerSpawned`
// (9) carries the player's id; `NEPlayerDead` (10) carries eight bytes —
// `GameLogic::killPlayer` (Linux server 0x47a6b0) fills them with the player's
// `vtable+0xa8` (the id, as in the spawn event: a bot's death on the live co-op
// server named 244..255) and, at +4, a byte from its third argument, purpose not
// established. A client that takes every such event for its own loses its body to
// every bot that dies.
struct RemoteEvent {
  std::uint32_t category = 0;
  std::uint32_t number = 0;
  std::optional<std::int32_t> value;  // the first four bytes, when there are four
  std::vector<std::uint8_t> data;
};

struct Event {
  std::uint32_t type = 0;
  std::optional<CreateObject> object;
  std::optional<CreatePlayer> player;
  std::optional<DataBlockPiece> block;
  std::optional<CreateSpawnGroup> spawnGroup;
  // `PostRemoteEvent` (type 11): the server sends them to us as we do to it.
  // The most important one for us is `NEPlayerSpawned`.
  std::optional<RemoteEvent> remote;
  std::optional<EnterVehicle> enter;
  std::optional<CreateKit> kit;
  std::optional<HandlePickup> pickup;
  // `ExitVehicleEvent` (type 10): the number of the player who left.
  std::optional<std::uint32_t> exitPlayer;
  // `RemoveSpawnGroupEvent` (type 58): the group's number (8 bits; then its
  // network id, 16). `executeClient` (Linux 0x42e1b0) has the client's spawn
  // manager drop the group (`removeClientSpawnGroup`, vtable 0x98).
  std::optional<std::uint8_t> removeSpawnGroup;
  // `DestroyObjectEvent` (7): the network id (16) of an object the server
  // destroyed — the client destroys it (`GameClient::destroyObject`, OpenBF2,
  // `BF2.exe` 0x4d1f40). `DestroyPlayerEvent` (8): the id (8) of a player who
  // left (`GameClient::destroyPlayer`).
  std::optional<std::uint16_t> destroyObject;
  std::optional<std::uint8_t> destroyPlayer;
  // `BeginRoundEvent` (type 56): the round's begin state and the tick it changes
  // next (`BeginRoundEvent::serialize`, Linux 0x41f3d0: 32 bits from +0x10, 32 bits
  // from +0x14). The states are `ServerGameLogic::uPlaying`'s: 0 not begun, 1
  // waiting for players, 2 the start countdown, 3 the round is on.
  struct BeginRound {
    std::uint32_t state = 0;
    std::uint32_t nextChangeTick = 0;
  };
  std::optional<BeginRound> beginRound;
  // `EndOfRoundEvent` (type 48; OpenBF2 Game/GameEvents/EndOfRoundEvent.cpp,
  // `deSerialize` Linux 0x8070a00): three 32-bit numbers in field order. The
  // first is the rounds still to play on this map (the server sends
  // `roundsPerMap - roundsPlayed - 1`, `ServerGameLogic::endGame`); the client
  // hands it to the end-of-round movie, which offers "join game" only while it is
  // above zero. Then the winning team and the victory type
  // (`ScoreManager::forceSetWinner`, `setVictoryType`).
  struct EndOfRound {
    std::int32_t roundsLeft = 0;
    std::int32_t winner = 0;
    std::int32_t victoryType = 0;
  };
  std::optional<EndOfRound> endOfRound;
  // `KilledByEvent` (type 21; OpenBF2 Game/GameEvents/KilledByEvent.cpp,
  // `deSerialize`, Linux 0x42a920): killer 8, victim 8, "killer alive" 1, the
  // material 16 (`fillKilledByData` narrows it), the **weapon's** template 32
  // (`KilledByData +0x0C`), the killer's **vehicle** template 32 (+0x08 — his
  // soldier on foot), the last hit bone 8, and the last hit direction, a
  // compressed vector at 1/1000. The order is the wire's, not the struct's: a
  // live kill from a T-90 came as `Coaxial_mg_mec` then `rutnk_t90`
  // (tests/data/bf2-killed-by.bin).
  struct KilledBy {
    std::uint32_t killer = 0;
    std::uint32_t victim = 0;
    bool killerAlive = false;
    std::uint32_t material = 0;
    std::uint32_t weaponTemplate = 0;
    std::uint32_t vehicleTemplate = 0;
    std::uint32_t lastHitBone = 0;
    Vec3f lastHitDirection;
  };
  std::optional<KilledBy> killedBy;
  // `StringBlockEvent` (type 15; OpenBF2 StringBlockEvent.cpp `deSerialize`): a
  // "new string" bit, then the string's size (8), or a chunk — an 8-bit length
  // and the bytes (chat_string.h assembles them).
  struct StringBlock {
    bool isNew = false;
    std::uint32_t size = 0;
    std::vector<std::uint8_t> chunk;
  };
  std::optional<StringBlock> stringBlock;
  // The squads (OpenBF2 Game/GameEvents/<Name>.cpp, `deSerialize`):
  // `ChangeSquadNameEvent` (22) squad 8, team 8, player 8, an 8-bit length and
  // the name; `SetPrivateSquadEvent` (23) squad 8, team 8, private 1. The server
  // sends both for every squad whose name or lock is not the default
  // (`SquadManager::sendSquadManagerState`) and again on each change.
  struct SquadName {
    std::uint32_t squad = 0, team = 0, player = 0;
    std::string name;
  };
  std::optional<SquadName> squadName;
  struct SquadPrivate {
    std::uint32_t squad = 0, team = 0;
    bool isPrivate = false;
  };
  std::optional<SquadPrivate> squadPrivate;
};

// Assembles blocks from the chunks that arrive as events.
class DataBlockAssembler {
 public:
  // Returns the assembled block once it has been read to the end.
  std::optional<std::pair<std::uint32_t, std::vector<std::byte>>> feed(const DataBlockPiece& piece);

 private:
  std::uint32_t type_ = 0;
  std::uint32_t expected_ = 0;
  std::vector<std::byte> data_;
};

// Advances the reader by exactly the length of an event of the given type.
// false means the type is unknown or the packet was truncated.
bool skipEvent(BitReader& reader, std::uint32_t type);

// Parses one event: what we can, it fills in; the rest it simply skips.
std::optional<Event> readEvent(BitReader& reader);

// Reads a file of captured packets (`tools/linuxded/capture.py --out`):
// a u32 length per packet, then the bytes.
std::vector<std::vector<std::byte>> loadCapture(const std::string& path);

// Diagnostics: whether we read the packet as far as the ghost stream's flag and
// what is in it. nullopt means the event parsing broke off earlier (so it is us
// breaking); false means the server itself says the packet has no ghosts.
std::optional<bool> ghostFlag(std::span<const std::byte> packet);

// The controlled-object state — what the server sends us about **our** soldier.
//
// It sits in the ghost stream right after the header, when the header's
// `controlObjectState` flag is set, and it is read by a separate function
// `GhostManager::readControlObjectState` (0x445c30). The start of its layout:
//
//   12 bits                a number at the start (purpose not established)
//   1 sign bit + 31 bits   a counter; with the sign the value is negated
//   32, 32, 32             three numbers — the compression reference point
//   16 bits                the controlled object's network id
//   1 bit ...             then the rest of the state, which we have not parsed
//
// Two things here are worth naming precisely, because they are visible directly in the code.
//
// **The triple of numbers is ONLY the compression reference point, not our
// position.** Before it stands `BitStream::resetCompressionVector`, and right
// after it `setCompressionVector` with the same triple (0x445cf7 and 0x445d4b).
// And the function does **nothing** else with it: among all its calls there is
// not one that puts the triple into the object. The soldier's position travels
// later — in the object's own state (`setNetUpdate` through the descriptor, the
// virtual call `*0x68(%rax)`), packed as a difference from this point.
//
// For a while we took the triple for our position: it did match
// `gameLogic.setBeforeSpawnCamera` exactly. It matched because before spawning
// the controlled object is that very camera. After spawning the point turned out
// to be rounded and stood a metre above the ground however far we walked — and the
// soldier was jerked upwards every time a packet arrived. A plausible number
// turned out to be worse than none.
//
// **The 16 bits after it are our object's network id.** The engine hands it
// straight to `NetworkManager::getObject`, and the result to `getSoldier`
// (0x445dc3 and 0x445e01). That is a direct answer to which soldier is ours:
// guessing by the distance to a flag is no longer needed.
struct ControlObjectState {
  // The number of bits the state occupies after this field — see
  // `skipControlObjectState` in bf2_events.cpp (`BF2.exe`, 0x5b9230).
  std::uint32_t first = 0;
  // The controlled soldier's own state, read with the precise layout — see
  // obf2/net/soldier_state.h. Present when the controlled object is a soldier
  // and the state reached that far; the spawn camera's state reads as something
  // else and is not taken for one.
  std::optional<SoldierState> soldier;
  std::int32_t counter = 0;
  // The name is deliberately not "position": a soldier must not be moved by this field.
  Vec3f compressionReference;
  std::uint16_t networkId = 0;
};

std::optional<ControlObjectState> readControlObjectState(std::span<const std::byte> packet);

// The ghost stream's header — what comes after the events in a data packet.
// The time is counted in ticks of 1/30 of a second.
struct GhostHeader {
  std::uint32_t time = 0;
  std::uint8_t records = 0;
  bool controlObjectState = false;
};

// What an object is on the network — which layout its ghost records have (see
// kSoldierGhostMask below for how it is told).
enum class GhostClass {
  Unknown,       // no full record seen yet: walked by length, not read
  SimpleObject,  // `SimpleObjectNetworkable`: vehicles, emplacements, props
  Soldier,       // `SoldierNetworkable`
  // `Player`: known by the network id `CreatePlayerEvent` names, not by a mask
  // (docs/functions/player-state.md).
  Player,
  // `SpawnGroup`: known by the network id `CreateSpawnGroupEvent` names.
  SpawnGroup,
  // `ScoreManager`: the static networkable 0x0201 (score_state.h).
  ScoreManager,
  // `ControlPointNetworkable` (control_point_state.h): an object the caller
  // marked as one of the level's control points.
  ControlPoint,
  // `GenericProjectileNetworkable` (projectile_state.h): an object whose
  // template's class is `GenericProjectile`, told by its create event's template.
  Projectile,
};

// One ghost stream record (`GhostManager::readData`):
//   2 bits kind, 16 bits network id;
//   kind 1 — a state update: 1 bit, 11 bits of content length, the content;
//   kind 0 — nothing more; kind 3 — the object disappears;
//   kind 2 the engine treats as a stream error.
//
// The length in a record makes it possible to skip it without parsing the
// contents — which is exactly what the engine does when it does not know the object.
struct GhostRecord {
  std::uint32_t kind = 0;
  std::uint16_t networkId = 0;
  std::uint32_t payloadBits = 0;
  std::size_t payloadStart = 0;  // the content's first bit in the packet
  bool baseline = false;         // the flag before the length
  std::uint32_t stateMask = 0;   // the first networkable's mask, 19 or 21 bits by class
  GhostClass netClass = GhostClass::Unknown;
  // The position, when the class is known and the mask carries it. For a soldier
  // it is the pivot, `coll-soldier-pivot-height` above the feet.
  std::optional<Vec3f> position;
  // A soldier's state, as far as the ghost layout could be read.
  std::optional<SoldierState> soldier;
  // A player's state (player_state.h), when it was read to its end inside the
  // record's length.
  std::optional<PlayerState> player;
  // A spawn group's state, likewise.
  std::optional<SpawnGroupState> spawnGroup;
  // The round's score (score_state.h), likewise.
  std::optional<ScoreState> score;
  // A control point's state (control_point_state.h), likewise.
  std::optional<ControlPointState> controlPoint;
  // A projectile's state (projectile_state.h): kept whenever it fitted into the
  // content, read whole or not (`complete` says which).
  std::optional<ProjectileState> projectile;
  // A simple object's rotation (x, y, z, w, normalised) and the length it had as
  // read (readSimpleObjectState).
  std::optional<std::array<float, 4>> rotation;
  float rotationLength = 0.0f;
  // How many bits the content's reader took — the check that a layout is right:
  // a record read whole ends exactly at `payloadBits`.
  std::uint32_t readBits = 0;
  // A simple object's bits after the head (readBits..payloadBits), one per byte:
  // the other networkables of its descriptor, not read yet.
  std::vector<std::uint8_t> tail;
};

// A record's contents are read by the object's networked class. For everything
// with a position in the world that is `SimpleObjectNetworkable::setNetUpdate`
// (Linux server, 0x5d78a0), and it always begins the same way:
//
//   19 bits  the state mask — which fields are in this update
//   if bit 1 is set in the mask:
//       a compressed vector — the object's position
//
// The mask is read at 0x5d7a17 (`readBits(..., 0x13)`), bit 1 is checked first
// (`testb $0x2` at 0x5d7a35), and its branch holds
// `BitStream::readCompressedVector` (0x5d845b). The precision is the constant
// 0.0005 from `.rodata` at 0xb47194, not a guess of ours.
//
// Which fields this class has at all is said by `getGhostStateMask` (0x5d6990):
// it returns 0x5849b. The other fields we have not parsed — and need not: a
// record's length lets the tail be skipped, as the engine itself does.
inline constexpr unsigned kObjectStateMaskBits = 19;
inline constexpr std::uint32_t kObjectStatePosition = 0x2;
inline constexpr float kObjectPositionPrecision = 0.0005f;

// The head of the update, in the client's order (`BF2.exe` 0x6306b0,
// docs/functions/network-events.md "A vehicle's update, read whole"):
//
//   0x2  position, compressed against the stream's vector, 0.0005
//   0x4  linear velocity, compressed from zero, 0.001          (a physical object)
//   0x8  angular velocity: the wide vector from zero, 0.01     (a physical object;
//        an object of physics type 3 sends a compressed one, 0.001 — not taken)
//   0x1  rotation, a quaternion of four 14-bit values (0x6b7270), w x y z
//
// "Physical" is asked of the object on the client and not sent; a bit the server
// set is taken to mean the field is there. What follows the rotation depends on
// the object (a range from one of its interfaces, its control object) and is not
// read: the record's length passes it.
inline constexpr std::uint32_t kObjectStateRotation = 0x1;
inline constexpr std::uint32_t kObjectStateVelocity = 0x4;
inline constexpr std::uint32_t kObjectStateAngular = 0x8;
inline constexpr float kObjectVelocityPrecision = 0.001f;
inline constexpr float kObjectAngularPrecision = 0.01f;
inline constexpr unsigned kObjectQuaternionBits = 14;

struct SimpleObjectState {
  std::uint32_t mask = 0;
  std::optional<Vec3f> position;
  std::optional<Vec3f> velocity;
  std::optional<Vec3f> angular;
  // (x, y, z, w) as read, before normalising, and its length — a quaternion read
  // from the right place is 1 long (the measure the notes name).
  std::optional<std::array<float, 4>> rotation;
  float rotationLength = 0.0f;
};

std::optional<SimpleObjectState> readSimpleObjectState(BitReader& reader, const Vec3f& reference);

// **Which layout a record has is the object's class, not who sits in it.** A
// record carries the networkables of the object (`GhostManager::readData`,
// `BF2.exe` 0x5b8840, through 0x5b81c0 with type 2); a soldier's is
// `SoldierNetworkable` (soldier_state.h, the ghost layout), a vehicle's starts
// with the 19-bit one above.
//
// We used to call an object a soldier when a player had entered it — and a jeep
// with a player at the wheel was read with the soldier's layout: positions metres
// off one packet and 4.9e29 the next. Measured on a live server (`--record`, the
// original client in the gas station's jeep):
//
//   jeep 1841 (template 5184)     full record mask 0x5849b, the position at bit 19
//   soldier 1731 (template 3283)  full record mask 0x1950ff, the position at bit 21;
//                                 a record with mask 0 is exactly 30 bits
//
// Both full masks are the classes' `getGhostStateMask` (Linux server 0x5d6990 and
// 0x5dabe0): a full record carries the whole mask. That is what tells the class.
// Both positions are raw floats (level 0) or a difference from the stream's
// compression vector — the jeep's lands on its creation spot to the centimetre
// either way.
inline constexpr std::uint32_t kObjectGhostMask = 0x5849b;
inline constexpr std::uint32_t kSoldierGhostMask = 0x1950ff;

// The width of the length field. In the engine it is computed on the fly, and on
// the wire it is exactly eleven bits: with it the whole captured sample parses to
// the last byte, with any other not a single packet does.
inline constexpr unsigned kGhostLengthBits = 11;

// Reads the ghost stream's header from a data packet, having walked the events.
// nullopt means the packet has no ghosts or some event cannot yet be skipped by
// the right length.
std::optional<GhostHeader> readGhostHeader(std::span<const std::byte> packet);

// The ghost stream's records. Empty when there are no ghosts or when the packet
// has the controlled-object state flag set — that state comes before the records
// and is not parsed yet.
// `reference` is the stream's compression vector: the one the latest
// controlled-object state set (measured on the jeep above).
//
// `classOf` says what the caller already knows about an object. A record of an
// unknown object is recognised by its full mask (kSoldierGhostMask /
// kObjectGhostMask) and comes back with `netClass` set, for the caller to keep.
//
// `inventoryOf` gives a soldier's inventory size (`SoldierTemplate::
// getInventorySize`, the template's `inventorySize`), which is the width of his
// weapon index (0x1000); -1 or no function leaves the reader stopping there.
std::vector<GhostRecord> readGhostRecords(
    std::span<const std::byte> packet, const Vec3f& reference = {},
    const std::function<GhostClass(std::uint16_t)>& classOf = {},
    const std::function<int(std::uint16_t)>& inventoryOf = {},
    // A projectile's armor (projectile_state.h): nothing for none; the outer
    // optional empty when it is not known.
    const std::function<std::optional<std::optional<ProjectileArmor>>(std::uint16_t)>& armorOf = {});

// How a packet's event list was walked: how many the packet declares, how many
// were read, and — when the walk broke off — the type it broke on. A broken walk
// loses every event after it, and the ghost stream with them.
struct EventWalk {
  std::uint32_t declared = 0;
  std::uint32_t read = 0;
  std::optional<std::uint32_t> brokeOn;
  // The block's own number (5 bits) and the resend bit (`GameEventManager::
  // transmit`); `hasBlock` false when the packet carries no events.
  bool hasBlock = false;
  std::uint32_t block = 0;
  bool resend = false;
};

// Walks a data packet and returns every event in it.
// Empty means this is not a data packet or it broke off on the very first event.
std::vector<Event> readEvents(std::span<const std::byte> packet, EventWalk* walk = nullptr);

}  // namespace obf2::net::bf2
