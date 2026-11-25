#include "obf2/app/hit_direction.h"

#include <cmath>

namespace obf2::app {

void HitDirection::cue(const Vec3f& cameraForward, float hitFromDirection) {
  timeLeft_ = kHitDirectionTime;  // 0x465b50, case 0x34
  apply(true, 0.0f, cameraForward, hitFromDirection);
}

void HitDirection::update(float dt, const Vec3f& cameraForward, float hitFromDirection) {
  apply(show_, dt, cameraForward, hitFromDirection);  // 0x467548
}

void HitDirection::apply(bool show, float dt, const Vec3f& forward, float hitFrom) {
  // `BF2.exe` 0x464aa0.
  show_ = show;
  if (show_) {
    // (x, 0, z) normalised by 0x4639b0 (a zero vector stays zero), then atan2(x, z)
    // and fmod by 2π (0x89e618).
    float x = forward.x, z = forward.z;
    const float length = std::sqrt(x * x + z * z);
    if (length > 0.0f) {
      x /= length;
      z /= length;
    }
    constexpr float kTwoPi = 6.28318530717958647692f;
    angle_ = std::fmod(std::atan2(x, z), kTwoPi) - hitFrom;
  }
  if (timeLeft_ <= 0.0f) {  // against 0 (0x93c850)
    timeLeft_ = kHitDirectionTime;
    show_ = false;
    return;
  }
  if (show_) timeLeft_ -= dt;
}

}  // namespace obf2::app
