#pragma once
// The vehicle HUD's flight values: what BF2.exe writes into the player HUD
// object (registered at 0x789480, docs/functions/hud-variables.md) for the
// vehicle the player occupies.
//
// 0x78cb10 (the occupied vehicle, every HUD update), from the vehicle's world
// matrix M (vtbl +0x80; rows as BF2 stores them, row 2 the forward axis):
//   VehicleAngle      +0x1c8  atan2(M[2].x, M[2].z)                      (0x78cb32)
//   VehicleElevation  +0x1d4  M[2].y
//   VehicleBanking    +0x1d0  −(π/2 − acos(clamp(M[0].y, −1, 1)))
//   AngleOfAttack     +0x1f0  (int) ceil(VehicleElevation · 100 + 296)
//   AltitudeString    +0x284  "%i" (0x89e620) of ceil(M[3].y − sea level),
//                             the sea level from HeightmapCluster vtbl +0x60
//                             (getSeaWaterLevel; the object at 0xa07e28)
//   SpeedString       +0x280  "%i" of trunc(|velocity|) · 10: the physics
//                             object's (+0x44) vtbl +0x5c, truncated under the
//                             0xc00 control word (0x78cda5..0x78cde4)
// 0x78ce60 (the vehicle panel, same update):
//   Torque            +0x1e4  t += (max(target, 0) − t) · 0.01 per update
//   TorqueString      +0x288  "%i" of ceil(Torque · 100)
//   TorqueAngle       +0x1ec  Torque · 2π
// The target (+0x28c) is the player's own throttle input (axis 3, c_PIThrottle)
// of this frame, 0 when its mask bit is clear (BF2HudManager 0x752f50, 0x75307e).
//
//   VehicleElevationSpeedAngle +0x1d8  4π / maxClimbSpeed · velocity.y · 0.1
//                             (0x78ce12..0x78ce4a; the smoothing there adds
//                             the new value less the old to the old, so it is
//                             the value itself). maxClimbSpeed is the root's
//                             VehicleHud template +0xbc, copied to +0x9c at
//                             0x78cecb: Linux VehicleHudCompTemplate +0x90
//                             (getMaxClimbSpeed, ctor default 10) once the
//                             camera-mode vector (24 bytes there, 16 in MSVC)
//                             is allowed for; hasTurretIcon anchors the two
//                             layouts (BF2 +0x90, Linux +0x58).
#include <optional>
#include <string>

#include "obf2/core/math.h"

namespace obf2::app {

struct VehicleFlightInput {
  Mat4 world;                    // the vehicle's world matrix (m[8..10] = row 2)
  std::optional<Vec3f> velocity; // metres per second; none: the speed is left as it was
  float seaLevel = -10000.0f;    // HeightmapCluster::getSeaWaterLevel
  float throttle = 0.0f;         // our throttle input this frame, −1..1
  int maxClimbSpeed = 10;        // vehicleHud.maxClimbSpeed of the root
};

struct VehicleFlightHud {
  float angle = 0.0f;
  float elevation = 0.0f;
  float banking = 0.0f;
  int angleOfAttack = 0;
  std::string altitude;
  std::optional<std::string> speed;
  float torque = 0.0f;  // carried from one update to the next (+0x1e4)
  std::string torqueText;
  float torqueAngle = 0.0f;
  std::optional<float> elevationSpeedAngle;  // none without a velocity
};

// One HUD update. `previousTorque` is the last update's Torque (0 when the
// HUD object was made: the constructor clears +0x1e4).
VehicleFlightHud vehicleFlightHud(const VehicleFlightInput& input, float previousTorque);

}  // namespace obf2::app
