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
// Before any of that both look deltas go through the input's own conditioning
// (0x5ae205..0x5ae288): each axis times its look factor, under 0.001 (0x89abd4)
// it is zero, and an occupied soldier's are then multiplied by
// `getMouseSpeedMod` (0x5a61a0, called at 0x5ae27b) — `mouseSpeedMod` here, see
// `soldierMouseSpeedMod`. The engine applies it itself, so the action carries
// the axes unscaled.
//
// Recoil is in degrees after the mouse conditioning (0x5ae321..0x5ae347).
// The yaw limits of a seat (`+0x208`) do not apply to a soldier on foot.
void turnSoldier(SoldierLook& look, float mouseX, float mouseY, float forwardInput,
                 const PhysicsConstants& constants, float mouseSpeedMod = 1.0f,
                 float recoilYaw = 0.0f, float recoilPitch = 0.0f);

// `Soldier::getMouseSpeedMod` (`BF2.exe` 0x5a61a0, Linux 0x54e9a0): the player's
// field of view (+0x8c) over the camera's active one (+0x58) when the camera's is
// wider, else 1 — times the active weapon's own `getMouseSpeedMod` (vtable
// +0x10c; `DefaultZoomComp::getMouseSpeedMod`, Linux 0x6295c0: the zoom
// template's `mouseSpeedMod` above zoom level 0, else 1).
float soldierMouseSpeedMod(float playerFov, float cameraFov, float weaponMouseSpeedMod);

}  // namespace obf2::server
