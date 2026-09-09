#include "obf2/hud/bottom_left.h"

#include <algorithm>
#include <cmath>

namespace obf2::hud {
void BottomLeftPanel::update(BottomLeftMode mode, float menuBackgroundAlpha) {
  // The order is the same as in 0x78b600: the targets by the current state first,
  // and the dimmed alphas at the end. There is no movement here — the graph does
  // that.
  switch (mode) {
    case BottomLeftMode::Hidden:
      // We hide only once the vehicle bars have faded; until then the region
      // stands at the "on foot" position.
      if (vehicleAlpha == 0.0f) {
        targetX = kBottomLeftFootX;
        // In the original it is `XPos <= on foot` rather than `<`: otherwise a
        // region that had already arrived would never start fading. The order is
        // exactly this — fade the bars first, and only once faded drive off the edge.
        if (x <= kBottomLeftFootX) {
          if (healthAlpha == 0.0f) targetX = kBottomLeftHiddenX;
          else targetHealthAlpha = 0.0f;
        }
      }
      break;

    case BottomLeftMode::Health:
      if (x < kBottomLeftFootX) {
        targetX = kBottomLeftFootX;
      } else {
        targetHealthAlpha = 1.0f;
        if (vehicleAlpha == 0.0f) targetX = kBottomLeftFootX;
        else targetVehicleAlpha = 0.0f;
      }
      break;

    case BottomLeftMode::Vehicle:
      // `on foot <= XPos` in the original, that is we arrived at the on-foot position.
      if (x < kBottomLeftFootX) {
        targetX = kBottomLeftFootX;
      } else {
        targetHealthAlpha = 1.0f;
        if (healthAlpha == 1.0f) {
          // The health bars have already faded in — now the region travels
          // further, under the vehicle bars, and only there do they fade in.
          if (x < kBottomLeftVehicleX) targetX = kBottomLeftVehicleX;
          else targetVehicleAlpha = 1.0f;
        }
      }
      break;
  }

  recomputeFaded(menuBackgroundAlpha);
}

void BottomLeftPanel::recomputeFaded(float menuBackgroundAlpha) {
  // `Faded = clamp(Alpha - (1 - base), 0, 1)`, where base is the plates' alpha
  // from the player's profile. Verbatim from the end of 0x78b600. It is computed
  // after the graph has moved the alphas — hence a separate function.
  const float lost = 1.0f - menuBackgroundAlpha;
  healthFadedAlpha = std::clamp(healthAlpha - lost, 0.0f, 1.0f);
  vehicleFadedAlpha = std::clamp(vehicleAlpha - lost, 0.0f, 1.0f);
}

}  // namespace obf2::hud
