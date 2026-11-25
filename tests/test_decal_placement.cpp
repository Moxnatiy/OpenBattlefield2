#include "check.h"
#include <cmath>
#include <cstdint>
#include <vector>
#include "obf2/app/decal_placement.h"

using namespace obf2;

// Checks against RendDX9.dll 0x1009f860, 0x101ae980 and 0x101ae450
// (docs/functions/m4-effects.md, "Decal size, rotation and atlas frame").
// Expected values come from the recovered arithmetic, written out here
// independently of decal_placement.h.
namespace {
std::uint32_t schrageStep(std::uint32_t s) {
  // The generator's own two-step Schrage form (0x1009f860..0x1009f8ec).
  auto step = [](std::int32_t x, std::int32_t m) {
    std::int32_t lo = (x & 0xffff) * m;
    std::int32_t hi = (x >> 16) * m + (lo >> 16);
    // 32-bit wraparound as the `lea` at 0x1009f897 computes it.
    const std::uint32_t sum = (static_cast<std::uint32_t>(hi & 0x7fff) << 16) +
        static_cast<std::uint32_t>(hi >> 15) + static_cast<std::uint32_t>(lo & 0xffff) -
        0x7fffffffu;
    std::int32_t r = static_cast<std::int32_t>(sum);
    if (r < 0) r += 0x7fffffff;
    return r;
  };
  return static_cast<std::uint32_t>(step(step(static_cast<std::int32_t>(s), 0x5e30), 0x661f));
}
}  // namespace

int main() {
  // The 64-bit modular product equals the binary's two Schrage steps.
  app::DecalRandom random;
  std::uint32_t reference = 0x759dc150u;
  for (int i = 0; i < 10000; ++i) {
    reference = schrageStep(reference);
    CHECK(random.next() == reference);
    CHECK(reference >= 1u && reference <= 2147483646u);
  }

  // Signed unit range (-1, 1], and both ends of the scaled integer.
  random = {};
  for (int i = 0; i < 10000; ++i) {
    const float r = random.signedUnit();
    CHECK(r > -1.0f && r <= 1.0f);
  }

  // decal_s_rock: size 0.033 +- 0.005, rotation 0 +- 360 degrees, atlas 2x2,
  // randomized start (Effects/decals/small/decal_s_rock.tweak).
  app::DecalTemplateValues rock;
  rock.size = 0.033f;
  rock.randomSize = 0.005f;
  rock.randomRotation = 360.0f;
  rock.animationEnable = true;
  rock.randomizedStartFrame = true;
  rock.frameCount = 4;
  rock.frameCountX = 2;
  random = {};
  int frames[4] = {};
  float minExtent = 1, maxExtent = 0;
  const int trials = 40000;
  for (int i = 0; i < trials; ++i) {
    app::DecalRandom copy = random;
    const auto placement = app::placeDecal(rock, random);
    // Draw order: colour, frame, rotation, size (0x101aea9d, 0x101aeb75,
    // 0x101ae475, 0x101ae4c8).
    const float colour = copy.signedUnit();
    const float frameValue = static_cast<float>(copy.scaled()) * 3.0f * 5.9604644775390625e-8f;
    const float degrees = copy.signedUnit() * 360.0f;
    const float extent = 0.033f + copy.signedUnit() * 0.005f;
    CHECK(placement.brightnessUnit == colour);
    CHECK(std::abs(placement.radians - degrees * 0.01745329238474369f) < 1e-6f);
    CHECK(placement.halfExtent == extent);
    CHECK(placement.frame == static_cast<std::uint32_t>(std::nearbyint(frameValue)));
    CHECK(placement.frame < 4u);
    ++frames[placement.frame];
    minExtent = std::min(minExtent, placement.halfExtent);
    maxExtent = std::max(maxExtent, placement.halfExtent);
  }
  CHECK(minExtent >= 0.028f - 1e-6f && maxExtent <= 0.038f + 1e-6f);
  // round(U(0,3]): the end frames take half the share of the middle ones.
  CHECK(frames[0] > trials / 6 - 600 && frames[0] < trials / 6 + 600);
  CHECK(frames[1] > trials / 3 - 800 && frames[1] < trials / 3 + 800);
  CHECK(frames[2] > trials / 3 - 800 && frames[2] < trials / 3 + 800);
  CHECK(frames[3] > trials / 6 - 600 && frames[3] < trials / 6 + 600);

  // Without a randomized start, the frame draw is skipped and the frame is 0.
  auto fixed = rock;
  fixed.randomizedStartFrame = false;
  random = {};
  app::DecalRandom copy = random;
  const auto first = app::placeDecal(fixed, random);
  (void)copy.signedUnit();
  CHECK(first.frame == 0u);
  CHECK(first.radians == (copy.signedUnit() * 360.0f) * 0.01745329238474369f);

  // Atlas off: no frame even with a randomized start, the draw still consumed.
  auto off = rock;
  off.animationEnable = false;
  random = {};
  for (int i = 0; i < 100; ++i) CHECK(app::placeDecal(off, random).frame == 0u);

  // Atlas offsets for a 2x2 grid of half frames.
  const auto f3 = app::decalFrameOffset(3, 2, 0.5f, 0.5f);
  CHECK(f3.u == 0.5f && f3.v == 0.5f);
  const auto f2 = app::decalFrameOffset(2, 2, 0.5f, 0.5f);
  CHECK(f2.u == 0.0f && f2.v == 0.5f);
  const auto f1 = app::decalFrameOffset(1, 2, 0.5f, 0.5f);
  CHECK(f1.u == 0.5f && f1.v == 0.0f);
  return obf2test::g_failures ? 1 : 0;
}
