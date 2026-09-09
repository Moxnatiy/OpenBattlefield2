#pragma once
// The protocol of the original BF2 server.
//
// The layout is taken from the engine (Linux server, `dice::hfe::io::NetServer`):
// `_update` parses the header and dispatches packets by type, while
// `handleConnectRequest`, `sendConnectAccept` and `sendConnectDenied` define the
// bodies.
//
// Every packet's header is exactly 12 bits:
//   4 bits  type
//   8 bits  connection id (0 in the client before connecting)
//
// Bits are packed low first — the same as in our BitStream.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace obf2::net::bf2 {

// The packet types from `NetServer::_update`'s dispatcher.
enum class PacketKind : std::uint8_t {
  ConnectRequest = 1,
  ConnectAccept = 2,
  ConnectDenied = 3,
  ConnectAcceptAck = 4,
  Disconnect = 5,
  PingRequest = 7,
  PingResponse = 8,
  ServerInfoRequest = 9,
  Data = 15,
};

// The constant the engine checks first (`checkVersion`: the first number has to
// be exactly 0x1002, otherwise it refuses).
inline constexpr std::uint32_t kProtocolMagic = 0x1002;

// The game's version. The bytes 15 0C 51 00 read as 1.5 and build 0x0C51 = 3153,
// that is exactly `bf2-linuxded-1.5.3153.0`. Found by a binary search against a
// live server: it answers "too old" (0x17) or "too new" (0x18), and 32 steps
// give the exact number.
inline constexpr std::uint32_t kGameVersion = 0x150C5100;

// The refusal reasons from `handleConnectRequest`.
enum class DenyReason : std::uint32_t {
  ServerFull = 0x02,
  VersionMismatch = 0x09,
  WrongPassword = 0x11,
  Banned = 0x16,
  ClientTooOld = 0x17,
  ClientTooNew = 0x18,
  NoFreeSlots = 0x1E,
  PunkBusterRequired = 0x1F,
  WrongMod = 0x24,
};

std::string_view denyReasonName(DenyReason reason);

struct ConnectRequest {
  std::uint32_t magic = kProtocolMagic;
  std::uint32_t version = kGameVersion;
  bool punkBuster = true;
  std::uint32_t reconnectToken = 0;
  std::string password;
  std::string modDirectory = "mods/bf2";
};

struct ConnectAccept {
  std::uint8_t connectionId = 0;
  std::uint32_t serverTime = 0;
  bool punkBuster = false;
};

struct ConnectDenied {
  DenyReason reason = DenyReason::VersionMismatch;
  std::string modDirectory;  // the server adds it only to the WrongMod refusal
};

// The reliability layer's extended header. It stands right after the basic one in
// ping and data packets (`writeExtendedHeader`):
//   6 bits   the packet number (wrapping at 64)
//   6 bits   the number of the last one received
//   32 bits  a bit mask of which of the previous ones arrived
struct ExtendedHeader {
  std::uint8_t sequence = 0;
  std::uint8_t ack = 0;
  std::uint32_t ackBits = 0;
};

// The framing of the event stream inside a data packet.
//
// `GameEventManager::processReceivedPacket` reads: 1 bit "there are events",
// 8 bits of count, 5 bits and one more service bit. Then `readGameEvent` takes
// the event's type in N bits, where N is the smallest for which (1<<N)-1 covers
// the event registry's size; against a live server that is 7.
//
// Before the streams, the data packet's header holds another 16 bits — the
// payload's length in bytes. In all the header takes exactly 72 bits (9 bytes).
inline constexpr unsigned kStreamFramingBits = 16;
inline constexpr unsigned kEventTypeBits = 7;

// The type of the event data blocks travel in (`DataBlockEvent::getType`).
inline constexpr std::uint32_t kDataBlockEvent = 4;

// The type of the block with the client's details (`GameServer::handleDataBlock`).
inline constexpr std::uint32_t kClientInfoBlock = 1;

