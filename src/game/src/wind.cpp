#include "obf2/game/wind.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "obf2/game/object_template.h"

namespace obf2::game {
namespace {

// The engine's normalize (BF2.exe 0x4639b0): left as is when |v|² is within
// 2⁻²³ of 1 (0x89e4e8), zero when |v|² is below that, else scaled.
Vec3f normalized(Vec3f v) {
  constexpr float kEpsilon = 1.1920929e-7f;
  const float squared = dot(v, v);
  if (std::abs(squared - 1.0f) < kEpsilon) return v;
  if (std::abs(squared) < kEpsilon) return {};
  return v * (1.0f / std::sqrt(squared));
}

// 0x5a1770: a row vector times the matrix's 3×3.
Vec3f rotated(const Mat4& m, Vec3f v) {
  return {v.x * m.m[0] + v.y * m.m[4] + v.z * m.m[8], v.x * m.m[1] + v.y * m.m[5] + v.z * m.m[9],
          v.x * m.m[2] + v.y * m.m[6] + v.z * m.m[10]};
}

}  // namespace

std::optional<WindAffectorTemplate> WindAffectorTemplate::fromDefinition(const ObjectTemplate& d) {
  const auto* component = d.component("WindAffector");
  if (!component) return std::nullopt;
  const auto last = [&](const char* key) -> const Property* {
    if (const auto found = component->properties.find(key);
        found != component->properties.end() && !found->second.empty())
      return &found->second.back();
    return d.property(key);
  };
  WindAffectorTemplate t;
  if (const auto* p = last("windtype")) {
    std::string name(p->value());
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "directional") t.type = WindType::Directional;
    else if (name == "point") t.type = WindType::Point;
    else if (name == "radialdir") t.type = WindType::RadialDir;
    else if (name == "turbulencefield") t.type = WindType::TurbulenceField;
  }
  if (const auto* p = last("windisdynamic")) t.isDynamic = p->asBool().value_or(t.isDynamic);
  if (const auto* p = last("winddirection"))
    if (const auto v = p->asVec3()) t.direction = {v->x, v->y, v->z};
  if (const auto* p = last("windspeed")) t.speed = p->asFloat().value_or(t.speed);
  if (const auto* p = last("windfalloff")) t.falloff = p->asFloat().value_or(t.falloff);
  if (const auto* p = last("windradialfalloff")) t.radialFalloff = p->asFloat().value_or(t.radialFalloff);
  if (const auto* p = last("windblowtime")) t.blowTime = p->asFloat().value_or(t.blowTime);
  return t;
}

bool WindAffector::update(float dt) {
  if (active_ && remaining_ > 0) {
    remaining_ -= dt;
    if (remaining_ <= 0) active_ = false;
  }
  return active_;
}

void WindAffector::directionAndSpeed(Vec3f at, Vec3f& direction, float& speed) const {
  if (!active_) {
    speed = 0;
    return;
  }
  const auto& t = tmpl_;
  switch (t.type) {
    case WindType::Directional:
    case WindType::Point: {
      // Directional: the template direction turned by the object (virtual
      // +0x80, 0x5a1770). Point: away from the affector, normalized.
      direction = t.type == WindType::Directional ? rotated(orientation_, t.direction)
                                                  : normalized(at - position_);
      const Vec3f offset = at - position_;
      const float share = std::min(dot(offset, offset) / (t.falloff * t.falloff), 1.0f);
      speed = (1.0f - share) * t.speed;
      return;
    }
    case WindType::RadialDir: {
      const Vec3f axis = rotated(orientation_, t.direction);
      const Vec3f offset = at - position_;
      direction = offset;
      const float along = dot(offset, axis);
      speed = (1.0f - std::min(along / t.falloff, 1.0f)) * t.speed;
      Vec3f radial = offset - axis * along;
      const float squared = dot(radial, radial);
      if (t.radialFalloff * t.radialFalloff <= squared) {
        speed = 0;
        return;
      }
      // 0.2 at 0x978180.
      const float out = std::min(squared / (t.radialFalloff * t.radialFalloff) + 0.2f, 1.0f);
      radial = normalized(radial);
      direction = radial * out + axis * (1.0f - out);
      return;
    }
    case WindType::TurbulenceField:
      // No branch in 0x67e740: the caller's zeros stand.
      return;
  }
}

Vec3f WindAffector::wind(Vec3f at) const {
  if (!active_) return {};
  Vec3f direction{};
  float speed = 0;
  directionAndSpeed(at, direction, speed);
  return direction * speed;
}

void WindManager::add(const WindAffector* affector) {
  if (std::ranges::find(affectors_, affector) == affectors_.end()) affectors_.push_back(affector);
}

void WindManager::remove(const WindAffector* affector) { std::erase(affectors_, affector); }

Vec3f WindManager::wind(Vec3f at) const {
  Vec3f total = globalDirection_ * globalSpeed_;
  for (const auto* affector : affectors_) {
    if (!affector->active()) continue;
    const Vec3f offset = affector->position() - at;
    const float reach = affector->boundingRadius();
    if (dot(offset, offset) < reach * reach) total = total + affector->wind(at);
  }
  return total;
}

}  // namespace obf2::game
