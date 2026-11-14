#pragma once
// Chat strings as the server sends them — `StringManager` (OpenBF2
// Game/Common/StringManager.cpp) over `StringBlockEvent` (type 15).
//
// A string is announced by a block event with its total size (text plus a
// 12-byte header), then arrives in chunks of at most 63 bytes. The header trails
// the text: the channel (int32), the message type (int32), the sender's player
// id (byte; -1 sent as 0), a byte the server reads as the target, and the flags
// byte (on the player channel the target player). All little-endian.
//
// `newStringDataBlock` refuses a second announcement while one is assembling;
// `addStringDataChunk` refuses a chunk that overruns the announced size and
// completes the string at exactly its size.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace obf2::net::bf2 {

struct ChatString {
  int channel = 0;  // `TargetChannel`
  int type = 0;     // `ChannelMessageType`: 0/1 game, 2 player, 3 squad radio
  int sender = 0;
  int target = 0;
  int flags = 0;
  std::string text;
};

class ChatAssembler {
 public:
  // `newStringDataBlock`: false when one is already assembling.
  bool begin(std::uint32_t size);
  // `addStringDataChunk`: false without an announcement or past its size; a
  // completed string comes back.
  std::optional<ChatString> add(const std::vector<std::uint8_t>& chunk, bool* refused = nullptr);
  bool assembling() const { return assembling_; }

 private:
  bool assembling_ = false;
  std::uint32_t size_ = 0;
  std::vector<std::uint8_t> data_;
};

}  // namespace obf2::net::bf2