// The block the engine reads in `ClientInfo::setFromDataBlock`:
//   u16 length + name, u32 name hash, 1 sign bit + 31 bits of profile number,
//   u16 length + clan tag, u16 length + authentication string, 1 bit.
//
// On a server without ranking (`sv.ranked 0`) the hash and the authentication
// string are not checked, and the engine assembles the player's final name as
// "tag + space + name" (`GameServer::handleClientInfo`).
struct ClientInfo {
  std::string name;
  std::uint32_t nameHash = 0;
  std::int32_t profileId = 0;
  std::string clanTag;
  std::string auth;
  bool flag = false;
};

// The challenge event: the server sends it right after connecting
// (`GameServer::onNewConnection`), and the client has to answer.
struct ChallengeEvent {
  std::string challenge;
  std::string modDirectory;
};

// A packet parsed off the network.
struct Incoming {
  PacketKind kind = PacketKind::Data;
  std::uint8_t connectionId = 0;
  std::optional<ConnectAccept> accept;
  std::optional<ConnectDenied> denied;

  // Present in pings and data.
  std::optional<ExtendedHeader> extended;
  // The server's time from a ping request: it has to be returned unchanged.
  std::optional<std::uint32_t> pingTime;

  // For data packets: how many events are inside and the first one parsed.
  int eventCount = 0;
  std::optional<ChallengeEvent> challenge;
};

// Assembles a connection request (type 1).
std::vector<std::byte> writeConnectRequest(const ConnectRequest& request);

// The challenge reply (event type 2). A server without an authenticator
// (`sv.internet 0`) checks only the network version — it does not read the rest of the block.
//
// The network version is the same as in the connection request: in the engine it
// is `BuildNrUtil::getNetVersionNumber()`, and it returns exactly kGameVersion.
std::vector<std::byte> writeChallengeResponse(std::uint8_t connectionId,
                                              const ExtendedHeader& header, std::uint8_t batch);

// The data block event: first the header (the block's type and full size), then
// chunks of no more than 255 bytes. Once the whole block is assembled, the engine
// hands it to `GameServer::handleDataBlock`.
std::vector<std::byte> writeDataBlockHeader(std::uint8_t connectionId,
                                            const ExtendedHeader& header, std::uint8_t batch,
                                            std::uint32_t blockType, std::uint32_t size);
std::vector<std::byte> writeDataBlockChunk(std::uint8_t connectionId, const ExtendedHeader& header,
                                           std::uint8_t batch, std::span<const std::byte> chunk);

// The "raise this event on your side" event (`PostRemoteEvent`, type 11):
//   4 bits category, 32 bits event number, 32 bits delay (float),
//   8 bits data length, then the bytes.
//
// Category 6 the server hands to `GameServer::handleNetworkEvent`
// (`GameServer::handleEvent` compares against exactly six; two there is the HUD).
// Number 2 in the jump table leads to `clientLoadComplete`: until the client
// sends it, the connection's state stays below `isClientReady`'s threshold, and
// the server sends neither world objects nor a ghost stream.
inline constexpr std::uint32_t kPostRemoteEvent = 11;
inline constexpr std::uint32_t kNetworkCategory = 6;
inline constexpr std::uint32_t kNetDataBlockReady = 1;
inline constexpr std::uint32_t kNetLoadComplete = 2;
inline constexpr std::uint32_t kNetStartSimulation = 3;
inline constexpr std::uint32_t kNetDatabaseComplete = 4;
inline constexpr std::uint32_t kNetSelectSpawnGroup = 6;
inline constexpr std::uint32_t kNetSelectTeam = 7;
inline constexpr std::uint32_t kNetSelectKit = 8;
// The server's confirmation that the player spawned. In the original's captured
// traffic it arrives 100 ms after `NESelectSpawnGroup`, and it is the most direct
// sign that the spawn succeeded.
inline constexpr std::uint32_t kNetPlayerSpawned = 9;

