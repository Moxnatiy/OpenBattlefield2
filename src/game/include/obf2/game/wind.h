#pragma once

// The world's wind: WindManager and the WindAffector component, as BF2.exe
// runs them. Notes and addresses in docs/functions/wind.md.

#include <memory>
#include <optional>
#include <vector>

#include "obf2/core/math.h"

namespace obf2::game {

class ObjectTemplate;

// `windType` names, in the order of the table at BF2.exe 0x9767e0.
enum class WindType { Directional, Point, RadialDir, TurbulenceField };

// WindAffectorTemplate (ctor 0x640af0; the writer 0x640810 names the fields).
struct WindAffectorTemplate {
  WindType type = WindType::Directional;  // +0x10, ctor "Directional"
  bool isDynamic = true;                  // +0x0c, ctor 1; not read by the wind query
  Vec3f direction{0, -1, 0};              // +0x30
  float speed = 0;                        // +0x3c
  float falloff = 10;                     // +0x40, also the bounding radius (slot +0x54)
  float radialFalloff = 1;                // +0x44
  float blowTime = -1;                    // +0x48, getter slot +0x64 (0x65a0a0)

  // The `WindAffector` component of a template; nullopt without one.
  static std::optional<WindAffectorTemplate> fromDefinition(const ObjectTemplate& definition);
};

// WindAffector (vtable 0x909980, ctor 0x67ea10).
class WindAffector {
 public:
  WindAffector(const WindAffectorTemplate& tmpl, Vec3f position, const Mat4& orientation = Mat4::identity())
      : tmpl_(tmpl), position_(position), orientation_(orientation), remaining_(tmpl.blowTime) {}

  void setPosition(Vec3f position) { position_ = position; }
  void setOrientation(const Mat4& orientation) { orientation_ = orientation; }
  Vec3f position() const { return position_; }        // slot +0x48: the object's position
  float boundingRadius() const { return tmpl_.falloff; }  // slot +0x44
  bool active() const { return active_; }             // slot +0x3c
  void setActive(bool active) { active_ = active; }   // slot +0x40 (0x6d87b0)
  // Slot +0x38 (0x67e630): counts the blow time down when it is above 0; at
  // ≤ 0 the affector leaves the manager and turns inactive. False then.
  bool update(float dt);
  // Slot +0x4c (0x67e740).
  void directionAndSpeed(Vec3f at, Vec3f& direction, float& speed) const;
  // Slot +0x50 (0x67e6d0): direction · speed, zero when inactive.
  Vec3f wind(Vec3f at) const;

 private:
  WindAffectorTemplate tmpl_;
  Vec3f position_;
  Mat4 orientation_;
  bool active_ = true;  // +0x10, ctor 1
  float remaining_;     // +0x14, the blow time at init (0x67e600)
};

// WindManager (BF2.exe vtable 0x920470, ctor 0x729ff0; global 0xa08f64).
class WindManager {
 public:
  // `globalWindSpeed`, `globalWindDirection` (0x729f20, 0x729f60). No shipped
  // level sets either.
  void setGlobalSpeed(float speed) { globalSpeed_ = speed; }
  void setGlobalDirection(Vec3f direction) { globalDirection_ = direction; }
  void add(const WindAffector* affector);     // slot +0x40
  void remove(const WindAffector* affector);  // slot +0x44
  // Slot +0x3c (0x729720), what RendDX9.dll's particles read: the global
  // speed · direction plus the wind of every active affector the point is
  // inside the bounding radius of (slot +0x18, 0x729cd0, with radius 0).
  Vec3f wind(Vec3f at) const;
  const std::vector<const WindAffector*>& affectors() const { return affectors_; }

 private:
  float globalSpeed_ = 0;              // +0x2c, ctor 0
  Vec3f globalDirection_{0, 0, 1};     // +0x30, ctor 0/0/1
  std::vector<const WindAffector*> affectors_;
};

// An affector that is in a manager for as long as it lives: init (0x67e600)
// adds it, destroy (0x67e680) removes it.
class ScopedWindAffector {
 public:
  ScopedWindAffector(WindManager& manager, const WindAffectorTemplate& tmpl)
      : manager_(&manager), affector_(std::make_unique<WindAffector>(tmpl, Vec3f{})) {
    manager_->add(affector_.get());
  }
  ScopedWindAffector(ScopedWindAffector&&) noexcept = default;
  ScopedWindAffector& operator=(ScopedWindAffector&& other) noexcept {
    if (this != &other) {
      release();
      manager_ = other.manager_;
      affector_ = std::move(other.affector_);
    }
    return *this;
  }
  ~ScopedWindAffector() { release(); }
  WindAffector& operator*() { return *affector_; }
  WindAffector* operator->() { return affector_.get(); }

 private:
  void release() {
    if (affector_) manager_->remove(affector_.get());
    affector_.reset();
  }
  WindManager* manager_;
  std::unique_ptr<WindAffector> affector_;
};

}  // namespace obf2::game
