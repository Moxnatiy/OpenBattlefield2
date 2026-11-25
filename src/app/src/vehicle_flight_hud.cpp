#include "obf2/app/vehicle_flight_hud.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace obf2::app {

VehicleFlightHud vehicleFlightHud(const VehicleFlightInput& input, float previousTorque) {
  const auto& m = input.world.m;
  VehicleFlightHud out;
  // 0x78cb10.
  out.angle = std::atan2(m[8], m[10]);
  out.elevation = m[9];
  out.banking = -(std::numbers::pi_v<float> / 2.0f - std::acos(std::clamp(m[1], -1.0f, 1.0f)));
  out.angleOfAttack = static_cast<int>(std::ceil(out.elevation * 100.0f + 296.0f));
  out.altitude = std::to_string(static_cast<int>(std::ceil(m[13] - input.seaLevel)));
  if (input.velocity) {
    const auto& v = *input.velocity;
    const int speed = static_cast<int>(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
    out.speed = std::to_string(speed * 10);
    if (input.maxClimbSpeed != 0) {
      out.elevationSpeedAngle = 12.566371f / static_cast<float>(input.maxClimbSpeed) * v.y * 0.1f;
    }
  }
  // 0x78ce60: the torque eases toward the throttle input, 1% of the gap.
  const float target = std::max(input.throttle, 0.0f);
  out.torque = (target - previousTorque) * 0.01f + previousTorque;
  out.torqueText = std::to_string(static_cast<int>(std::ceil(out.torque * 100.0f)));
  out.torqueAngle = out.torque * 6.2831855f;
  return out;
}

}  // namespace obf2::app
