#include "obf2/net/bitstream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace obf2::net {
namespace {

std::uint32_t maskOf(unsigned bits) {
  return bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
}

}  // namespace

namespace {

constexpr float kPi = 3.1415927410125732f;
constexpr float kFloatEpsilon = 1.1920928955078125e-07f;

float floatOf(std::uint32_t raw) {
  float value = 0.0f;
  std::memcpy(&value, &raw, sizeof value);
  return value;
}

std::uint32_t rawOf(float value) {
  std::uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof raw);
  return raw;
}

// The quantiser `readNormalAngle` and `readQuatComponent` share, without the
// pi: [-1, 1] from `bits` (or a raw float at 32); `deadZone` makes a value
// within one step of zero zero.
std::optional<float> readUnit(BitReader& reader, unsigned bits, bool deadZone) {
  float value = 0.0f;
  const auto raw = reader.readBits(bits);
  if (!raw) return std::nullopt;
  if (bits == 32) {
    value = floatOf(*raw);
  } else {
    const auto top = static_cast<float>((1u << bits) - 1u);
    value = (static_cast<float>(*raw) / top) * 2.0f - 1.0f;
  }
  if (deadZone) {
    const float top = bits == 32 ? 4294967295.0f : static_cast<float>((1u << bits) - 1u);
    if (2.0f / top >= std::fabs(value)) value = 0.0f;
  }
  return value;
}

// The writers' side: (v + 1) / 2 clamped from below (and from above when
// `clampTop`), times 2^bits - 1, truncated; a raw float at 32.
bool writeUnit(BitWriter& writer, float value, unsigned bits, bool clampTop) {
  if (bits == 32) return writer.writeBits(rawOf(value), 32);
  float unit = (value + 1.0f) * 0.5f;
  if (unit < 0.0f) unit = 0.0f;
  if (clampTop && unit > 1.0f) unit = 1.0f;
  const auto top = static_cast<float>((1u << bits) - 1u);
  return writer.writeBits(static_cast<std::uint32_t>(unit * top), bits);
}

}  // namespace

std::string_view packetTypeName(PacketType type) {
  switch (type) {
    case PacketType::ConnectionRequest: return "ConnectionRequest";
    case PacketType::ConnectionAccept: return "ConnectionAccept";
    case PacketType::ConnectionDenied: return "ConnectionDenied";
    case PacketType::ConnectionAcknowledge: return "ConnectionAcknowledge";
    case PacketType::Disconnect: return "Disconnect";
    case PacketType::PingRequest: return "PingRequest";
    case PacketType::PingResponse: return "PingResponse";
    case PacketType::Data: return "Data";
  }
  return "?";
}

// --- BitReader ---------------------------------------------------------------

std::optional<std::uint32_t> BitReader::readBits(unsigned bits) {
  if (!ok_) return std::nullopt;
  if (bits == 0) return 0u;
  if (bits > 32 || bits > bitsRemaining()) {
    ok_ = false;
    return std::nullopt;
  }

  std::uint32_t result = 0;
  unsigned collected = 0;
  unsigned remaining = bits;
  std::size_t at = position_;

  // We read in chunks up to the end of the current byte: low bits first.
  while (remaining > 0) {
    const unsigned bitInByte = static_cast<unsigned>(at & 7u);
    const unsigned availableInByte = 8u - bitInByte;
    const unsigned take = std::min(remaining, availableInByte);

    const auto byte = static_cast<std::uint32_t>(data_[at >> 3]);
    const std::uint32_t chunk = (byte >> bitInByte) & maskOf(take);
    result |= chunk << collected;

    collected += take;
    remaining -= take;
    at += take;
  }

  position_ += bits;
  return result;
}

std::optional<bool> BitReader::readBool() {
  const auto value = readBits(1);
  if (!value) return std::nullopt;
  return *value != 0;
}

std::optional<std::uint8_t> BitReader::readByte() {
  const auto value = readBits(8);
  if (!value) return std::nullopt;
  return static_cast<std::uint8_t>(*value);
}

std::optional<std::string> BitReader::readString(std::size_t length) {
  std::string text;
  text.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    const auto byte = readByte();
    if (!byte) return std::nullopt;
    if (*byte != 0) text.push_back(static_cast<char>(*byte));
  }
  return text;
}

