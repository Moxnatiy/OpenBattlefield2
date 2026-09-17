// A soldier's movement must not depend on the frame rate.
//
// The engine computes physics in ticks of `WorldPref::mTickTime` = 1/30 s (the
// Linux server's `.data` at 0xf68c50). For a long time we computed a step as long
// as a frame — and the same jump came out different at 120 frames and at 60.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "obf2/server/soldier_move.h"
#include "obf2/server/soldier_node.h"
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

// One tick in the engine's order: the input asks, the collision's friction turns
// the ask into the node's friction, and the next step takes it. The live server's
// states 333 and 334: from rest, the axis at 0.196 asks for 3.9 * 0.196 m/s; the
// next tick reports 0.757 m/s (times `p-pos-damp` 0.99) and moved 0.0126 m — half
// of that speed for a tick, the average of the old velocity and the new
// (0x6f1de0).
void testTickTakesThePreviousRequest() {
  PhysicsConstants physics;
  BodyState body;
  body.onGround = true;
  SwimState swim;
  SoldierIntent intent;
  intent.wish = Vec3f{0.0f, 0.0f, 0.98f * physics.acceleration};
  intent.speed = physics.runSpeed;
  tickSoldier(body, swim, intent, 0.0f, physics, nullptr, nullptr, kTickTime);
  // The first tick only asks: the friction holds the difference.
  CHECK(std::abs(body.position.z) < 1e-6f);
  CHECK(std::abs(body.velocity.z) < 1e-6f);
  CHECK(std::abs(body.friction.z - 0.7644f * 30.0f) < 1e-3f);
  CHECK(body.onGround);

  tickSoldier(body, swim, intent, 0.0f, physics, nullptr, nullptr, kTickTime);
  // Within the state's own precision: the resistance adds 0.035 of the slip.
  CHECK(std::abs(body.velocity.z - 0.757f) < 2e-3f);
  CHECK(std::abs(body.position.z - 0.0126f) < 1e-4f);
  // Standing on the floor: the step fell by one tick of gravity, the contact put
  // the feet back and holds the fall in the local linear speed.
  CHECK(std::abs(body.position.y) < 1e-5f);
  CHECK(std::abs(body.velocity.y + 14.73f / 30.0f * 0.99f) < 1e-3f);
  CHECK(std::abs(body.linearSpeed.y + body.velocity.y) < 1e-5f);
}

// A standing jump, against the live server's states (`--trace-own-state`, Dalian
// Plant, light kit): the first state after the jump is 0.2022 m up at 6.135 m/s.
// The input sets the speed and the acceleration to (0, 6, 0); the step adds the
// acceleration once more, less the drag, and damps.
void testStandingJumpMatchesTheServer() {
  PhysicsConstants physics;
  BodyState body;
  SwimState swim;
  SoldierIntent intent;
  // A tick standing, so the soldier is on the ground with gravity in the node.
  tickSoldier(body, swim, intent, 0.0f, physics, nullptr, nullptr, kTickTime);
  const float before = body.position.y;
  intent.jump = true;
  CHECK(tickSoldier(body, swim, intent, 0.0f, physics, nullptr, nullptr, kTickTime));
  CHECK(!body.onGround);
  CHECK(std::abs(body.position.y - before - 0.2022f) < 5e-4f);
  CHECK(std::abs(body.velocity.y - 6.135f) < 1e-3f);
  CHECK(std::abs(body.airControl - 2.0f) < 1e-6f);
  CHECK(body.noAirControl < 0.0f);
  CHECK(std::abs(body.sprintRechargeDelay - (0.7f - kTickTime)) < 1e-5f);

  // And the rest of the flight follows the states: 5.039 m/s two ticks later.
  intent.jump = false;
  tickSoldier(body, swim, intent, 0.0f, physics, nullptr, nullptr, kTickTime);
  tickSoldier(body, swim, intent, 0.0f, physics, nullptr, nullptr, kTickTime);
  CHECK(std::abs(body.velocity.y - 5.039f) < 2e-3f);
}

