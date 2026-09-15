#include "obf2/server/soldier_sprint.h"

#include <algorithm>

namespace obf2::server {

void sprintMessage(SprintState& sprint, bool goOnOnly) {
  // 0x43deb0.
  sprint.wants = true;
  if (goOnOnly && !sprint.sprinting) sprint.wants = false;
}

void updateSprint(SprintState& sprint, bool blocked, float rechargeDelay, float step) {
  // 0x43ded0. A dissipation time not above the float epsilon (0xb2bfb0,
  // 0x34000000) counts as blocked.
  const bool stop = blocked || sprint.dissipationTime <= 1.1920929e-7f;

  if (sprint.sprinting) {
    // 0x43df7c.
    sprint.stamina =
        std::max(0.0f, sprint.stamina - sprint.drainScale * step / sprint.dissipationTime);
    if (!sprint.wants || sprint.stamina <= 0.0f || stop) sprint.sprinting = false;
    sprint.wants = false;
    return;
  }

  // 0x43df10.
  if (sprint.recoverTime <= 0.0f) {
    sprint.stamina = 1.0f;
  } else if (rechargeDelay <= 0.0f) {
    sprint.stamina = std::min(1.0f, sprint.stamina + step / sprint.recoverTime);
  }
  if (sprint.wants && sprint.stamina >= sprint.limit && !stop) sprint.sprinting = true;
  sprint.wants = false;
}

}  // namespace obf2::server
