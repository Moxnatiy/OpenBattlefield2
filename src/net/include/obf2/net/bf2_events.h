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
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/net/bitstream.h"
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
struct CreatePlayer {
  std::uint32_t team = 0;
  std::uint32_t squad = 0;
  std::uint32_t id = 0;
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

// The level's details — block type 5, which the server sends right after
// registration (`GameServer::sendClientMapInfo` hands over the `MapInfo`).
struct MapInfo {
  std::string levelName;
  std::string gameMode;
  int size = 0;
  // The block's first number. It looks like the "challenge number" — the same
  // line in the fingerprint files the server checks the content with. It is
  // verified against a live server rather than taken on trust.
  std::uint32_t first = 0;
};

inline constexpr std::uint32_t kMapInfoBlock = 5;

std::optional<MapInfo> parseMapInfo(std::span<const std::byte> block);

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
  bool flag1 = false;
  bool flag2 = false;
  bool flag3 = false;
  std::uint8_t worldX = 0;     // the packed position, axis 1
  std::uint8_t worldZ = 0;     // the packed position, axis 2
  // The group's network id — the object system knows it by this. For choosing a
  // spawn point it is **not** needed: the original does not send it.
  std::uint16_t networkId = 0;  // 16 bits
};

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
  // `ExitVehicleEvent` (type 10): the number of the player who left.
  std::optional<std::uint32_t> exitPlayer;
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
std::vector<GhostRecord> readGhostRecords(
    std::span<const std::byte> packet, const Vec3f& reference = {},
    const std::function<GhostClass(std::uint16_t)>& classOf = {});

// Walks a data packet and returns every event in it.
// Empty means this is not a data packet or it broke off on the very first event.
std::vector<Event> readEvents(std::span<const std::byte> packet);

}  // namespace obf2::net::bf2
