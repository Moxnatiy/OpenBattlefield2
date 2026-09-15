#include "obf2/server/soldier_look.h"

#include <algorithm>
#include <cmath>

#include "obf2/server/physics.h"

namespace obf2::server {

void turnSoldier(SoldierLook& look, float mouseX, float mouseY, float forwardInput,
                 const PhysicsConstants& constants) {
  const float delta = mouseX * constants.lookFactorX;
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

  look.pitch += mouseY * constants.lookFactorY;
  look.aimYaw = std::clamp(look.aimYaw, -maxYaw, maxYaw);
  look.pitch = std::clamp(look.pitch, -constants.lookMaxPitch, constants.lookMaxPitch);
}

}  // namespace obf2::server
