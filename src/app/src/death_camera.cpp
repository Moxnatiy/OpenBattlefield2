#include "obf2/app/death_camera.h"

#include <algorithm>
#include <cmath>

namespace obf2::app {
namespace {

// `normaliseIfNeeded` (OpenBF2 FreeCamera.cpp): a vector already unit to within
// 1.19e-7 is left alone, a zero one stays zero, the rest is normalised.
void normaliseIfNeeded(float& x, float& y, float& z) {
  const float lengthSquared = x * x + y * y + z * z;
  if (std::fabs(lengthSquared - 1.0f) < 1.192092896e-07f) return;
  if (std::fabs(lengthSquared) < 1.192092896e-07f) {
    x = y = z = 0.0f;
    return;
  }
  const float inverse = 1.0f / std::sqrt(lengthSquared);
  x *= inverse;
  y *= inverse;
  z *= inverse;
}

struct Quat4 {
  float x, y, z, w;
};

// `Quat::slerp` (OpenBF2 Math/Quat.cpp): the short way round, a plain lerp when
// the two are within 1e-6 of each other.
Quat4 slerp(const Quat4& a, const Quat4& b, float t) {
  const float dot = b.y * a.y + b.x * a.x + b.z * a.z + a.w * b.w;
  const bool negative = dot < 0.0f;
  const float c = negative ? -dot : dot;
  float scale0 = 1.0f - t;
  float scale1 = t;
  if (1.0f - c > 0.000001f) {
    const float theta = std::acos(std::min(1.0f, c));
    const float sinTheta = std::sin(theta);
    scale0 = std::sin((1.0f - t) * theta) / sinTheta;
    scale1 = std::sin(t * theta) / sinTheta;
  }
  if (negative) scale1 = -scale1;
  return {a.x * scale0 + b.x * scale1, a.y * scale0 + b.y * scale1,
          a.z * scale0 + b.z * scale1, a.w * scale0 + b.w * scale1};
}

}  // namespace

void DeathCamera::start(const Vec3f& body, const Vec3f& forward, float radius) {
  active_ = true;
  // `resetTraceCamera`.
  offset_ = Vec3f{};
  distance_ = 0.0f;
  goal_ = std::max(radius * 3.0f, kDeathCamMinDistance);
  target_ = body;
  direction_ = forward;
  offsetW_ = 1.0f;
  eased_ = body;
}

DeathCamera::View DeathCamera::update(float dt, const std::optional<Vec3f>& body,
                                      const std::optional<Vec3f>& killer,
                                      const std::function<float(float, float)>& ground) {
  const bool first =
      offset_.x * offset_.x + offset_.y * offset_.y + offset_.z * offset_.z <= 0.0f;
  if (body) target_ = *body;

  // The wanted direction is taken before the killer may change the stored one.
  Quat4 wanted{-direction_.x, -direction_.y + 0.3f, -direction_.z, 1.0f};
  normaliseIfNeeded(wanted.x, wanted.y, wanted.z);

  if (killer && distance_ > goal_ * 0.8f) {
    direction_ = Vec3f{killer->x - target_.x, 0.0f, killer->z - target_.z};
    normaliseIfNeeded(direction_.x, direction_.y, direction_.z);
  }

  const float turn = std::clamp(dt * kCamStiffness, 0.0f, 1.0f);
  if (first) {
    offset_ = Vec3f{-direction_.x * 0.1f, -direction_.y * 0.1f, -direction_.z * 0.1f};
    distance_ = 0.5f;
    eased_ = target_;
  } else {
    Quat4 blended = slerp(Quat4{offset_.x, offset_.y, offset_.z, offsetW_}, wanted, turn);
    normaliseIfNeeded(blended.x, blended.y, blended.z);
    offset_ = Vec3f{blended.x, blended.y, blended.z};
    offsetW_ = blended.w;
  }

  // Only an upper clamp, as in the engine.
  const float attach = std::min(dt * kCamInterpolateAttachObject, 1.0f);
  eased_ = eased_ + (target_ - eased_) * attach;

  distance_ = goal_ * turn + distance_ * (1.0f - turn);

  View view;
  view.eye = eased_ + offset_ * distance_;
  if (ground) {
    const float floor = ground(view.eye.x, view.eye.z) + kDeathCamTerrainOffset;
    if (floor > view.eye.y) view.eye.y = floor;
  }
  view.target = target_;
  return view;
}

}  // namespace obf2::app
