#include "obf2/game/rotational_bundle.h"
#include <algorithm>
#include <cmath>
#include "obf2/core/path.h"

namespace obf2::game {
namespace {
Vec3f vector(const ObjectTemplate& object, const char* key) {
  if (const auto* property = object.property(key))
    if (const auto value = property->asVec3(0)) return {value->x,value->y,value->z};
  // RotationalBundleTemplate constructor, Linux 0x588460: vector defaults 0.
  return {};
}
RotationInput input(std::string_view name, bool& supported) {
  const auto key = normalizeAssetPath(name);
  if (key.empty() || key == "pinone") return RotationInput::None;
  if (key == "pimouselookx") return RotationInput::MouseX;
  if (key == "pimouselooky") return RotationInput::MouseY;
  if (key == "pithrottle") return RotationInput::Throttle;
  if (key == "piyaw") return RotationInput::Yaw;
  if (key == "pipitch") return RotationInput::Pitch;
  if (key == "piroll") return RotationInput::Roll;
  supported = false; return RotationInput::None;
}
}
RotationalSettings rotationalSettings(const ObjectTemplate& object) {
  RotationalSettings result;
  // Names/fields verified by RotationalBundleTemplate::makeScript, Linux 0x588720.
  result.minimum = vector(object,"setMinRotation");
  result.maximum = vector(object,"setMaxRotation");
  result.maxSpeed = vector(object,"setMaxSpeed");
  result.acceleration = vector(object,"setAcceleration");
  result.deceleration = vector(object,"setDeAcceleration");
  result.continuous = vector(object,"setContinousRotationSpeed");
  result.useDeceleration = object.number("setUseDeAcceleration").value_or(0) != 0;
  result.capMaxSpeed = object.number("capMaxSpeed").value_or(0) != 0;
  // Template bytes +0x260 and +0x261 (makeScript, Linux 0x588720).
  result.automaticReset = object.number("setAutomaticReset").value_or(0) != 0;
  result.snapToZero = object.number("snapToZeroOnNoInput").value_or(0) != 0;
  result.inputs = {input(object.text("setInputToYaw"),result.supported),
                   input(object.text("setInputToPitch"),result.supported),
                   input(object.text("setInputToRoll"),result.supported)};
  result.rememberExcess = object.number("rememberExcessInput").value_or(0) != 0;
  for (const char* flag : {"automaticPitchStabilization",
                           "startAutoRotateOnEnter","setLesserYawAtSpeed"})
    if (object.number(flag).value_or(0) != 0) result.supported = false;
  return result;
}

void stepRotationalBundle(RotationalState& state, const RotationalSettings& settings,
                          Vec3f mappedInput, float seconds) {
  if (!settings.supported || !std::isfinite(seconds) || seconds <= 0) return;
  float* angles[] = {&state.angle.x,&state.angle.y,&state.angle.z};
  float* velocities[] = {&state.velocity.x,&state.velocity.y,&state.velocity.z};
  float* stored[] = {&state.excess.x,&state.excess.y,&state.excess.z};
  float inputs[] = {mappedInput.x,mappedInput.y,mappedInput.z};
  const float speeds[] = {settings.maxSpeed.x,settings.maxSpeed.y,settings.maxSpeed.z};
  const float accelerations[] = {settings.acceleration.x,settings.acceleration.y,settings.acceleration.z};
  const float decelerations[] = {settings.deceleration.x,settings.deceleration.y,settings.deceleration.z};
  const float continuous[] = {settings.continuous.x,settings.continuous.y,settings.continuous.z};
  const float minimum[] = {settings.minimum.x,settings.minimum.y,settings.minimum.z};
  const float maximum[] = {settings.maximum.x,settings.maximum.y,settings.maximum.z};
  for (unsigned axis = 0; axis < 3; ++axis) {
    if (accelerations[axis] == 0 && continuous[axis] == 0) continue;
    auto& angle = *angles[axis];
    // calculateAndClipAngle, Linux 0x585a40. rememberExcessInput: the input
    // adds to the store (+0x264), held to ±40; at most ±1 of it is this
    // tick's input and the rest stays. An input against the stored sign, or
    // the flag off, empties the store and the raw input is used.
    auto& excess = *stored[axis];
    if (settings.rememberExcess && excess*inputs[axis] >= 0) {
      excess = std::clamp(excess+inputs[axis],-40.0f,40.0f);
      inputs[axis] = std::clamp(excess,-1.0f,1.0f);
      excess -= inputs[axis];
    } else {
      excess = 0;
    }
    // snapToZeroOnNoInput: an input
    // under 0.05 toward the angle's side zeroes the angle before the step.
    if (settings.snapToZero && ((inputs[axis] < 0.05f && angle > 0) || (inputs[axis] > -0.05f && angle < 0))) {
      angle = 0;
    }
    // rot-use-old branch. Negative authored acceleration reverses the input,
    // not its acceleration magnitude. setAutomaticReset makes the input a
    // position: the target is input * maxRotation, not input * maxSpeed.
    const float scale = settings.automaticReset ? maximum[axis] : speeds[axis];
    float target = accelerations[axis] > 0 ? inputs[axis]*scale : -inputs[axis]*scale;
    if (settings.capMaxSpeed) {
      // Registered default rotationalbundle-max-speed-mod=30, Linux 0x58551f.
      const float limit = speeds[axis]*30.0f;
      target = std::max(-limit,std::min(target,limit));
    }
    if (settings.automaticReset) {
      // The angle itself moves toward the target at |acceleration| per second
      // (|deceleration| going down with setUseDeAcceleration); no velocity
      // and no continuous rotation.
      const float rate = target < angle && settings.useDeceleration
          ? std::abs(decelerations[axis]) : std::abs(accelerations[axis]);
      const float change = rate*seconds;
      angle = target > angle ? std::min(target,angle+change) : std::max(target,angle-change);
    } else {
      auto& velocity = *velocities[axis];
      const float acceleration = target < velocity && settings.useDeceleration
          ? std::abs(decelerations[axis]) : std::abs(accelerations[axis]);
      const float change = acceleration*seconds;
      velocity = target > velocity ? std::min(target,velocity+change)
                                  : std::max(target,velocity-change);
      angle += (velocity+continuous[axis])*seconds;
    }
    // The clip: above the maximum takes the maximum, else below the minimum
    // takes the minimum; with both zero only above 180 wraps by 360.
    if (minimum[axis] != 0 || maximum[axis] != 0) {
      if (angle > maximum[axis]) angle = maximum[axis];
      else if (angle < minimum[axis]) angle = minimum[axis];
    } else if (angle > 180.0f) {
      angle -= 360.0f;
    }
  }
}
}
