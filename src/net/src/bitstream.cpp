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

  // Читаємо шматками до кінця поточного байта: молодші біти першими.
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

// Рівень стиснення за довжиною різниці. Пороги — це 2^(біти-1) тієї самої
// таблиці, тобто найбільше число, яке ще влазить у поле модуля.
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

  // Рівень 0 — це відмова від стиснення: пишеться АБСОЛЮТНА позиція
  // сирими float-ами, а не різниця.
  if (level == 0) {
    return writeBits(floatToBits(value.x), 32) && writeBits(floatToBits(value.y), 32) &&
           writeBits(floatToBits(value.z), 32);
  }

  const unsigned magnitudeBits = table[level] - 1u;
  const float components[3] = {delta.x * inverse, delta.y * inverse, delta.z * inverse};
  for (const float component : components) {
    // Обрізання до нуля, як у оригіналі: (int)(-1.7f) це -1, а не -2.
    const int quantized = static_cast<int>(component);
    const bool negative = quantized < 0;
    if (!writeBool(negative)) return false;
    const auto magnitude = static_cast<std::uint32_t>(negative ? -quantized : quantized);
    if (!writeBits(magnitude, magnitudeBits)) return false;
  }
  return true;
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

    // Чистимо саме ті біти, які пишемо, і не чіпаємо сусідні: у той самий
    // байт може потрапити хвіст попереднього значення.
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
