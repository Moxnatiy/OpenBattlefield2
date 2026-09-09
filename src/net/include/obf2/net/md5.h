#pragma once
// MD5 — needed for exactly one thing, the content check in BF2's protocol.
//
// The engine computes three hashes with it (`ChecksumContext`), and the server
// compares them on connection. Our own rather than a library's: the algorithm is
// short, and an extra dependency for a hundred lines does not pay for itself.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace obf2::net {

class Md5 {
 public:
  Md5();
  void update(std::span<const std::byte> data);
  std::array<std::byte, 16> finish();

 private:
  void transform(const std::byte* block);

  std::uint32_t state_[4];
  std::uint64_t length_ = 0;  // in bits
  std::byte buffer_[64]{};
  std::size_t pending_ = 0;
};

// A convenience wrapper for one piece of data.
std::array<std::byte, 16> md5(std::span<const std::byte> data);

}  // namespace obf2::net