bool BitReader::readBytes(std::span<std::byte> destination) {
  for (std::byte& out : destination) {
    const auto byte = readByte();
    if (!byte) return false;
    out = static_cast<std::byte>(*byte);
  }
  return true;
}

namespace {

// The compression level by the difference's length. The thresholds are 2^(bits-1)
// of the same table, that is the largest number that still fits the magnitude field.
std::uint32_t compressionLevelFor(float scaledDistance, const std::uint32_t (&table)[4]) {
  auto threshold = [&](std::size_t index) {
    return static_cast<float>(1u << ((table[index] - 1u) & 31u));
  };
  if (scaledDistance < threshold(3)) return 3;
  if (scaledDistance < threshold(2)) return 2;
  if (scaledDistance < threshold(1)) return 1;
  return 0;
}

float bitsToFloat(std::uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::uint32_t floatToBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

}  // namespace

bool BitWriter::writeCompressedVector(const Vec3f& value, const Vec3f& reference, float precision,
                                      const std::uint32_t (&table)[4]) {
  if (precision == 0.0f) {
    ok_ = false;
    return false;
  }
  const float inverse = 1.0f / precision;
  const Vec3f delta = value - reference;
  const std::uint32_t level = compressionLevelFor(length(delta) * inverse, table);

  if (!writeBits(level, kCompressionLevelBits)) return false;

  // Level 0 is a refusal to compress: an ABSOLUTE position is written as raw
  // floats, not a difference.
  if (level == 0) {
    return writeBits(floatToBits(value.x), 32) && writeBits(floatToBits(value.y), 32) &&
           writeBits(floatToBits(value.z), 32);
  }

  const unsigned magnitudeBits = table[level] - 1u;
  const float components[3] = {delta.x * inverse, delta.y * inverse, delta.z * inverse};
  for (const float component : components) {
    // Truncation towards zero, as in the original: (int)(-1.7f) is -1, not -2.
    const int quantized = static_cast<int>(component);
    const bool negative = quantized < 0;
    if (!writeBool(negative)) return false;
    const auto magnitude = static_cast<std::uint32_t>(negative ? -quantized : quantized);
    if (!writeBits(magnitude, magnitudeBits)) return false;
  }
  return true;
}

bool BitWriter::writeWideVector(const Vec3f& value, const Vec3f& base, float precision) {
  if (precision == 0.0f) {
    ok_ = false;
    return false;
  }
  const float inverse = 1.0f / precision;
  const Vec3f delta = value - base;
  const float distance = length(delta);
  // Shorter than FLT_EPSILON: level 7 and nothing else.
  if (distance < 1.1920928955078125e-07f) return writeBits(7, 3);
  const float scaled = distance * inverse;
  std::uint32_t level = 0;
  for (std::uint32_t i = 6; i >= 1; --i) {
    if (static_cast<float>(1u << (kCompressionVectorBitTable2[i] - 1u)) > scaled) {
      level = i;
      break;
    }
  }
  if (!writeBits(level, 3)) return false;
  if (level == 0) {
    return writeBits(floatToBits(value.x), 32) && writeBits(floatToBits(value.y), 32) &&
           writeBits(floatToBits(value.z), 32);
  }
  const unsigned magnitudeBits = kCompressionVectorBitTable2[level] - 1u;
  for (const float component : {delta.x * inverse, delta.y * inverse, delta.z * inverse}) {
    const int quantized = static_cast<int>(component);
    const bool negative = quantized < 0;
    if (!writeBool(negative)) return false;
    if (!writeBits(static_cast<std::uint32_t>(negative ? -quantized : quantized), magnitudeBits)) {
      return false;
    }
  }
  return true;
}

bool BitWriter::writeNormalAngle(float angle, unsigned bits) {
  return writeUnit(*this, angle / kPi, bits, false);
}

bool BitWriter::writeNormalVector(const Vec3f& normal, unsigned bits) {
  const float azimuth = std::atan2(normal.y, normal.x);
  const float z = std::clamp(normal.z, -1.0f, 1.0f);
  const float elevation = std::acos(z);
  // An angle beyond pi is the engine's error 4: nothing written.
  if (std::fabs(azimuth) > kPi || std::fabs(elevation) > kPi) {
    ok_ = false;
    return false;
  }
  return writeNormalAngle(azimuth, bits) && writeNormalAngle(elevation, bits);
}

bool BitWriter::writeUnitQuaternion3Comp(const Quatf& rotation, unsigned bits) {
  Quatf v = rotation;
  if (std::fabs(v.x) > 1.0f || std::fabs(v.y) > 1.0f || std::fabs(v.z) > 1.0f) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
    if (len < kFloatEpsilon) {
      v = Quatf{};
    } else {
      v = Quatf{v.x / len, v.y / len, v.z / len, v.w / len};
    }
  }
  return writeBits(v.w < 0.0f ? 1u : 0u, 1) && writeUnit(*this, v.x, bits, false) &&
         writeUnit(*this, v.y, bits, false) && writeUnit(*this, v.z, bits, false);
}

