#include <cmath>
#include <vector>

#include "check.h"
#include "obf2/game/lod_switch.h"

using namespace obf2;

static void testDefaultDistances() {
  // No template value: 25 m steps from (skip + 1) · 25 (RendDX9.dll 0x10119a80).
  CHECK(game::lodSwitchDistances(nullptr, 0, 1).empty());
  const auto none = game::lodSwitchDistances(nullptr, 0, 3);
  CHECK_EQ(none.size(), std::size_t(2));
  CHECK_EQ(none[0], 25.0f);
  CHECK_EQ(none[1], 50.0f);
  const auto skipped = game::lodSwitchDistances(nullptr, 1, 3);
  CHECK_EQ(skipped[0], 50.0f);
  CHECK_EQ(skipped[1], 75.0f);
}

static void testBundledDistances() {
  // A bundled mesh steps by 50 m from (skip + 1) · 50 (0x100ea5f0), and its
  // scale is renderer+0x84c alone, +0x8b4 being 1 (0x1002dc40, 0x101050c0).
  const auto bundled = game::lodSwitchDistances(nullptr, 1, 3, game::kBundledLodStep);
  CHECK_EQ(bundled[0], 100.0f);
  CHECK_EQ(bundled[1], 150.0f);
  CHECK_EQ(game::lodDistanceScaleBundled(1), 0.5f);
  CHECK_EQ(game::lodDistanceScaleBundled(2), 0.8f);
  CHECK_EQ(game::lodDistanceScaleBundled(3), 1.0f);
}

static void testGivenDistances() {
  // `setSubGeometryLodDistance 0 0 1000` on a two-LOD mesh, nothing skipped.
  const std::vector<float> given{1000.0f};
  const auto two = game::lodSwitchDistances(&given, 0, 2);
  CHECK_EQ(two.size(), std::size_t(1));
  CHECK_EQ(two[0], 1000.0f);
  // Slot i reads the given value at skip + i, and the running value grows by
  // the given value at i.
  const std::vector<float> three{30.0f, 80.0f, 200.0f};
  const auto shifted = game::lodSwitchDistances(&three, 1, 3);
  CHECK_EQ(shifted[0], 80.0f);
  CHECK_EQ(shifted[1], 200.0f);
}

static void testPick() {
  const std::vector<float> distances{25.0f, 50.0f};
  // Close: LOD 0; past 25 m (after the base radius): LOD 1; past 50 m: LOD 2.
  CHECK_EQ(game::pickLod(distances, 1.0f, 1.0f, 20.0f, 1.0f), std::size_t(0));
  CHECK_EQ(game::pickLod(distances, 1.0f, 1.0f, 27.0f, 1.0f), std::size_t(1));
  CHECK_EQ(game::pickLod(distances, 1.0f, 1.0f, 52.0f, 1.0f), std::size_t(2));
  // Medium scales the distances by 0.8.
  CHECK_EQ(game::pickLod(distances, game::lodDistanceScale(2), 1.0f, 22.0f, 1.0f), std::size_t(1));
  // Zoomed in, the factor is below 1 and the near LOD holds farther out.
  CHECK(game::lodFovFactor(0.2f, 1.1f) < 1.0f);
  CHECK_EQ(game::lodFovFactor(1.1f, 1.1f), 1.0f);
}

static void testScaleAndRadius() {
  CHECK_EQ(game::lodDistanceScale(1), 1.0f);
  CHECK(std::fabs(game::lodDistanceScale(2) - 0.8f) < 1e-6f);
  CHECK_EQ(game::lodDistanceScale(3), 1.0f);
  const mesh::Aabb box{{-1.0f, -2.0f, -2.0f}, {1.0f, 2.0f, 2.0f}};
  CHECK(std::fabs(game::lodBaseRadius(box) - 3.0f) < 1e-6f);
}

TEST_MAIN({
  testDefaultDistances();
  testBundledDistances();
  testGivenDistances();
  testPick();
  testScaleAndRadius();
})
