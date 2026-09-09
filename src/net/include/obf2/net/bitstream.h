#pragma once
// The bit stream of Refractor 2's network protocol.
//
// The bit layout: within each byte the low bits come **first**, and values longer
// than a byte continue into the next one. So writing the 3 bits `0b101` into an
// empty buffer gives the byte `0x05`, not `0xA0`.
//
// Sources: [Refractor-2-BitStream-Emulator](https://github.com/matthias-hoste/Refractor-2-BitStream-Emulator)
// (a working handshake implementation) and the BF2 1.5 Linux server's symbols,
// where the class is called `dice::hfe::io::BitStream` and has exactly this method set.
//
// The implementation is our own. The main difference from the original is
// **bounds checking**: packets arrive here off the network, and running past the buffer is unacceptable.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "obf2/core/math.h"

namespace obf2::net {

// The size of the service fields in bits — exactly how the game reads and writes them.
inline constexpr unsigned kBasicHeaderTypeBits = 4;
inline constexpr unsigned kBasicHeaderSubtypeBits = 8;
inline constexpr unsigned kExtendedHeaderTypeBits = 6;
inline constexpr unsigned kExtendedHeaderIdBits = 6;
inline constexpr unsigned kExtendedHeaderSequenceBits = 32;

// The packet type in the basic header.
enum class PacketType : std::uint32_t {
  ConnectionRequest = 1,
  ConnectionAccept = 2,
  ConnectionDenied = 3,
  ConnectionAcknowledge = 4,
  Disconnect = 5,
  PingRequest = 7,
  PingResponse = 8,
  Data = 15,
};

std::string_view packetTypeName(PacketType type);

struct BasicHeader {
  std::uint32_t type = 0;     // 4 bits, see PacketType
  std::uint32_t subtype = 0;  // 8 bits
};

struct ExtendedHeader {
  std::uint32_t type = 0;      // 6 bits
  std::uint32_t dataId = 0;    // 6 bits
  std::uint32_t sequence = 0;  // 32 bits
};

// A compressed vector: instead of three floats, the difference from a reference
// point is written at a precision chosen by the distance. The bit tables come
// from the Linux server's data (`BitStream::m_compressionVectorBitTable`), and
// the algorithm from the decompilation of `writeCompressedVector`.
//
// The level (2 bits) is chosen by the difference's length:
//
//   < 2^11  -> level 3, 12 bits per component
//   < 2^15  -> level 2, 16 bits
//   < 2^19  -> level 1, 20 bits
//   otherwise -> level 0: three raw floats, and that is an ABSOLUTE position
//
// In levels 1-3 a component is written as sign and magnitude: 1 sign bit plus
// (bits - 1) bits of magnitude.
inline constexpr std::uint32_t kCompressionVectorBitTable[4] = {32, 20, 16, 12};
inline constexpr std::uint32_t kHighCompressionVectorBitTable[4] = {32, 12, 10, 8};
inline constexpr std::uint32_t kCompressionVectorBitTable2[8] = {28, 24, 20, 16, 12, 10, 8, 0};

// How many bits go to the compression level.
inline constexpr unsigned kCompressionLevelBits = 2;

class BitReader {
 public:
  explicit BitReader(std::span<const std::byte> data) : data_(data) {}

  // nullopt means running past the buffer or asking for more than 32 bits.
  std::optional<std::uint32_t> readBits(unsigned bits);
  std::optional<bool> readBool();
  std::optional<std::uint8_t> readByte();
  // A fixed-length string; the zero bytes are trimmed.
  std::optional<std::string> readString(std::size_t length);
  bool readBytes(std::span<std::byte> destination);

  // precision is the same quantisation step as on the write.
  std::optional<Vec3f> readCompressedVector(const Vec3f& reference, float precision,
                                            const std::uint32_t (&table)[4] =
                                                kCompressionVectorBitTable);

