// A projectile's ghost record (obf2/net/projectile_state.h): the 5-bit mask, the
// position before the rotation, the detonation's branch and the armor's range —
// written in the order OpenBF2's `getNetUpdate` writes them and read back.
#include <cmath>
#include <cstddef>
#include <vector>

#include "check.h"
#include "obf2/net/projectile_state.h"

using namespace obf2::net::bf2;
using obf2::net::BitReader;
using obf2::net::BitWriter;
using obf2::net::Quatf;
using obf2::Vec3f;

namespace {

bool near(float a, float b, float eps = 0.006f) { return std::fabs(a - b) <= eps; }

// In flight: position (0x2) and rotation (0x1) — 5 + 2 + 3×12 + 4×12 = 91 bits
// when the position lies within 2^11 hundredths of the reference.
void testInFlight() {
  std::vector<std::byte> buffer(32);
  BitWriter writer(buffer);
  const Vec3f reference{100.0f, 20.0f, -50.0f};
  writer.writeBits(0x3, 5);
  writer.writeCompressedVector(Vec3f{112.5f, 21.25f, -60.0f}, reference, 0.01f);
  writer.writeUnitQuaternion(Quatf{0.0f, 0.7071068f, 0.0f, 0.7071068f}, 12);
  CHECK_EQ(writer.bitPosition(), std::size_t{91});

  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readProjectileState(reader, reference, std::nullopt);
  CHECK(state && state->complete);
  CHECK_EQ(reader.bitPosition(), std::size_t{91});
  CHECK(near(state->position->x, 112.5f) && near(state->position->y, 21.25f) &&
        near(state->position->z, -60.0f));
  CHECK(near(state->rotation->y, 0.7071f, 0.002f) && near(state->rotation->w, 0.7071f, 0.002f));
  CHECK(!state->detonated);
}

// Detonated on a target: the flag, sign + 15, a high-compressed vector against
// zero, the target flag and its 16-bit id.
void testDetonation() {
  std::vector<std::byte> buffer(32);
  BitWriter writer(buffer);
  writer.writeBits(0x4, 5);
  writer.writeBits(1, 1);      // detonated
  writer.writeBits(1, 1);      // negative
  writer.writeBits(73, 15);    // -73
  writer.writeHighCompressedVector(Vec3f{0.0f, 1.0f, -0.5f}, Vec3f{}, 0.01f);
  writer.writeBits(1, 1);      // has a target
  writer.writeBits(1825, 16);  // its network id
  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readProjectileState(reader, Vec3f{}, std::nullopt);
  CHECK(state && state->complete);
  CHECK_EQ(reader.bitPosition(), writer.bitPosition());
  CHECK(*state->detonated);
  CHECK_EQ(*state->detonationNumber, -73);
  CHECK(near(state->detonationVector->y, 1.0f) && near(state->detonationVector->z, -0.5f));
  CHECK_EQ(*state->target, std::uint16_t{1825});
}

// A detonation flag of 0 carries nothing more.
void testNotDetonated() {
  std::vector<std::byte> buffer(4);
  BitWriter writer(buffer);
  writer.writeBits(0x4, 5);
  writer.writeBits(0, 1);
  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readProjectileState(reader, Vec3f{}, std::nullopt);
  CHECK(state && state->complete);
  CHECK_EQ(reader.bitPosition(), std::size_t{6});
  CHECK(!*state->detonated && !state->detonationNumber);
}

// Hit points: `0x4f9a10(0, max + wreck)` less the wreck. A mine is 1000 and the
// default 10: 0..1010 is 10 bits.
void testHitPoints() {
  std::vector<std::byte> buffer(4);
  BitWriter writer(buffer);
  writer.writeBits(0x10, 5);
  writer.writeBits(990 + 10, 10);
  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readProjectileState(reader, Vec3f{}, ProjectileArmor{1000, 10});
  CHECK(state && state->complete);
  CHECK_EQ(reader.bitPosition(), std::size_t{15});
  CHECK_EQ(*state->hitPoints, 990);

  // No armor: the client skips the field — nothing more is read.
  BitReader bare(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto none = readProjectileState(bare, Vec3f{}, std::nullopt);
  CHECK(none && none->complete && !none->hitPoints);
  CHECK_EQ(bare.bitPosition(), std::size_t{5});

  // Not known whether it has armor: what came before is kept, the record is not whole.
  BitReader unknown(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto partial = readProjectileState(unknown, Vec3f{}, std::nullopt, false);
  CHECK(partial && !partial->complete);
}

}  // namespace

TEST_MAIN({
  testInFlight();
  testDetonation();
  testNotDetonated();
  testHitPoints();
})
