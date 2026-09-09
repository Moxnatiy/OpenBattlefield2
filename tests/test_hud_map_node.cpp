// The map: the target by the HUD's state (BF2.exe 0x777dc0) and deriving
// MapFullSize/MapMinSize from the current size (0x77c330).
#include <cmath>

#include "obf2/hud/map_node.h"
#include "check.h"

using namespace obf2;

namespace {

// Run seconds forward in the engine's tick steps (1/30).
void run(hud::MapNode& map, float seconds) {
  const float step = 1.0f / 30.0f;
  for (float t = 0.0f; t < seconds; t += step) map.update(step);
}

}  // namespace

// In combat the map stands as a thumbnail in the corner: 197x197 at (597, 0) —
// setMiniPos 197/-300 plus half a screen. And that is the state in which
// `MapMinSize` is on rather than `MapFullSize`.
void testIngameIsMiniMap() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  run(map, 1.0f);

  CHECK(std::abs(map.size().x - 197.0f) < 0.5f);
  CHECK(std::abs(map.position().x - 597.0f) < 0.5f);
  CHECK(std::abs(map.position().y - 0.0f) < 0.5f);
  CHECK(map.minSize());
  CHECK(!map.fullSize());
  CHECK(map.settled());
}

// The M key is state 2: the map travels to setMaxiPos -122/-273 and grows to
// 512x512, after which `MapFullSize` comes on.
void testBigMapGrowsToMaxi() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  run(map, 1.0f);

  map.applyState(hud::kMapStateBigMap);
  run(map, 3.0f);

  CHECK(std::abs(map.size().x - 512.0f) < 0.5f);
  CHECK(std::abs(map.size().y - 512.0f) < 0.5f);
  CHECK(std::abs(map.position().x - 278.0f) < 0.5f);
  CHECK(std::abs(map.position().y - 27.0f) < 0.5f);
  CHECK(map.fullSize());
  CHECK(!map.minSize());
}

// The main thing this was taken apart for: **while the size is in transit neither**
// of the show variables is on. Until now we switched them together with the state,
// and the minimap's and the big map's frames managed to overlap.
void testNeitherFlagWhileMoving() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  run(map, 1.0f);

  map.applyState(hud::kMapStateBigMap);
  map.update(1.0f / 30.0f);

  CHECK(!map.fullSize());
  CHECK(!map.minSize());
  CHECK(!map.settled());
  // The size has already started growing but has not arrived.
  CHECK(map.size().x > 197.0f);
  CHECK(map.size().x < 512.0f);
}

// The quick zoom menu (state 19, `MapMenuShow`) lands over the map and does not
// move the map itself — in 0x777e11 that number falls into a branch that leaves the
// target alone.
void testMapMenuDoesNotMoveMap() {
  hud::MapNode map;
  map.applyState(hud::kMapStateBigMap);
  run(map, 3.0f);

  map.applyState(19);
  run(map, 1.0f);

  CHECK(std::abs(map.size().x - 512.0f) < 0.5f);
  CHECK(map.fullSize());
}

// The commander's map is larger than the big one: 561x561 at (239, 19).
void testCommanderMap() {
  hud::MapNode map;
  map.applyState(hud::kMapStateCommander);
  run(map, 4.0f);

  CHECK(std::abs(map.size().x - 561.0f) < 0.5f);
  CHECK(std::abs(map.position().x - 239.0f) < 0.5f);
  CHECK(map.fullSize());
}

// The compass drives the angle towards the player's direction. Through two
// smoothers in a row, so more slowly than through one.
void testCompassFollowsTheLook() {
  hud::MapAngle angle;
  angle.setTarget(1.5f);
  for (int i = 0; i < 90; ++i) angle.update(1.0f / 30.0f);

  CHECK(std::abs(angle.angle() - 1.5f) < 0.01f);
  CHECK(std::abs(angle.delayed() - 1.5f) < 0.01f);
}

// The delayed angle lags behind the smoothed one — which is exactly why in the
// original the compass arrives after the turn rather than with it.
void testDelayedLagsBehind() {
  hud::MapAngle angle;
  angle.setTarget(1.5f);
  angle.update(1.0f / 30.0f);

  CHECK(angle.angle() > 0.0f);
  CHECK(angle.delayed() < angle.angle());
}

// Across zero the angle goes the shortest way rather than round the half circle:
// 3.0 -> -3.0 is 0.28 radians forward, not 6.0 back.
void testShortestWayAroundZero() {
  hud::MapAngle angle;
  angle.setTarget(3.0f);
  for (int i = 0; i < 90; ++i) angle.update(1.0f / 30.0f);

  angle.setTarget(-3.0f);
  angle.update(1.0f / 30.0f);
  // We went past pi, that is jumped the boundary rather than crawling back to zero.
  CHECK(angle.angle() > 3.0f || angle.angle() < -3.0f);
}

// The minimap follows the player: the centre is driven towards his position rather
// than jumping. That is exactly why in the original the map creeps rather than jerks.
void testCentreFollowsThePlayer() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  map.update(1.0f / 30.0f);

  map.setCentre(0.75f, 0.25f);
  map.update(1.0f / 30.0f);
  // It moved but has not arrived.
  CHECK(map.centre().x > 0.5f);
  CHECK(map.centre().x < 0.75f);

  for (int i = 0; i < 90; ++i) map.update(1.0f / 30.0f);
  CHECK(std::abs(map.centre().x - 0.75f) < 0.001f);
  CHECK(std::abs(map.centre().y - 0.25f) < 0.001f);
}

// The zoom: every next index magnifies by 2.3, and the value is driven towards it
// rather than switched.
void testZoomIsAPowerOfBase() {
  hud::MapNode map;
  CHECK(std::abs(map.zoomScale() - 1.0f) < 0.001f);

  map.setZoomIndex(2);
  for (int i = 0; i < 120; ++i) map.update(1.0f / 30.0f);
  CHECK(std::abs(map.zoom() - 2.0f) < 0.01f);
  CHECK(std::abs(map.zoomScale() - 2.3f * 2.3f) < 0.05f);
}

// There are exactly three indices — the map node's tables hold no more.
void testZoomIndexIsClamped() {
  hud::MapNode map;
  map.setZoomIndex(7);
  CHECK_EQ(map.zoomIndex(), hud::kMapZoomLevels - 1);
  map.setZoomIndex(-3);
  CHECK_EQ(map.zoomIndex(), 0);
}

TEST_MAIN({
  testIngameIsMiniMap();
  testBigMapGrowsToMaxi();
  testNeitherFlagWhileMoving();
  testMapMenuDoesNotMoveMap();
  testCommanderMap();
  testCompassFollowsTheLook();
  testDelayedLagsBehind();
  testShortestWayAroundZero();
  testCentreFollowsThePlayer();
  testZoomIsAPowerOfBase();
  testZoomIndexIsClamped();
});