  std::optional<BasicHeader> readBasicHeader();
  std::optional<ExtendedHeader> readExtendedHeader();

  bool skipBits(std::size_t bits);
  std::size_t bitPosition() const { return position_; }
  std::size_t bitsRemaining() const { return data_.size() * 8 - position_; }
  bool ok() const { return ok_; }

 private:
  std::span<const std::byte> data_;
  std::size_t position_ = 0;
  bool ok_ = true;
};

class BitWriter {
 public:
  explicit BitWriter(std::span<std::byte> buffer) : buffer_(buffer) {}

  // false means it did not fit; the stream stays in an error state afterwards.
  bool writeBits(std::uint32_t value, unsigned bits);
  bool writeBool(bool value);
  bool writeByte(std::uint8_t value);
  // The string is padded with zeroes to length bytes or truncated.
  bool writeString(std::string_view text, std::size_t length);
  bool writeBytes(std::span<const std::byte> bytes);

  bool writeCompressedVector(const Vec3f& value, const Vec3f& reference, float precision,
                             const std::uint32_t (&table)[4] = kCompressionVectorBitTable);

  bool writeBasicHeader(const BasicHeader& header);
  bool writeExtendedHeader(const ExtendedHeader& header);

  std::size_t bitPosition() const { return position_; }
  // How many whole bytes the written data takes (padded to a byte boundary).
  std::size_t byteSize() const { return (position_ + 7) / 8; }
  bool ok() const { return ok_; }

 private:
  std::span<std::byte> buffer_;
  std::size_t position_ = 0;
  bool ok_ = true;
};

// --- one layout description for both directions ---------------------
//
// A protocol's assembler and parser inevitably describe the same layout, and
// while there are two of them they diverge: forgetting a field in one is enough.
// So the layout is written **once** — as a function that takes a cursor and
// walks the fields — and the direction is chosen by the cursor itself.
//
// It looks like this:
//
//   template <typename Cursor>
//   bool serialize(Cursor& cursor, PlayerActions& actions) {
//     if (!cursor.bits(actions.number, 9)) return false;
//     ...
//   }
//
// and the same body works as `readPlayerActions` and as `writePlayerActions`.
// The engine does the same: every one of its events has a
// `serialize`/`deSerialize` pair, and the fields go the same way in both.
class ReadCursor {
 public:
  explicit ReadCursor(BitReader& reader) : reader_(reader) {}

  static constexpr bool reading = true;

  bool bits(std::uint32_t& value, unsigned width) {
    const auto got = reader_.readBits(width);
    if (!got) return false;
    value = *got;
    return true;
  }
  bool flag(bool& value) {
    std::uint32_t raw = 0;
    if (!bits(raw, 1)) return false;
    value = raw != 0;
    return true;
  }
  // A signed number: the sign bit, then the value. That is exactly how the engine
  // writes every signed integer — both in the MapInfo block and in the action stream.
  bool signedBits(std::int32_t& value, unsigned width) {
    std::uint32_t sign = 0;
    std::uint32_t magnitude = 0;
    if (!bits(sign, 1) || !bits(magnitude, width)) return false;
    value = static_cast<std::int32_t>(magnitude);
    if (sign) value = -value;
    return true;
  }

 private:
  BitReader& reader_;
};

class WriteCursor {
 public:
  explicit WriteCursor(BitWriter& writer) : writer_(writer) {}

  static constexpr bool reading = false;

  bool bits(std::uint32_t& value, unsigned width) { return writer_.writeBits(value, width); }
  bool flag(bool& value) { return writer_.writeBits(value ? 1 : 0, 1); }
  bool signedBits(std::int32_t& value, unsigned width) {
    const bool negative = value < 0;
    const auto magnitude = static_cast<std::uint32_t>(negative ? -value : value);
    return writer_.writeBits(negative ? 1 : 0, 1) && writer_.writeBits(magnitude, width);
  }

 private:
  BitWriter& writer_;
};

}  // namespace obf2::net
