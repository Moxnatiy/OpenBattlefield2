// The soldier's angles over one tick (`soldier_look.h`, `BF2.exe` 0x5adea0).
//
// The numbers are the live server's states of our own soldier, strafing left with
// the mouse turning left by 5 degrees a tick (docs/functions/soldier-physics.md).
#include <cmath>

#include "check.h"
#include "obf2/server/physics.h"
#include "obf2/server/soldier_look.h"

namespace {

using namespace obf2::server;

bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }

// Standing with the aim at its limit: the body turns by the mouse and by
// `soldier-lookSideRestore` on top; the aim stays at -40 and 0x8 reads -41.
// States 470 and 471: body -325.5 -> -334.5.
void testAimAtTheLimitTurnsTheBody() {
  PhysicsConstants physics;
  SoldierLook look{-325.5f, -40.0f, -41.0f, 0.0f};
  turnSoldier(look, -1.0f, 0.0f, 0.0f, physics);
  CHECK(near(look.bodyYaw, -334.5f));
  CHECK(near(look.aimYaw, -40.0f));
  CHECK(near(look.turnLeft, -41.0f));
}

// Just short of the limit: states 463 -> 464, body -271.5 -> -280.5, aim and 0x8
// -36 -> -37.
void testAimNearTheLimit() {
  PhysicsConstants physics;
  SoldierLook look{-271.5f, -36.0f, -36.0f, 0.0f};
  turnSoldier(look, -1.0f, 0.0f, 0.0f, physics);
  CHECK(near(look.bodyYaw, -280.5f));
  CHECK(near(look.aimYaw, -37.0f));
  CHECK(near(look.turnLeft, -37.0f));
}

// Standing inside the limit with nothing to take back: only the aim moves.
void testStandingTurnsTheAim() {
  PhysicsConstants physics;
  SoldierLook look;
  turnSoldier(look, 2.0f, 0.0f, 0.0f, physics);
  CHECK(near(look.bodyYaw, 0.0f));
  CHECK(near(look.aimYaw, 10.0f));
  CHECK(near(look.yaw(), 10.0f));
}

// Running: the mouse turns the body, and an aim offset is taken back four degrees
// a tick. The forward run of the live server reported aim 0 throughout.
void testRunningTurnsTheBody() {
  PhysicsConstants physics;
  SoldierLook look;
  turnSoldier(look, 2.4f, 0.0f, 0.98f, physics);
  CHECK(near(look.bodyYaw, 12.0f));
  CHECK(near(look.aimYaw, 0.0f));

  SoldierLook offset{0.0f, 10.0f, 0.0f, 0.0f};
  turnSoldier(offset, 0.0f, 0.0f, 0.98f, physics);
  CHECK(near(offset.bodyYaw, 4.0f));
  CHECK(near(offset.aimYaw, 6.0f));
  CHECK(near(offset.turnLeft, 6.0f));
  // The last step stops at what is left rather than overshooting.
  SoldierLook small{0.0f, 3.0f, 0.0f, 0.0f};
  turnSoldier(small, 0.0f, 0.0f, 1.0f, physics);
  CHECK(near(small.bodyYaw, 3.0f));
  CHECK(near(small.aimYaw, 0.0f));
  CHECK(near(small.turnLeft, 0.0f));
}

// The throttle counts as moving once its square is over 0.01 (0x892adc).
void testForwardThreshold() {
  PhysicsConstants physics;
  SoldierLook slow;
  turnSoldier(slow, 1.0f, 0.0f, 0.09f, physics);
  CHECK(near(slow.bodyYaw, 0.0f));
  SoldierLook moving;
  turnSoldier(moving, 1.0f, 0.0f, 0.2f, physics);
  CHECK(near(moving.bodyYaw, 5.0f));
}

// The pitch stops at `soldier-lookMaxAngle.y`, 85, and the body wraps.
void testClamps() {
  PhysicsConstants physics;
  SoldierLook look;
  for (int i = 0; i < 30; ++i) turnSoldier(look, 0.0f, 1.0f, 0.0f, physics);
  CHECK(near(look.pitch, 85.0f));
  SoldierLook wrap{358.0f, 0.0f, 0.0f, 0.0f};
  turnSoldier(wrap, 1.0f, 0.0f, 1.0f, physics);
  CHECK(near(wrap.bodyYaw, 3.0f));
}

}  // namespace

TEST_MAIN({
  testAimAtTheLimitTurnsTheBody();
  testAimNearTheLimit();
  testStandingTurnsTheAim();
  testRunningTurnsTheBody();
  testForwardThreshold();
  testClamps();
})