bool BitWriter::writeUnitQuaternion(const Quatf& rotation, unsigned bits) {
  Quatf v = rotation;
  if (std::fabs(v.x) > 1.0f || std::fabs(v.y) > 1.0f || std::fabs(v.z) > 1.0f) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
    if (len >= kFloatEpsilon) {
      v = Quatf{v.x / len, v.y / len, v.z / len, v.w / len};
    } else {
      v = Quatf{};
    }
  }
  return writeUnit(*this, v.w, bits, true) && writeUnit(*this, v.x, bits, true) &&
         writeUnit(*this, v.y, bits, true) && writeUnit(*this, v.z, bits, true);
}

std::optional<Vec3f> BitReader::readCompressedVector(const Vec3f& reference, float precision,
                                                     const std::uint32_t (&table)[4]) {
  const auto level = readBits(kCompressionLevelBits);
  if (!level) return std::nullopt;

  if (*level == 0) {
    const auto x = readBits(32);
    const auto y = readBits(32);
    const auto z = readBits(32);
    if (!x || !y || !z) return std::nullopt;
    return Vec3f{bitsToFloat(*x), bitsToFloat(*y), bitsToFloat(*z)};
  }

  const unsigned magnitudeBits = table[*level] - 1u;
  float components[3] = {0.0f, 0.0f, 0.0f};
  for (float& component : components) {
    const auto negative = readBool();
    const auto magnitude = readBits(magnitudeBits);
    if (!negative || !magnitude) return std::nullopt;
    const float value = static_cast<float>(*magnitude) * precision;
    component = *negative ? -value : value;
  }
  return Vec3f{reference.x + components[0], reference.y + components[1],
               reference.z + components[2]};
}

std::optional<Vec3f> BitReader::readWideVector(const Vec3f& base, float precision) {
  const auto level = readBits(3);
  if (!level) return std::nullopt;
  if (*level == 0) {
    const auto x = readBits(32);
    const auto y = readBits(32);
    const auto z = readBits(32);
    if (!x || !y || !z) return std::nullopt;
    return Vec3f{bitsToFloat(*x), bitsToFloat(*y), bitsToFloat(*z)};
  }
  if (*level == 7) return base;

  const unsigned magnitudeBits = kCompressionVectorBitTable2[*level] - 1u;
  float components[3] = {0.0f, 0.0f, 0.0f};
  for (float& component : components) {
    const auto negative = readBool();
    const auto magnitude = readBits(magnitudeBits);
    if (!negative || !magnitude) return std::nullopt;
    const float value = static_cast<float>(*magnitude) * precision;
    component = *negative ? -value : value;
  }
  return Vec3f{base.x + components[0], base.y + components[1], base.z + components[2]};
}


std::optional<float> BitReader::readNormalAngle(unsigned bits) {
  const auto unit = readUnit(*this, bits, true);
  if (!unit) return std::nullopt;
  return *unit * kPi;
}

std::optional<Vec3f> BitReader::readNormalVector(unsigned bits) {
  const auto azimuth = readNormalAngle(bits);
  const auto elevation = readNormalAngle(bits);
  if (!azimuth || !elevation) return std::nullopt;
  return Vec3f{std::cos(*azimuth) * std::sin(*elevation), std::sin(*azimuth) * std::sin(*elevation),
               std::cos(*elevation)};
}

std::optional<Quatf> BitReader::readUnitQuaternion3Comp(unsigned bits) {
  const auto sign = readBits(1);
  const auto x = readUnit(*this, bits, true);
  const auto y = readUnit(*this, bits, true);
  const auto z = readUnit(*this, bits, true);
  if (!sign || !x || !y || !z) return std::nullopt;
  float sum = *x * *x + *y * *y + *z * *z;
  if (sum >= 1.0f) sum = 1.0f;
  Quatf out{*x, *y, *z, std::sqrt(1.0f - sum)};
  if (*sign) out.w = -out.w;
  return out;
}