// One set of a player's actions — what the client sends the server thirty times a
// second. The layout comes from `PlayerActionManager::processReceivedPacket`
// (0x44d670) and is cross-checked against the original's captured traffic:
//
//   1 bit                  are there actions
//   4 bits                 how many sets in the packet (the original sends three)
//   9 bits                 a number (it varies in the dump; purpose not
//                          established)
//   1 sign bit + 31 bits   the input counter, growing by one per packet
//   then, per set:
//      6 times: 1 sign bit + 15 bits of value
//      32 bits  the button mask
//      9 bits   (always zero in the dump)
//      1 bit    a flag (one in the dump)
//
// What the axes mean is visible from the dump: while the player ran forward the
// third axis stood at 99 and the rest at zero; the fifth and sixth twitched
// slightly all the time — that is the mouse. The button mask in those same
// seconds equalled 32 while the player held sprint, and zero when they released it.
struct PlayerAction {
  std::int16_t axes[6] = {0, 0, 0, 0, 0, 0};
  std::uint32_t buttons = 0;
  bool flag = true;
};

// The axes and buttons are named not from the dump but from the table the engine
// registers its control constants with (BF2.exe, 0x6904c0 onwards: the name
// string, then its number in EDX). The order from there:
//
//   0  c_PIYaw          4  c_PIMouseLookX     8  c_PIFire
//   1  c_PIPitch        5  c_PIMouseLookY     9  c_PIAction
//   2  c_PIRoll         6  c_PICameraX       10  c_PIUse
//   3  c_PIThrottle     7  c_PICameraY       13  c_PISprint
//
// The first six go into the packet — exactly axes 0..5.
//
// For infantry the layout is set by `Settings/Controls.con`:
//
//   ControlMap.addKeysToAxisMapping c_PIYaw      IDKey_D IDKey_A
//   ControlMap.addKeysToAxisMapping c_PIThrottle IDKey_W IDKey_S
//   ControlMap.addKeyToTriggerMapping c_PIAction IDKey_Space
//   ControlMap.addKeyToTriggerMapping c_PISprint IDKey_LeftShift
//
// So a soldier strafes with the **yaw axis**: the engine has no separate strafe
// axis. Until we knew that, strafing was impossible altogether.
inline constexpr int kAxisYaw = 0;       // D/A — strafing for infantry
inline constexpr int kAxisPitch = 1;
inline constexpr int kAxisRoll = 2;
inline constexpr int kAxisThrottle = 3;  // W/S — forward and back
inline constexpr int kAxisMouseX = 4;    // c_PIMouseLookX
inline constexpr int kAxisMouseY = 5;    // c_PIMouseLookY
// The third axis's old name — kept so the calls need not be rewritten.
inline constexpr int kAxisForward = kAxisThrottle;
// Full movement in the dump is exactly 99.
inline constexpr std::int16_t kAxisFull = 99;

// On the wire an axis travels as an integer, while in the engine it is a float.
// `PlayerAction::set` stores the axes as int16 (`BF2.exe`, 0x5bc890), and the
// reverse conversion beside it (0x5bc5f0) does exactly `(float)value * 0.01`.
//
// Hence "full movement = 99": the throttle axis reached 0.99.
inline constexpr float kAxisWireScale = 100.0f;

// The button mask: the bit is the constant's number minus `c_PIFire`'s number.
// The check agrees with the traffic: sprint is 13 - 8 = 5, and bit 5 (value 32)
// is exactly what stood in the dump while the player held Shift.
inline constexpr std::uint32_t kButtonFire = 1u << 0;    // c_PIFire   (8)
inline constexpr std::uint32_t kButtonAction = 1u << 1;  // c_PIAction (9) — jump
inline constexpr std::uint32_t kButtonUse = 1u << 2;     // c_PIUse    (10)
inline constexpr std::uint32_t kButtonSprint = 1u << 5;  // c_PISprint (13)

// The whole action stream from a packet.
struct PlayerActions {
  std::uint32_t number = 0;  // the 9 bits at the start
  std::int32_t tick = 0;     // the input counter
  std::vector<PlayerAction> actions;
};

