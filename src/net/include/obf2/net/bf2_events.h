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
struct RemoteEvent {
  std::uint32_t category = 0;
  std::uint32_t number = 0;
  std::optional<std::int32_t> value;
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
  std::uint32_t first = 0;
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
  bool baseline = false;         // the flag before the length
  std::uint32_t stateMask = 0;   // 19 bits: which fields travel in the content
  // The position, when the mask has kObjectStatePosition set.
  std::optional<Vec3f> position;
  // Where the soldier is looking, in degrees (mask 0x2).
  std::optional<float> yaw;
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

// **A soldier is read differently, and that is no detail.** Its networked class
// is its own — `SoldierNetworkable` (0x5dc640), and in it:
//
//   * the mask is not 19 bits but **21** (`readBits(..., 0x15)` at 0x5dc719).
//     The mask's width is the number of bits in `getGhostStateMask`, and that
//     differs per class: 0x5849b for a simple object, for a soldier
//     0x1950ff (0x5dabe0);
//   * the position is enabled by **bit 7**, not bit 1 (`testb %dl, %dl; js` at
//     0x5dc941 — a check of the sign bit of the mask's low byte);
//   * the reference point is neither the stream's nor the previous position but
//     `dice::hfe::nullVec`, that is **zero** (0x5dd367). So a soldier's position
//     arrives essentially absolute;
//   * the precision is 0.01 (the constant at 0xb6d934), not 0.0005.
//
// That is why other players' soldiers did not move for us: we read them with a
// simple object's layout. Which object is a soldier we know for certain — the
// server says so itself with the `CreatePlayerEvent` and `EnterVehicleEvent` events.
// The layout of the start of a soldier's state comes from the client, from the
// `SoldierNetworkable::setNetUpdate` (`BF2.exe`, 0x62d4e0):
//
//   mask                      21 bits     (0x62d5xx, a read of 0x15)
//   if mask & 0x40            8 bits, then 1 bit
//   if mask & 0x20            3 bits, then 3 bits   (range 0..4)
//   if mask & 0x8000          another branch — the ragdoll state, which we do not read
//   if mask & 0x1             **the position**: a compressed vector, precision 0.001
//
// Then come the velocity (0x80), the angles (0x2 -> yaw, 0x4 -> pitch, 0x8,
// 0x10 — 12 bits each, expanded into ±360/±90/±180/±90) and a dozen more fields;
// we do not need them yet, and they need not be read — after the position we can
// stop straight away.
//
// Two things we stumbled on before this:
//
// * the position is enabled by **bit 0**, not 7. We used to take bit 7 (that is
//   the velocity) and read from the wrong place — numbers like -6.7e27 came out;
// * the reference is the **stream's compression vector**, the same one the
//   controlled-object state sets. Visible in `FUN_0062bd60`: it calls the vector
//   read with the field `stream+0x54`, that is with the internal compression
//   vector, rather than with the zero passed in (the zero is for the neighbouring fields, 0x62bdb0).
inline constexpr unsigned kSoldierStateMaskBits = 21;
inline constexpr std::uint32_t kSoldierStatePosition = 0x1;
inline constexpr std::uint32_t kSoldierStateHasByte = 0x40;    // 8 bits + 1 bit
inline constexpr std::uint32_t kSoldierStateHasPair = 0x20;    // 3 bits + 3 bits
inline constexpr std::uint32_t kSoldierStateRagdoll = 0x8000;  // another branch
inline constexpr float kSoldierPositionPrecision = 0.001f;
inline constexpr std::uint32_t kSoldierStateVelocity = 0x80;   // a compressed vector
inline constexpr float kSoldierVelocityPrecision = 0.01f;
inline constexpr std::uint32_t kSoldierStateYaw = 0x2;         // 12 bits -> ±360°

// The angles travel in twelve bits expanded into a range. The arithmetic is
// verbatim from the client: `(v * 2/4095 - 1) * limit`, where the limit for yaw is 360.
inline constexpr unsigned kSoldierAngleBits = 12;
inline constexpr float kSoldierYawRange = 360.0f;
inline float soldierAngle(std::uint32_t packed, float range) {
  const float unit = static_cast<float>(packed) * (2.0f / 4095.0f) - 1.0f;
  return unit * range;
}
// `dice::hfe::nullVec` — it is what stands as the reference in a soldier's layout.
inline constexpr Vec3f kNullVec{};

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
// referenceFor is the reference point for **this** object's compressed vector.
// Measurement showed the difference is small (metres) rather than half a map
// away: so the base is the object's own last known position, not one point for
// the whole stream. Whoever calls remembers the last position; while the object
// is unknown, the position from `CreateObjectEvent` serves as the base.
//
// isSoldier says whether this id belongs to a player's soldier: a soldier has a
// different layout (see above), and without this it reads as rubbish.
std::vector<GhostRecord> readGhostRecords(
    std::span<const std::byte> packet,
    const std::function<Vec3f(std::uint16_t)>& referenceFor = {},
    const std::function<bool(std::uint16_t)>& isSoldier = {});

// Walks a data packet and returns every event in it.
// Empty means this is not a data packet or it broke off on the very first event.
std::vector<Event> readEvents(std::span<const std::byte> packet);

}  // namespace obf2::net::bf2