std::optional<Quatf> BitReader::readUnitQuaternion(unsigned bits) {
  const auto w = readUnit(*this, bits, false);
  const auto x = readUnit(*this, bits, false);
  const auto y = readUnit(*this, bits, false);
  const auto z = readUnit(*this, bits, false);
  if (!w || !x || !y || !z) return std::nullopt;
  Quatf out{*x, *y, *z, *w};
  const float length = std::sqrt(out.w * out.w + out.x * out.x + out.y * out.y + out.z * out.z);
  if (length >= kFloatEpsilon) {
    out.w /= length;
    out.x /= length;
    out.y /= length;
    out.z /= length;
  } else {
    out = Quatf{};
  }
  return out;
}

std::optional<BasicHeader> BitReader::readBasicHeader() {
  const auto type = readBits(kBasicHeaderTypeBits);
  const auto subtype = readBits(kBasicHeaderSubtypeBits);
  if (!type || !subtype) return std::nullopt;
  return BasicHeader{*type, *subtype};
}

std::optional<ExtendedHeader> BitReader::readExtendedHeader() {
  const auto type = readBits(kExtendedHeaderTypeBits);
  const auto dataId = readBits(kExtendedHeaderIdBits);
  const auto sequence = readBits(kExtendedHeaderSequenceBits);
  if (!type || !dataId || !sequence) return std::nullopt;
  return ExtendedHeader{*type, *dataId, *sequence};
}

bool BitReader::skipBits(std::size_t bits) {
  if (!ok_ || bits > bitsRemaining()) {
    ok_ = false;
    return false;
  }
  position_ += bits;
  return true;
}

// --- BitWriter ---------------------------------------------------------------

bool BitWriter::writeBits(std::uint32_t value, unsigned bits) {
  if (!ok_) return false;
  if (bits == 0) return true;
  if (bits > 32 || position_ + bits > buffer_.size() * 8) {
    ok_ = false;
    return false;
  }

  value &= maskOf(bits);
  unsigned remaining = bits;
  std::size_t at = position_;

  while (remaining > 0) {
    const unsigned bitInByte = static_cast<unsigned>(at & 7u);
    const unsigned availableInByte = 8u - bitInByte;
    const unsigned put = std::min(remaining, availableInByte);

    // We clear exactly the bits we write and leave the neighbours alone: the tail
    // of the previous value may land in the same byte.
    const std::uint32_t clearMask = ~(maskOf(put) << bitInByte);
    auto byte = static_cast<std::uint32_t>(buffer_[at >> 3]);
    byte &= clearMask;
    byte |= (value & maskOf(put)) << bitInByte;
    buffer_[at >> 3] = static_cast<std::byte>(byte & 0xFFu);

    value >>= put;
    remaining -= put;
    at += put;
  }

  position_ += bits;
  return true;
}

bool BitWriter::writeBool(bool value) { return writeBits(value ? 1u : 0u, 1); }

bool BitWriter::writeByte(std::uint8_t value) { return writeBits(value, 8); }

bool BitWriter::writeString(std::string_view text, std::size_t length) {
  for (std::size_t i = 0; i < length; ++i) {
    const std::uint8_t byte =
        i < text.size() ? static_cast<std::uint8_t>(text[i]) : std::uint8_t{0};
    if (!writeByte(byte)) return false;
  }
  return true;
}

bool BitWriter::writeBytes(std::span<const std::byte> bytes) {
  for (const std::byte byte : bytes) {
    if (!writeByte(static_cast<std::uint8_t>(byte))) return false;
  }
  return true;
}

bool BitWriter::writeBasicHeader(const BasicHeader& header) {
  return writeBits(header.type, kBasicHeaderTypeBits) &&
         writeBits(header.subtype, kBasicHeaderSubtypeBits);
}

bool BitWriter::writeExtendedHeader(const ExtendedHeader& header) {
  return writeBits(header.type, kExtendedHeaderTypeBits) &&
         writeBits(header.dataId, kExtendedHeaderIdBits) &&
         writeBits(header.sequence, kExtendedHeaderSequenceBits);
}

}  // namespace obf2::net
