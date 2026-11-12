#include <array>
#include <cstddef>
#include <cstdint>

#include "check.h"
#include "obf2/engine/bink.h"

using namespace obf2;

namespace {

void put(std::array<std::byte, engine::kBinkHeaderBytes>& bytes, std::size_t offset,
         std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::byte>((value >> (8 * i)) & 0xff);
  }
}

// The head of `mods/bf2/Movies/EA.bik`, field by field as the file has it:
// 96 frames at 2997/100, 720 x 486, 1 247 548 bytes on disk.
std::array<std::byte, engine::kBinkHeaderBytes> eaHead() {
  std::array<std::byte, engine::kBinkHeaderBytes> bytes{};
  bytes[0] = std::byte{'B'};
  bytes[1] = std::byte{'I'};
  bytes[2] = std::byte{'K'};
  bytes[3] = std::byte{'i'};
  put(bytes, 0x04, 1247548 - 8);
  put(bytes, 0x08, 96);
  put(bytes, 0x10, 96);
  put(bytes, 0x14, 720);
  put(bytes, 0x18, 486);
  put(bytes, 0x1C, 2997);
  put(bytes, 0x20, 100);
  return bytes;
}

}  // namespace

static void testReadsTheHead() {
  const auto bytes = eaHead();
  const auto header = engine::readBinkHeader(bytes);
  CHECK(header.has_value());
  CHECK_EQ(header->version, 'i');
  CHECK_EQ(header->fileSize, 1247548u);
  CHECK_EQ(header->frames, 96u);
  CHECK_EQ(header->width, 720u);
  CHECK_EQ(header->height, 486u);
  // 96 × 100 / 2997 = 3.2032 s.
  CHECK(header->seconds() > 3.203 && header->seconds() < 3.204);
}

static void testRefusesWhatIsNotBink() {
  auto bytes = eaHead();
  bytes[0] = std::byte{'R'};
  CHECK(!engine::readBinkHeader(bytes).has_value());
  const auto good = eaHead();
  CHECK(!engine::readBinkHeader(std::span<const std::byte>(good.data(), 20)).has_value());
}

static void testZeroRateIsZeroSeconds() {
  auto bytes = eaHead();
  put(bytes, 0x1C, 0);
  const auto header = engine::readBinkHeader(bytes);
  CHECK(header.has_value());
  CHECK(header->seconds() == 0.0);
}

TEST_MAIN({
  testReadsTheHead();
  testRefusesWhatIsNotBink();
  testZeroRateIsZeroSeconds();
})
