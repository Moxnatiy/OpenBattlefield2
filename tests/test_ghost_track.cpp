// The pose between ghost updates — `SoldierNetworkable::predict` (0x62d130) and
// `SimpleObjectNetworkable::predict` (0x62f7d0), see ghost_track.h.
#include <cmath>

#include "check.h"
#include "obf2/net/ghost_track.h"

using namespace obf2;
using namespace obf2::net::bf2;

static bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }

// Drawn 100 ms behind: halfway between two updates 66 ms apart.
static void testInterpolatesBehindNow() {
  GhostTrack track;
  track.push(1000.0f).position = Vec3f{0.0f, 0.0f, 0.0f};
  auto& b = track.push(1066.0f);
  b.position = Vec3f{6.6f, 0.0f, 0.0f};
  b.bodyYaw = 66.0f;
  const auto pose = track.poseAt(1133.0f);  // at 1033
  CHECK(pose && pose->mode == GhostPrediction::Interpolated);
  CHECK(near(pose->position.x, 3.3f));
  CHECK(near(pose->bodyYaw, 33.0f));
}

// A new slot starts as a copy of the newest: an update that changes nothing
// holds the object still while time goes on.
static void testPushCopiesTheNewest() {
  GhostTrack track;
  track.push(0.0f).position = Vec3f{1.0f, 2.0f, 3.0f};
  track.push(33.0f);
  CHECK(near(track.newest()->position.y, 2.0f));
  CHECK_EQ(track.count(), std::size_t(2));
  for (int i = 0; i < 6; ++i) track.push(66.0f + 33.0f * static_cast<float>(i));
  CHECK_EQ(track.count(), GhostTrack::kSlots);
}

// Past the newest: along the velocity, for at most GSExtrapolationTime.
static void testExtrapolatesAlongVelocity() {
  GhostTrack track;
  auto& a = track.push(1000.0f);
  a.velocity = Vec3f{0.0f, 0.0f, 10.0f};
  auto pose = track.poseAt(1300.0f);  // 200 ms past it
  CHECK(pose && pose->mode == GhostPrediction::Extrapolated);
  CHECK(near(pose->position.z, 2.0f));
  pose = track.poseAt(1000.0f + 100.0f + 1200.0f);
  CHECK(pose && pose->mode == GhostPrediction::Newest);
  CHECK(near(pose->position.z, 0.0f));
}

// No velocity of its own (a simple object): the last two positions give it.
static void testSimpleObjectVelocityFromPositions() {
  GhostTrack track;
  track.push(0.0f).position = Vec3f{0.0f, 0.0f, 0.0f};
  track.push(100.0f).position = Vec3f{1.0f, 0.0f, 0.0f};
  const auto pose = track.poseAt(300.0f);  // 100 ms past the newest
  CHECK(pose && near(pose->position.x, 2.0f));
}

// Updates 500 ms or more apart are not interpolated: the newest is taken.
static void testLongGapTakesTheNewest() {
  GhostTrack track;
  track.push(0.0f).position = Vec3f{0.0f, 0.0f, 0.0f};
  track.push(600.0f).position = Vec3f{6.0f, 0.0f, 0.0f};
  const auto pose = track.poseAt(400.0f);
  CHECK(pose && pose->mode == GhostPrediction::Newest);
  CHECK(near(pose->position.x, 6.0f));
}

TEST_MAIN({
  testInterpolatesBehindNow();
  testPushCopiesTheNewest();
  testExtrapolatesAlongVelocity();
  testSimpleObjectVelocityFromPositions();
  testLongGapTakesTheNewest();
})
