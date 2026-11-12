// The soldier's pose: the keys, the delays, the change and the speed's blend
// (obf2/server/soldier_pose.h; Linux 0x5468d0, 0x546840, 0x54eb70, 0x54ec9d).
#include <cmath>

#include "check.h"
#include "obf2/server/physics.h"
#include "obf2/server/soldier_pose.h"

using namespace obf2::server;

namespace {

// Holding crouch asks for it once, on the press; letting go stands him up.
void testCrouchHoldAndRelease() {
  PhysicsConstants physics;
  BodyState body;
  updateSoldierPose(body, false, true, false, true, physics);
  CHECK_EQ(body.requestedPose, kPoseCrouch);
  executePoseChange(body, false, false, 1.0f, physics);
  CHECK_EQ(body.pose, kPoseCrouch);
  CHECK_EQ(body.speedState, 1);
  // Run (3) to crouch (1): `pose-run-crouch`, 0.25.
  CHECK(std::abs(body.poseDuration - 0.25f) < 1e-6f);

  updateSoldierPose(body, false, false, false, true, physics);
  CHECK_EQ(body.requestedPose, kPoseStand);
}

// Prone toggles: a press goes down (and starts the stand delay), a second press
// once the delay has run out comes back up with the three getting-up delays.
void testProneToggle() {
  PhysicsConstants physics;
  BodyState body;
  updateSoldierPose(body, true, false, false, true, physics);
  CHECK_EQ(body.requestedPose, kPoseProne);
  CHECK(std::abs(body.standDelay - physics.standDelayFromProne) < 1e-6f);
  executePoseChange(body, false, false, 1.0f, physics);
  CHECK_EQ(body.pose, kPoseProne);

  // Held, the key does nothing more (the latch).
  updateSoldierPose(body, true, false, false, true, physics);
  CHECK_EQ(body.requestedPose, kPoseProne);

  // Released, then pressed with the delay still running: he stays down.
  updateSoldierPose(body, false, false, false, true, physics);
  updateSoldierPose(body, true, false, false, true, physics);
  CHECK_EQ(body.requestedPose, kPoseProne);

  // Released, the delay over, pressed: up, with the three delays.
  body.standDelay = 0.0f;
  updateSoldierPose(body, false, false, false, true, physics);
  updateSoldierPose(body, true, false, false, true, physics);
  CHECK_EQ(body.requestedPose, kPoseStand);
  CHECK(std::abs(body.jumpDelay - physics.jumpDelayAfterProne) < 1e-6f);
  CHECK(std::abs(body.proneFromStandDelay - physics.proneDelayFromStand) < 1e-6f);
  CHECK(std::abs(body.fireFromProneDelay - physics.fireDelayFromProne) < 1e-6f);
}

// A jump's delay keeps him from going prone; so does deep water.
void testCanGoProne() {
  PhysicsConstants physics;
  BodyState body;
  CHECK(soldierCanGoProne(body, -100.0f, physics));
  body.proneDelay = 0.1f;
  CHECK(!soldierCanGoProne(body, -100.0f, physics));
  body.proneDelay = 0.0f;
  // Water over the pivot plus the radius less the limit: 1 + 0.25 - 1.1 = 0.15.
  CHECK(!soldierCanGoProne(body, 0.2f, physics));
  CHECK(soldierCanGoProne(body, 0.1f, physics));
}

// The speed blends from the old state's to the new one over the table's time.
void testSpeedBlend() {
  PhysicsConstants physics;
  BodyState body;
  body.speedState = 3;
  body.requestedPose = kPoseCrouch;
  executePoseChange(body, false, false, 1.0f, physics);
  const float crouch = soldierSpeed(physics, body.speedState, body.pose, 1.0f);
  const float first = blendPoseSpeed(body, crouch, 0.125f);
  // Half way through 0.25 s: half way from 3.9 to 2.0.
  CHECK(std::abs(first - (physics.runSpeed + crouch) * 0.5f) < 1e-4f);
  blendPoseSpeed(body, crouch, 0.125f);
  CHECK(std::abs(blendPoseSpeed(body, crouch, 0.125f) - crouch) < 1e-6f);
}

// The speed states: zoomed standing walks, sprint wins over zoom.
void testSpeedStates() {
  CHECK_EQ(soldierSpeedState(kPoseStand, false, false), 3);
  CHECK_EQ(soldierSpeedState(kPoseStand, false, true), 2);
  CHECK_EQ(soldierSpeedState(kPoseStand, true, true), 4);
  CHECK_EQ(soldierSpeedState(kPoseCrouch, true, false), 1);
  CHECK_EQ(soldierSpeedState(kPoseProne, false, false), 0);
  PhysicsConstants physics;
  // Crouching, the speed takes the mouse speed mod (Linux 0x54eb34).
  CHECK(std::abs(soldierSpeed(physics, 1, kPoseCrouch, 0.5f) - physics.crouchSpeed * 0.5f) < 1e-6f);
  CHECK(std::abs(soldierSpeed(physics, 2, kPoseStand, 0.5f) - physics.walkSpeed) < 1e-6f);
}

// Standing up is tested against the world; going down never is.
void testPoseFits() {
  BodyState body;
  body.pose = kPoseCrouch;
  body.requestedPose = kPoseStand;
  CHECK(!poseChangeFits(body, [](int) { return false; }));
  body.requestedPose = kPoseProne;
  CHECK(poseChangeFits(body, [](int) { return false; }));
  body.pose = kPoseStand;
  body.requestedPose = kPoseCrouch;
  CHECK(poseChangeFits(body, [](int) { return false; }));
}

}  // namespace

TEST_MAIN({
  testCrouchHoldAndRelease();
  testProneToggle();
  testCanGoProne();
  testSpeedBlend();
  testSpeedStates();
  testPoseFits();
})
