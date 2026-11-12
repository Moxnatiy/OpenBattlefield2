#include "obf2/server/soldier_pose.h"

namespace obf2::server {

namespace {

// prone → up (0x5469b9..0x5469e0 and 0x546a44..0x546a6b): the three delays.
void startGettingUp(BodyState& body, const PhysicsConstants& constants) {
  body.jumpDelay = constants.jumpDelayAfterProne;
  body.proneFromStandDelay = constants.proneDelayFromStand;
  body.fireFromProneDelay = constants.fireDelayFromProne;
}

}  // namespace

void updateSoldierPose(BodyState& body, bool prone, bool crouch, bool swimming, bool canGoProne,
                       const PhysicsConstants& constants) {
  if (swimming) return;  // 0x5468d9 (+0x48c); skydiving (+0x4b0) is not modelled

  if (prone) {
    if ((body.poseLatch & 2u) != 0) return;  // 0x546902
    const int pose = body.pose;
    int requested = pose < 2 ? pose : 0;  // 0x54690f..0x546914
    // 0x546917/0x546a10: prone with the stand delay run out keeps `requested`;
    // otherwise ask whether he may go down.
    const bool standDelayRunning = body.standDelay > 0.0f;
    if (pose != kPoseProne || standDelayRunning) {
      if (canGoProne) requested = kPoseProne;
    }
    body.requestedPose = requested;  // 0x54693b
    if (pose == kPoseProne && requested == kPoseStand) startGettingUp(body, constants);
    if (pose == kPoseStand && requested == kPoseProne) {
      body.standDelay = constants.standDelayFromProne;  // 0x546a30
    }
    body.poseLatch |= 2u;  // 0x546965
    return;
  }

  body.poseLatch &= ~2u;  // 0x54697f
  if (crouch) {
    if ((body.poseLatch & 1u) == 0) {  // 0x54698c
      body.poseLatch |= 1u;
      body.requestedPose = kPoseCrouch;
    }
    // 0x5469a6: pose 2 with request 1.
    if (body.pose == kPoseProne && body.requestedPose == kPoseCrouch) {
      startGettingUp(body, constants);
    }
  } else {
    body.poseLatch &= ~3u;  // 0x5469e8
    if (body.pose == kPoseCrouch) body.requestedPose = kPoseStand;  // 0x5469fe
  }
}

bool soldierCanGoProne(const BodyState& body, float waterLevel, const PhysicsConstants& constants) {
  // 0x546890..0x5468a4: the pivot is the feet plus the pivot height.
  const float pivot = body.position.y + constants.pivotHeight;
  if (waterLevel > constants.radius + pivot - constants.proneInWaterLimit) return false;
  return body.proneDelay <= 0.0f && body.proneFromStandDelay <= 0.0f;  // 0x5468a9, 0x5468b2
}

void executePoseChange(BodyState& body, bool sprinting, bool zoomed, float mouseSpeedMod,
                       const PhysicsConstants& constants) {
  // 0x54eb7d..0x54eb9d: the speed and the state before.
  const float outgoing = soldierSpeed(constants, body.speedState, body.pose, mouseSpeedMod);
  const int from = body.speedState;
  body.poseFromSpeed = outgoing;  // +0x49c
  body.poseProgress = 0.0f;       // +0x494
  body.pose = body.requestedPose;  // 0x54ebd5
  body.speedState = soldierSpeedState(body.pose, sprinting, zoomed);  // 0x54ec06
  // 0x54ec0b..0x54ec25: [from * 7 + to].
  if (from >= 0 && from < 7 && body.speedState >= 0 && body.speedState < 7) {
    body.poseDuration = constants.poseTransition[from][body.speedState];
  }
}

float blendPoseSpeed(BodyState& body, float speed, float dt) {
  if (body.poseDuration > body.poseProgress) {  // 0x54ecb0
    body.poseProgress += dt;                     // 0x54ecc0
    return (speed - body.poseFromSpeed) * body.poseProgress / body.poseDuration +
           body.poseFromSpeed;
  }
  return speed;
}

bool poseChangeFits(const BodyState& body, const std::function<bool(int pose)>& fits) {
  if (body.pose == kPoseSwim) return false;
  switch (body.requestedPose) {
    case kPoseCrouch:
      return body.pose == kPoseStand || fits(kPoseCrouch);
    case kPoseStand:
      return fits(kPoseStand);
    case kPoseProne:
      return true;
    default:
      return body.pose == body.requestedPose;
  }
}

float poseCameraFollow(int pose, const PhysicsConstants& constants) {
  if (pose == kPoseCrouch) return constants.cameraSpeedOnCrouch;
  if (pose == kPoseProne) return constants.cameraSpeedOnProne;
  return constants.cameraSpeedOnStand;
}

}  // namespace obf2::server
