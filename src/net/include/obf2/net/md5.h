#pragma once
// MD5 — потрібен рівно для перевірки вмісту в протоколі BF2.
//
// Рушій рахує ним три хеші (`ChecksumContext`), і сервер звіряє їх при
// під'єднанні. Свій, а не з бібліотеки: алгоритм короткий, а зайва
// залежність заради ста рядків не окупається.
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
  std::uint64_t length_ = 0;  // у бітах
  std::byte buffer_[64]{};
  std::size_t pending_ = 0;
};

// Зручна обгортка для одного шматка даних.
std::array<std::byte, 16> md5(std::span<const std::byte> data);

}  // namespace obf2::net
