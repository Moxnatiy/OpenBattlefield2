// A control point's ghost record (obf2/net/control_point_state.h): the 7-bit mask,
// the sign-and-three-bit fields and the two take-over units.
#include <cmath>
#include <cstddef>
#include <vector>

#include "check.h"
#include "obf2/net/control_point_state.h"

using namespace obf2::net::bf2;
using obf2::net::BitReader;
using obf2::net::BitWriter;

namespace {

void signed3(BitWriter& writer, int value) {
  writer.writeBits(value < 0 ? 1u : 0u, 1);
  writer.writeBits(static_cast<std::uint32_t>(value < 0 ? -value : value), 3);
}
// `writeUnit16` (OpenBF2): ((v / 1.25 + 1) / 2) × 65535, truncated.
void unit16(BitWriter& writer, float value) {
  double t = (static_cast<double>(value) / 1.25 + 1.0) * 0.5;
  t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
  writer.writeBits(static_cast<std::uint32_t>(static_cast<int>(t * 65535.0)), 16);
}

// The full record is 60 bits, as on the live server.
void testFullRecord() {
  std::vector<std::byte> buffer(16);
  BitWriter writer(buffer);
  writer.writeBits(0x7F, 7);
  signed3(writer, 2);           // team
  writer.writeBits(0, 1);       // not neutral
  unit16(writer, 1.0f);         // take-over counter
  unit16(writer, -0.05f);       // per second
  signed3(writer, 1);           // team 1 may capture
  signed3(writer, 0);           // team 2 may not
  signed3(writer, 2);           // flag team
  signed3(writer, -1);          // capturing state
  CHECK_EQ(writer.bitPosition(), std::size_t{60});
  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readControlPointState(reader);
  CHECK(state && state->complete);
  CHECK_EQ(reader.bitPosition(), std::size_t{60});
  CHECK(state->team == 2);
  CHECK(state->neutral == false);
  CHECK(std::fabs(*state->takeOverCounter - 1.0f) < 1e-3f);
  CHECK(std::fabs(*state->takeOverPerSecond + 0.05f) < 1e-3f);
  CHECK(state->captureByTeam1 == true);
  CHECK(state->captureByTeam2 == false);
  CHECK(state->flagTeam == 2);
  CHECK(state->capturingState == -1);
}

// The take-over alone: 39 bits; zero falls in the dead zone.
void testTakeOverOnly() {
  std::vector<std::byte> buffer(8);
  BitWriter writer(buffer);
  writer.writeBits(0x04, 7);
  unit16(writer, 0.0f);
  unit16(writer, 0.0f);
  CHECK_EQ(writer.bitPosition(), std::size_t{39});
  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readControlPointState(reader);
  CHECK(state && state->complete);
  CHECK(std::fabs(*state->takeOverCounter) < 1e-4f);
  CHECK(!state->team);
}

}  // namespace

TEST_MAIN({
  testFullRecord();
  testTakeOverOnly();
})