// The action stream's layout — **one description for both directions**.
//
// The same body both reads and writes: the difference is the cursor (`ReadCursor`
// or `WriteCursor` from `bitstream.h`). While the description is one, the
// assembler and the parser cannot diverge — and while there were two of them,
// every field had to be written twice.
//
// The fields come from `PlayerActionManager::processReceivedPacket` (0x44d670).
template <typename Cursor>
bool serializePlayerActions(Cursor& cursor, PlayerActions& stream) {
  std::uint32_t count = static_cast<std::uint32_t>(stream.actions.size());
  if (!cursor.bits(count, 4)) return false;
  if (!cursor.bits(stream.number, 9)) return false;
  // On a read this creates the required number of sets; on a write it changes
  // nothing: there count already equals the size.
  if (count > 15) return false;
  stream.actions.resize(count);

  if (count > 0 && !cursor.signedBits(stream.tick, 31)) return false;

  for (PlayerAction& action : stream.actions) {
    for (std::int16_t& axis : action.axes) {
      std::int32_t value = axis;
      if (!cursor.signedBits(value, 15)) return false;
      axis = static_cast<std::int16_t>(value);
    }
    if (!cursor.bits(action.buttons, 32)) return false;
    std::uint32_t spare = 0;  // always zero in the captured traffic
    if (!cursor.bits(spare, 9)) return false;
    if (!cursor.flag(action.flag)) return false;
  }
  return true;
}

// A packet of actions alone: it holds no events.
std::vector<std::byte> writePlayerActions(std::uint8_t connectionId, const ExtendedHeader& header,
                                          const PlayerActions& stream);

// Read the action stream from a data packet. nullopt means this is not a data
// packet or it holds no actions.
//
// We read both our own packets and ones captured from the original client with
// the same parser.
std::optional<PlayerActions> readPlayerActions(std::span<const std::byte> packet);

// `value` travels in the payload as a 32-bit number — that is how the events
// carrying a choice (team, kit, spawn point) read it.
std::vector<std::byte> writePostRemoteEvent(std::uint8_t connectionId,
                                            const ExtendedHeader& header, std::uint8_t batch,
                                            std::uint32_t category, std::uint32_t event,
                                            std::optional<std::int32_t> value = std::nullopt);

// The content check (`ContentCheckEvent`, type 46): three 128-bit hashes.
// The server compares them in `GameServer::onContentCheckEvent` and only on a
// match sets `contentValid` for the client. Without it
// `clientSendDatabaseComplete` queues the client for disconnection, and the
// connection's state never grows to what the ghost stream requires.
//
// The order of the hashes:
//   1. what the server computes itself at startup (`runMiscChecksum`);
//   2. a line from `mods/<mod>/std_archive.md5`;
//   3. a line from `mods/<mod>/levels/<level>/archive.md5`.
//
// The line number in both files comes from `MapInfo::getChallengeOrdinal()`.
inline constexpr std::uint32_t kContentCheckEvent = 46;

// The server considers the check only when the connection's state is already
// greater than one, that is after NELoadComplete. Otherwise it silently ignores it.
std::vector<std::byte> writeContentCheckEvent(std::uint8_t connectionId,
                                              const ExtendedHeader& header, std::uint8_t batch,
                                              const std::array<std::byte, 16>& misc,
                                              const std::array<std::byte, 16>& archives,
                                              const std::array<std::byte, 16>& level);

// Reads a fingerprint file: "number md5" for the archives, "name number md5" for
// the level. nullopt means there is no line with that number.
std::optional<std::array<std::byte, 16>> readFingerprint(std::string_view text, int ordinal);

// The name hash a ranked server compares against the one sent: h = 0x1505, then
// for every lower-cased character h = h * 0x21 ^ c.
std::uint32_t clientInfoNameHash(const std::string& name);

std::vector<std::byte> buildClientInfo(const ClientInfo& info);

// The ping reply. `time` is the same number the server sent: it computes the
// latency from the difference.
std::vector<std::byte> writePingResponse(std::uint8_t connectionId, const ExtendedHeader& header,
                                         std::uint32_t time);

// A short packet of nothing but the header: acknowledgement, disconnect, ping.
std::vector<std::byte> writeShortPacket(PacketKind kind, std::uint8_t connectionId);

// Parses what arrived from the server. nullopt means the packet is shorter than the header.
std::optional<Incoming> readPacket(std::span<const std::byte> data);

}  // namespace obf2::net::bf2
