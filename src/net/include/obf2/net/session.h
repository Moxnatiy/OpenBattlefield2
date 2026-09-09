#pragma once
// The handshake and state exchange between client and server.
//
// The sequence is taken from the original's protocol (the packet types are
// cross-checked against Refractor-2-BitStream-Emulator, which connected to live BF2 servers):
//
//   client -> server : ConnectionRequest   (player name, protocol version)
//   server -> client : ConnectionAccept    (player id, level name, mode)
//                      or ConnectionDenied (a reason code)
//   client -> server : ConnectionAcknowledge
//   then             : Data / PingRequest / PingResponse / Disconnect
//
// We do not promise byte-for-byte compatibility with original servers (see
// docs/TODO.md) — but we keep the same format and sequence so that compatibility
// stays reachable.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/net/bitstream.h"
#include "obf2/net/connection.h"

namespace obf2::net {

// Our protocol's version. The original game put its own here.
inline constexpr std::uint32_t kProtocolVersion = 1;

// The refusal reason in ConnectionDenied.
enum class DenyReason : std::uint32_t {
  ServerFull = 1,
  WrongVersion = 2,
  Banned = 3,
};

std::string_view denyReasonName(DenyReason reason);

struct ConnectionRequest {
  std::uint32_t protocolVersion = kProtocolVersion;
  std::string playerName;
};

struct ConnectionAccept {
  std::uint32_t playerId = 0;
  std::string levelName;
  std::string gameMode;
};

// A player's input for one tick. The client sends this on every simulation tick,
// and the server applies it and acknowledges by number — so the client knows what was taken.
struct PlayerInput {
  std::uint32_t sequence = 0;
  float moveForward = 0.0f;  // -1..1
  float moveRight = 0.0f;    // -1..1
  float yaw = 0.0f;          // degrees
  float pitch = 0.0f;
  bool fire = false;
  bool jump = false;
  bool sprint = false;
};

// A spawn or an update of an object in the world. The position travels as a
// compressed vector — the same one as in the original.
struct ObjectUpdate {
  std::uint32_t objectId = 0;
  std::string templateName;  // on a spawn only
  Vec3f position;
  Vec3f rotation;
  bool spawn = false;
};

// The string lengths in packets are fixed: the original does the same, because it
// means the length need not be written separately.
inline constexpr std::size_t kPlayerNameLength = 24;
inline constexpr std::size_t kLevelNameLength = 32;
inline constexpr std::size_t kGameModeLength = 16;
// 64, not 32: the game has names up to 36 characters
// ("fence_corrugated_3x12m_broken_parts"), and at 32 they were silently
// truncated — the object arrived but no geometry could be found for it.
inline constexpr std::size_t kTemplateNameLength = 64;

// The precision of positions in update packets, in world units.
inline constexpr float kPositionPrecision = 0.01f;

// The input axes are quantised: 8 bits per axis is more than enough, because this
// is an analogue stick or keys.
inline constexpr unsigned kInputAxisBits = 8;
inline constexpr unsigned kInputSequenceBits = 16;

// --- writing ---
bool writeConnectionRequest(BitWriter& writer, const ConnectionRequest& request);
bool writeConnectionAccept(BitWriter& writer, const ConnectionAccept& accept);
bool writeConnectionDenied(BitWriter& writer, DenyReason reason);
bool writeConnectionAcknowledge(BitWriter& writer);
bool writeDisconnect(BitWriter& writer);
bool writeObjectUpdates(BitWriter& writer, const std::vector<ObjectUpdate>& updates,
                        const Vec3f& reference);
bool writePlayerInput(BitWriter& writer, const PlayerInput& input);

// --- reading ---
std::optional<ConnectionRequest> readConnectionRequest(BitReader& reader);
std::optional<ConnectionAccept> readConnectionAccept(BitReader& reader);
std::optional<DenyReason> readConnectionDenied(BitReader& reader);
std::optional<std::vector<ObjectUpdate>> readObjectUpdates(BitReader& reader,
                                                           const Vec3f& reference);
std::optional<PlayerInput> readPlayerInput(BitReader& reader);

}  // namespace obf2::net
