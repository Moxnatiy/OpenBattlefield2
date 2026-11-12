#include "obf2/engine/bink.h"

namespace obf2::engine {
namespace {

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
         static_cast<std::uint32_t>(bytes[offset + 2]) << 16 |
         static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}

}  // namespace

double BinkHeader::seconds() const {
  if (rateDividend == 0) return 0.0;
  return static_cast<double>(frames) * static_cast<double>(rateDivider) /
         static_cast<double>(rateDividend);
}

std::optional<BinkHeader> readBinkHeader(std::span<const std::byte> head) {
  if (head.size() < kBinkHeaderBytes) return std::nullopt;
  if (head[0] != std::byte{'B'} || head[1] != std::byte{'I'} || head[2] != std::byte{'K'}) {
    return std::nullopt;
  }
  BinkHeader header;
  header.version = static_cast<char>(head[3]);
  header.fileSize = word(head, 0x04) + 8;
  header.frames = word(head, 0x08);
  header.largestFrame = word(head, 0x0C);
  header.width = word(head, 0x14);
  header.height = word(head, 0x18);
  header.rateDividend = word(head, 0x1C);
  header.rateDivider = word(head, 0x20);
  return header;
}

}  // namespace obf2::engine
