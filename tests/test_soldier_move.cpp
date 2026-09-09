// A soldier's movement must not depend on the frame rate.
//
// The engine computes physics in ticks of `WorldPref::mTickTime` = 1/30 s (the
// Linux server's `.data` at 0xf68c50). For a long time we computed a step as long
// as a frame — and the same jump came out different at 120 frames and at 60.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "obf2/server/soldier_move.h"
#include "check.h"

namespace {

using namespace obf2;
using namespace obf2::server;

// A soldier thrown upwards and sent forward over exactly a second of time — sliced
// into frames of different lengths. The speed is set directly rather than with the
// jump button: the check then measures the integration itself, without depending on
// which frame the press fell into.
Vec3f flyForOneSecond(float frameSeconds) {
  PhysicsConstants physics;
  BodyState body;
  body.position = Vec3f{0.0f, 0.0f, 0.0f};
  body.velocity = Vec3f{0.0f, 5.0f, 0.0f};
  body.onGround = false;
  SwimState swim;
  TickAccumulator accumulator;

  const int frames = static_cast<int>(std::lround(1.0f / frameSeconds));
  for (int frame = 0; frame < frames; ++frame) {
    const int ticks = accumulator.take(frameSeconds);
    for (int i = 0; i < ticks; ++i) {
      moveSoldier(body, swim, Vec3f{0.0f, 0.0f, 1.0f}, physics.runSpeed, false, physics, nullptr,
                  nullptr, kTickTime);
    }
  }
  return body.position;
}

void testMovementDoesNotDependOnFrameRate() {
  const Vec3f at60 = flyForOneSecond(1.0f / 60.0f);
  const Vec3f at120 = flyForOneSecond(1.0f / 120.0f);
  const Vec3f at30 = flyForOneSecond(1.0f / 30.0f);

  // The tick is one and the same, so the same number of ticks accumulates over a
  // second, and the end has to match to within float error.
  CHECK(std::abs(at60.z - at120.z) < 1e-3f);
  CHECK(std::abs(at60.z - at30.z) < 1e-3f);
  CHECK(std::abs(at60.y - at120.y) < 1e-3f);
  CHECK(std::abs(at60.y - at30.y) < 1e-3f);

  // And there really was movement: otherwise the match would mean nothing. In the
  // air the acceleration is damped (`phy-soldier-air-movement-factor`), so not much
  // accumulates forward over a second — but not nothing.
  CHECK(at60.z > 0.1f);
}

// The accumulator must neither lose time nor issue extra ticks.
void testAccumulatorKeepsTheRemainder() {
  TickAccumulator accumulator;
  int total = 0;
  // Thirty frames of 1/60 s are exactly 15 ticks of 1/30 s.
  for (int i = 0; i < 30; ++i) total += accumulator.take(1.0f / 60.0f);
  CHECK_EQ(total, 15);

  // A frame shorter than a tick gives no tick by itself, but the time does not vanish.
  TickAccumulator slow;
  CHECK_EQ(slow.take(0.01f), 0);
  CHECK_EQ(slow.take(0.01f), 0);
  CHECK_EQ(slow.take(0.02f), 1);
}

}  // namespace

// A jump has the height and duration the engine's constants give: an initial speed
// of 6.0 (0xb355c8) and gravity of 14.73 (0x6d6ae4).
void testJumpMatchesEngineConstants() {
  PhysicsConstants physics;
  BodyState body;
  body.onGround = true;
  SwimState swim;

  float highest = 0.0f;
  float airborne = 0.0f;
  for (int tick = 0; tick < 200; ++tick) {
    const bool jump = tick == 0;
    moveSoldier(body, swim, Vec3f{}, physics.runSpeed, jump, physics, nullptr, nullptr, kTickTime);
    highest = std::max(highest, body.position.y);
    if (!body.onGround) airborne += kTickTime;
    else if (tick > 0 && airborne > 0.0f) break;
  }

  // The continuous formula gives 6^2/(2*14.73) = 1.222 m and 2*6/14.73 = 0.815 s.
  // The engine, like us, integrates in 1/30 ticks, and discreteness makes the apex
  // come out slightly higher — about 1.31 m. We compare against what the tick gives
  // rather than against the ideal formula.
  CHECK(highest > 1.25f && highest < 1.40f);
  CHECK(airborne > 0.75f && airborne < 0.92f);

  // And separately, the thing this is measured for: with the old invented numbers
  // (gravity 9.81, impulse 5.0) the soldier would hang in the air for over a
  // second. That is exactly what was visible as "the jump is too long".
  CHECK(airborne < 1.0f);
}

TEST_MAIN({
  testMovementDoesNotDependOnFrameRate();
  testJumpMatchesEngineConstants();
  testAccumulatorKeepsTheRemainder();
});
