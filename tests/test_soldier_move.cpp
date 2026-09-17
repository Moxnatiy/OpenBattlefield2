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

// The axes ramp as `Soldier::updateSoldierSpeed` smooths them (0x5a7c50). The
// live server's state, forward held from standing: 0.195, 0.352, … 0.979 — an
// input of 0.98 closing a fifth of the gap per tick.
void testAxesRampLikeTheServer() {
  PhysicsConstants physics;
  BodyState body;
  const Vec3f forward{0.0f, 0.0f, 1.0f};
  const Vec3f right{1.0f, 0.0f, 0.0f};
  Vec3f direction = soldierMoveDirection(body, 0.98f, 0.0f, forward, right, physics);
  CHECK(std::abs(body.forwardAxis - 0.196f) < 1e-4f);
  CHECK(std::abs(direction.z - 0.196f) < 1e-4f);
  direction = soldierMoveDirection(body, 0.98f, 0.0f, forward, right, physics);
  CHECK(std::abs(body.forwardAxis - 0.3528f) < 1e-4f);
  for (int i = 0; i < 60; ++i) soldierMoveDirection(body, 0.98f, 0.0f, forward, right, physics);
  CHECK(std::abs(body.forwardAxis - 0.98f) < 1e-3f);

  // Both axes full: the direction's length is held to one.
  BodyState both;
  both.forwardAxis = 1.0f;
  both.strafeAxis = 1.0f;
  direction = soldierMoveDirection(both, 1.0f, 1.0f, forward, right, physics);
  CHECK(std::abs(length(direction) - 1.0f) < 1e-4f);
}

// One tick in the engine's order. The live server's states 333 and 334: from rest,
// the axis at 0.196 asks for 3.9 * 0.196 m/s; the next tick reports 0.757 m/s
// (times `p-pos-damp` 0.99) and moved 0.0126 m — half of that speed for a tick,
// the average of the old velocity and the new (0x6f1de0).
void testTickTakesThePreviousRequest() {
  PhysicsConstants physics;
  BodyState body;
  body.onGround = true;
  const Vec3f wish{0.0f, 0.0f, 0.98f * physics.acceleration};
  stepSoldier(body, wish, physics.runSpeed, false, physics, 0.0f, kTickTime, true);
  // The first tick only asks.
  CHECK(std::abs(body.position.z) < 1e-6f);
  CHECK(std::abs(body.velocity.z) < 1e-6f);
  CHECK(std::abs(body.request.z - 0.7644f) < 1e-4f);

  stepSoldier(body, wish, physics.runSpeed, false, physics, 0.0f, kTickTime, true);
  CHECK(std::abs(body.velocity.z - 0.7568f) < 1e-4f);
  CHECK(std::abs(body.position.z - 0.0126f) < 1e-4f);
}

// Another player's soldier, carried by the physics the way `predict` seeds it
// (Linux 0x5dc314) and the node steps it (0x6f1de0).
static void testRemoteSoldierStopsOnTheFloor() {
  PhysicsConstants physics;
  BodyState body;
  SwimState swim;
  body.onGround = true;
  // The prediction has run him along a downward velocity to a tenth of a metre
  // under the floor — exactly what the drawn line did between two updates.
  carryRemoteSoldier(body, swim, Vec3f{0.0f, -0.1f, 0.0f}, Vec3f{0.0f, -0.48f, 0.0f}, physics,
                     nullptr, nullptr, kTickTime);
  CHECK(body.position.y >= 0.0f);
  CHECK(body.velocity.y >= 0.0f);
  CHECK(body.onGround);
}

// On the ground the node takes the velocity it was handed, damped by
// `p-pos-damp`, and moves by the average of the two.
static void testRemoteSoldierRunsWithTheNodeStep() {
  PhysicsConstants physics;
  BodyState body;
  SwimState swim;
  body.onGround = true;
  const float speed = 6.92f;
  carryRemoteSoldier(body, swim, Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{speed, 0.0f, 0.0f}, physics,
                     nullptr, nullptr, kTickTime);
  const float damped = speed * physics.positionalDamping;
  CHECK(std::abs(body.velocity.x - damped) < 0.001f);
  CHECK(std::abs(body.position.x - (speed + damped) * 0.5f * kTickTime) < 0.001f);
}

TEST_MAIN({
  testRemoteSoldierStopsOnTheFloor();
  testRemoteSoldierRunsWithTheNodeStep();
  testAxesRampLikeTheServer();
  testTickTakesThePreviousRequest();
  testMovementDoesNotDependOnFrameRate();
  testJumpMatchesEngineConstants();
  testAccumulatorKeepsTheRemainder();
});
