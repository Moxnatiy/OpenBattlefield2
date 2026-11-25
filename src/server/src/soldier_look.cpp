#include "obf2/server/soldier_look.h"

#include <algorithm>
#include <cmath>

#include "obf2/server/physics.h"

namespace obf2::server {

float soldierMouseSpeedMod(float playerFov, float cameraFov, float weaponMouseSpeedMod) {
  // 0x5a620d..0x5a6225: the ratio only when the camera is the wider.
  float mod = 1.0f;
  if (cameraFov > playerFov && cameraFov > 0.0f) mod = playerFov / cameraFov;
  return mod * weaponMouseSpeedMod;
}

void turnSoldier(SoldierLook& look, float mouseX, float mouseY, float forwardInput,
                 const PhysicsConstants& constants, float mouseSpeedMod,
                 float recoilYaw, float recoilPitch) {
  // 0x5ae205..0x5ae288: factor, the 0.001 dead zone, then the speed mod.
  float lookX = mouseX * constants.lookFactorX;
  float lookY = mouseY * constants.lookFactorY;
  if (std::abs(lookX) < 0.001f) lookX = 0.0f;
  if (std::abs(lookY) < 0.001f) lookY = 0.0f;
  lookX = lookX * mouseSpeedMod + recoilYaw;
  lookY = lookY * mouseSpeedMod + recoilPitch;
  const float delta = lookX;
  const float maxYaw = constants.lookMaxYaw;
  float bodyDelta = delta;

  const float forward = std::clamp(forwardInput, -1.0f, 1.0f);
  // 0x892adc: 0.01.
  const bool moving = std::abs(forward * forward) > 0.01f;
  if (moving) {
    if (look.aimYaw != 0.0f) look.turnLeft = look.aimYaw;
  } else {
    look.aimYaw += delta;
    if (look.aimYaw >= maxYaw || look.aimYaw <= -maxYaw) {
      look.turnLeft = look.aimYaw;
    } else {
      bodyDelta = 0.0f;
    }
  }

  if (std::abs(look.turnLeft) > 0.0f) {
    float step = (look.turnLeft > 0.0f ? 1.0f : -1.0f) * constants.lookSideRestore;
    if ((step > 0.0f && step >= look.turnLeft) || (step <= 0.0f && step < look.turnLeft)) {
      step = look.turnLeft;
    }
    look.turnLeft -= step;
    look.aimYaw -= step;
    bodyDelta += step;
  }

  look.bodyYaw += bodyDelta;
  while (look.bodyYaw >= 360.0f) look.bodyYaw -= 360.0f;
  while (look.bodyYaw <= -360.0f) look.bodyYaw += 360.0f;

  look.pitch += lookY;
  look.aimYaw = std::clamp(look.aimYaw, -maxYaw, maxYaw);
  look.pitch = std::clamp(look.pitch, -constants.lookMaxPitch, constants.lookMaxPitch);
}

}  // namespace obf2::server