// A sprint jump: the state before holds 6.788 m/s forward and friction 2.1701;
// the jump keeps 0.98 of the ground speed, and the friction, which nothing
// clears, is added by the next step. The state after: 6.133 up, 6.872 forward.
void testSprintJumpKeepsTheFriction() {
  PhysicsConstants physics;
  BodyState body;
  SwimState swim;
  body.onGround = true;
  body.velocity = Vec3f{0.0f, -0.485f, 6.788f};
  body.linearSpeed = Vec3f{0.0f, 0.485f, 0.0f};
  body.acceleration = Vec3f{0.0f, -14.73f, 0.0f};
  body.friction = Vec3f{0.0f, 0.0f, 2.1701f};
  SoldierIntent intent;
  intent.wish = Vec3f{0.0f, 0.0f, 0.98f};
  intent.speed = physics.sprintSpeed;
  intent.jump = true;
  CHECK(tickSoldier(body, swim, intent, 0.0f, physics, nullptr, nullptr, kTickTime));
  CHECK(std::abs(body.velocity.y - 6.133f) < 1e-3f);
  CHECK(std::abs(body.velocity.z - 6.872f) < 2e-3f);
}

// Steering in the air (`updateSoldierSpeed`, Linux 0x54f0a5): forward pressed
// five ticks into a standing jump, the axis at 0.195 — the state reads 0.3547.
void testAirControlFromStanding() {
  PhysicsConstants physics;
  BodyState body;
  body.airControl = 2.0f - 4.0f * kTickTime;
  body.noAirControl = -1.0f;
  SoldierIntent intent;
  intent.wish = Vec3f{0.0f, 0.0f, 0.195f};
  intent.speed = physics.runSpeed;
  soldierInput(body, intent, physics, kTickTime);
  stepSoldierNode(body, 0.0f, physics, kTickTime);
  CHECK(std::abs(body.velocity.z - 0.3547f) < 2e-3f);

  // Faster than the steering's strength, a push that would add speed is taken back.
  BodyState fast;
  fast.airControl = 1.0f;
  fast.noAirControl = -1.0f;
  fast.velocity = Vec3f{0.0f, 0.0f, 5.0f};
  soldierInput(fast, intent, physics, kTickTime);
  CHECK(std::abs(fast.velocity.z - 5.0f) < 1e-4f);
}

// A landing meets the friction's dynamic limit: friction 0.95 (`Human_body` 1.1
// and the ground's 0.8), 4.8 * 9.82 / 30 a tick — the live server's landing
// reports 44.78.
void testLandingFrictionIsLimited() {
  PhysicsConstants physics;
  BodyState body;
  body.onGround = true;
  body.groundNormal = Vec3f{0.0f, 1.0f, 0.0f};
  body.contactSpeed = Vec3f{-3.0f, 0.0f, 0.0f};
  soldierFriction(body, physics);
  CHECK(std::abs(length(body.friction) - 44.78f) < 0.02f);
  CHECK(!body.sticking);
  CHECK(length(body.contactSpeed) == 0.0f);
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
  carryRemoteSoldier(body, swim, Vec3f{0.0f, -0.1f, 0.0f}, Vec3f{0.0f, -0.48f, 0.0f}, 0.0f,
                     physics, nullptr, nullptr, kTickTime);
  CHECK(std::abs(body.position.y) < 1e-4f);
  CHECK(body.linearSpeed.y > 0.0f);
  CHECK(body.onGround);
}

// On the ground the node takes the velocity it was handed, dragged and damped by
// `p-pos-damp`, and moves by the average of the two. Looking along the run, the
// drag is the forward coefficient's.
static void testRemoteSoldierRunsWithTheNodeStep() {
  PhysicsConstants physics;
  BodyState body;
  SwimState swim;
  body.onGround = true;
  const float speed = 6.92f;
  carryRemoteSoldier(body, swim, Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{speed, 0.0f, 0.0f}, 90.0f,
                     physics, nullptr, nullptr, kTickTime);
  const float drag = physics.dragForward * (speed / physics.mass) * speed * kTickTime;
  const float damped = (speed - drag) * physics.positionalDamping;
  CHECK(std::abs(body.velocity.x - damped) < 0.001f);
  CHECK(std::abs(body.position.x - (speed + damped) * 0.5f * kTickTime) < 0.001f);
}

TEST_MAIN({
  testRemoteSoldierStopsOnTheFloor();
  testRemoteSoldierRunsWithTheNodeStep();
  testAxesRampLikeTheServer();
  testTickTakesThePreviousRequest();
  testStandingJumpMatchesTheServer();
  testSprintJumpKeepsTheFriction();
  testAirControlFromStanding();
  testLandingFrictionIsLimited();
  testMovementDoesNotDependOnFrameRate();
  testJumpMatchesEngineConstants();
  testAccumulatorKeepsTheRemainder();
});
