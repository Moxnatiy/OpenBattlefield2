#include "obf2/anim/camera_spring.h"

#include <algorithm>
#include <cmath>

namespace obf2::anim {
namespace {

// The epsilon `merge` compares against (Linux 0xb2bfb0, 2^-23).
constexpr float kMergeEpsilon = 1.1920929e-7f;

// `anim-soldier-spring-clamp` (`BF2.exe` 0x85aeb0; Linux 0x5519c5).
constexpr float kSpringClamp = 0.001f;

// A value within this of zero after a step is zero (Linux 0xb355d0, 0xb4fa50).
constexpr float kSpringRest = 1e-4f;

// Below this squared speed the movement does not move the spring: 0.49, a speed
// of 0.7 (Linux 0xb4fa58, compared at 0x552f1d).
constexpr float kStillSpeedSquared = 0.49f;

// The sideways movement's share that goes into the vertical, with its sign
// turned: |side| × -0.3 (Linux 0xb4fa54, 0x552f77).
constexpr float kSideDip = -0.3f;

// The state stays within ±1 (Linux 0xb2f3bc, 0xb2f3c0; 0x55303e..0x5530d0).
constexpr float kStateLimit = 1.0f;

// An amplitude below this moves nothing (Linux 0xb2f3c4, 0x5531ab).
constexpr float kAmplitudeFloor = 0.001f;

bool notZero(const Vec3f& v) {
  return std::abs(v.x) > kMergeEpsilon || std::abs(v.y) > kMergeEpsilon ||
         std::abs(v.z) > kMergeEpsilon;
}

}  // namespace

void CameraSpringTemplate::merge(const CameraSpringTemplate& other) {
  if (notZero(other.amplitude)) amplitude = other.amplitude;
  if (notZero(other.zoomDamping)) zoomDamping = other.zoomDamping;
  if (notZero(other.stiffness)) stiffness = other.stiffness;
  if (notZero(other.move)) move = other.move;
  // 0x6b9a54..0x6b9a78: the two look numbers together, by the sum of their sizes.
  if (std::abs(other.look[0]) + std::abs(other.look[1]) > kMergeEpsilon) {
    look[0] = other.look[0];
    look[1] = other.look[1];
  }
  if (notZero(other.hit)) hit = other.hit;
  if (notZero(other.fire)) fire = other.fire;
}

void applySpringFunction(float stiffness, float& value, float step) {
  const float fall = std::max(kSpringClamp, value * value * stiffness * step);
  if (value > 0.0f) {
    value -= fall;
    if (!(value > kSpringRest)) value = 0.0f;
  } else if (value < 0.0f) {
    value += fall;
    if (!(value < -kSpringRest)) value = 0.0f;
  }
}

void CameraSpring::update(const CameraSpringTemplate& spring, const Input& input) {
  const float step = input.step;
  // Every part falls back first (0x552d8d..0x552dc7).
  applySpringFunction(spring.stiffness.x, state_.x, step);
  applySpringFunction(spring.stiffness.y, state_.y, step);
  applySpringFunction(spring.stiffness.z, state_.z, step);

  // The turn pushes it the other way: a turn to the right moves the weapon left,
  // and a look down moves it up (0x552dcc..0x552e20).
  state_.x -= step * input.turn[0] * spring.look[0];
  state_.y += step * input.turn[1] * spring.look[1];

  // The movement, in the movement matrix's own axes (0x552e28..0x552ff5). Slower
  // than 0.7 it does nothing.
  Vec3f v = input.velocity;
  if (dot(v, v) < kStillSpeedSquared) v = Vec3f{};
  const float side = dot(input.right, v) * step * spring.move.x;
  state_.x += side;
  state_.y += std::abs(side) * kSideDip;
  state_.y -= dot(input.forward, v) * step * spring.move.z;
  state_.y += dot(input.up, v) * step * spring.move.y;

  // The recoil (0x552ffa..0x55304f).
  const float recoil = input.pitchRecoil * step;
  state_.x += recoil * spring.fire.x;
  state_.y += recoil * spring.fire.y;
  state_.z += recoil * spring.fire.z;

  state_.x = std::clamp(state_.x, -kStateLimit, kStateLimit);
  state_.y = std::clamp(state_.y, -kStateLimit, kStateLimit);
  state_.z = std::clamp(state_.z, -kStateLimit, kStateLimit);
}

Vec3f CameraSpring::offset(const CameraSpringTemplate& spring) const {
  const auto amplitude = [](float a) { return std::abs(a) < kAmplitudeFloor ? 0.0f : a; };
  return Vec3f{state_.x * amplitude(spring.amplitude.x), state_.y * amplitude(spring.amplitude.y),
               state_.z * amplitude(spring.amplitude.z)};
}

}  // namespace obf2::anim
