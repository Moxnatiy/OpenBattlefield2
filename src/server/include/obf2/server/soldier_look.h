#pragma once
// The soldier's angles over one tick of input — the look block of
// `Soldier::handlePlayerInput` (`BF2.exe` 0x5adea0, 0x5ae72b..0x5ae960) and the
// clamp after it (`FUN_005a8630`). Notes: docs/functions/soldier-physics.md,
// "The look over one tick".
//
// The soldier has two yaws: the body (`+0x224`), which the movement follows, and
// the aim's offset from it (`+0x240`), which the camera adds. A third angle,
// `+0x248`, is what the body still has to turn to take the offset back.
namespace obf2::server {

struct PhysicsConstants;

struct SoldierLook {
  float bodyYaw = 0.0f;   // +0x224, degrees; state bit 0x2
  float aimYaw = 0.0f;    // +0x240, the offset from the body; state bit 0x4
  float turnLeft = 0.0f;  // +0x248, the body's turn still to come; state bit 0x8
  float pitch = 0.0f;     // +0x238, degrees, positive looks down; state bit 0x10

  // Where the camera looks: the body plus the aim's offset.
  float yaw() const { return bodyYaw + aimYaw; }
};

// One tick. mouseX and mouseY are the action's look axes as the server reads them
// (0x5bc6a0); forwardInput is its throttle axis.
//
//   delta = mouseX * phy-soldier-look-factor-x (0x9ec248)
//   moving (sign(F) * F^2 over 0.01, F the throttle clamped to ±1, 0x892adc):
//     the body turns by delta; a non-zero aim offset becomes turnLeft
//   standing:
//     the aim takes delta; the body does not turn unless the aim is at or past
//     ±soldier-lookMaxAngle.x, and then the body turns by delta and turnLeft
//     becomes the whole aim
//   turnLeft not zero: a step of sign(turnLeft) * soldier-lookSideRestore
//     (0x9ec2cc), no further than turnLeft, comes off turnLeft and the aim and goes
//     onto the body
//   the body wraps into (-360, 360); the aim is clamped to ±lookMaxAngle.x and the
//   pitch, after adding mouseY * phy-soldier-look-factor-y, to ±lookMaxAngle.y
//   (0x5a8630).
//
// Not modelled: the sight's zoom multiplier (0x5a61a0), recoil (0x5a5f40,
// 0x5a5fa0), and the yaw limits of a seat (`+0x208`), none of which apply to a
// soldier on foot without a zoomed weapon.
void turnSoldier(SoldierLook& look, float mouseX, float mouseY, float forwardInput,
                 const PhysicsConstants& constants);

}  // namespace obf2::server
