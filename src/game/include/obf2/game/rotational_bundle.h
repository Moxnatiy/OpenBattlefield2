#pragma once
#include <array>
#include "obf2/core/math.h"
#include "obf2/game/object_template.h"

namespace obf2::game {
enum class RotationInput { None, MouseX, MouseY, Throttle, Yaw, Pitch, Roll };
struct RotationalSettings {
  Vec3f minimum{}, maximum{}, maxSpeed{}, acceleration{}, deceleration{}, continuous{};
  std::array<RotationInput,3> inputs{};
  bool useDeceleration = false, capMaxSpeed = false, supported = true;
  // setAutomaticReset (+0x260): the input is a position. snapToZeroOnNoInput
  // (+0x261): a released input zeroes the angle (Linux 0x585a40).
  bool automaticReset = false, snapToZero = false;
  // rememberExcessInput (+0x266): input past ±1 is kept and spent on later ticks.
  bool rememberExcess = false;
};
// excess: the bundle's stored input (+0x264), with rememberExcessInput.
struct RotationalState { Vec3f angle{}, velocity{}, excess{}; };
// Old rotational branch, default rot-use-old=1 (Linux 0x5854b0).
// Yaw
// stabilization is applied separately when composing the parent transform.
RotationalSettings rotationalSettings(const ObjectTemplate& object);
void stepRotationalBundle(RotationalState& state, const RotationalSettings& settings,
                          Vec3f mappedInput, float seconds);
}
