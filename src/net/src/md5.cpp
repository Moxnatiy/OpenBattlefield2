#include "obf2/net/md5.h"

#include <cstring>

namespace obf2::net {
namespace {

constexpr std::uint32_t kSine[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr int kShift[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                            5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

std::uint32_t rotate(std::uint32_t value, int by) {
  return (value << by) | (value >> (32 - by));
}

}  // namespace

Md5::Md5() : state_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {}

void Md5::transform(const std::byte* block) {
  std::uint32_t words[16];
  for (int i = 0; i < 16; ++i) {
    words[i] = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[i * 4])) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[i * 4 + 1])) << 8) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[i * 4 + 2])) << 16) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[i * 4 + 3])) << 24);
  }

  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  for (int i = 0; i < 64; ++i) {
    std::uint32_t mixed = 0;
    int index = 0;
    if (i < 16) {
      mixed = (b & c) | (~b & d);
      index = i;
    } else if (i < 32) {
      mixed = (d & b) | (~d & c);
      index = (5 * i + 1) % 16;
    } else if (i < 48) {
      mixed = b ^ c ^ d;
      index = (3 * i + 5) % 16;
    } else {
      mixed = c ^ (b | ~d);
      index = (7 * i) % 16;
    }
    const std::uint32_t next = d;
    d = c;
    c = b;
    b = b + rotate(a + mixed + kSine[i] + words[index], kShift[i]);
    a = next;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
}

void Md5::update(std::span<const std::byte> data) {
  length_ += static_cast<std::uint64_t>(data.size()) * 8;
  std::size_t at = 0;
  while (at < data.size()) {
    const std::size_t take = std::min(sizeof(buffer_) - pending_, data.size() - at);
    std::memcpy(buffer_ + pending_, data.data() + at, take);
    pending_ += take;
    at += take;
    if (pending_ == sizeof(buffer_)) {
      transform(buffer_);
      pending_ = 0;
    }
  }
}

std::array<std::byte, 16> Md5::finish() {
  const std::uint64_t bits = length_;
  const std::byte one{0x80};
  update(std::span<const std::byte>(&one, 1));
  const std::byte zero{0};
  while (pending_ != 56) update(std::span<const std::byte>(&zero, 1));

  // We append the length ourselves: `update` would increment the counter again.
  for (int i = 0; i < 8; ++i) {
    buffer_[56 + i] = static_cast<std::byte>((bits >> (i * 8)) & 0xFF);
  }
  transform(buffer_);

  std::array<std::byte, 16> out{};
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 4; ++k) {
      out[i * 4 + k] = static_cast<std::byte>((state_[i] >> (k * 8)) & 0xFF);
    }
  }
  return out;
}

std::array<std::byte, 16> md5(std::span<const std::byte> data) {
  Md5 context;
  context.update(data);
  return context.finish();
}

}  // namespace obf2::net
