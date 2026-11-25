#include <cmath>
#include <vector>

#include "check.h"
#include "obf2/app/hemi_map.h"

using namespace obf2;
using obf2::app::HemiMap;

// texCoords2 = ((pos + size/2).xz − centre.xz) / size, v flipped
// (Particles.fx:93-94; HemiMapInfo from 0x1012b620).
void testMapping() {
  // 2×2: top row (v = 0, far +z) 0 and 255, bottom row 255 and 0.
  const auto map = HemiMap::fromAlpha(2, 2, {0, 255, 255, 0}, {100, 0, 50}, 20, 500);
  CHECK(map.shadowAltitude() == 500);
  // −x, +z corner is texel (0, 0); +x, +z is (1, 0); clamped at the edges.
  CHECK(std::abs(map.alpha({90, 0, 60}) - 0.0f) < 1e-5f);
  CHECK(std::abs(map.alpha({110, 0, 60}) - 1.0f) < 1e-5f);
  CHECK(std::abs(map.alpha({110, 0, 40}) - 0.0f) < 1e-5f);
  // The centre is the bilinear mean of the four.
  CHECK(std::abs(map.alpha({100, 0, 50}) - 0.5f) < 1e-5f);
}

// A missing file is the original's 8×8 white (0x1012ba13).
void testMissing() {
  const auto map = HemiMap::load(std::nullopt, {}, 2048, 500);
  CHECK(map.alpha({123, 4, -56}) == 1.0f);
}

TEST_MAIN({
  testMapping();
  testMissing();
})
