#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace obf2::game {

class ObjectTemplate;

struct RecoilDelta {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

class WeaponRecoil {
 public:
  void setWeapon(const ObjectTemplate* weapon);
  void onRound(std::uint32_t action, bool zoomed);
  RecoilDelta delta(std::uint32_t action) const;

 private:
  struct ForceRange { float low = 0.0f; float high = 0.0f; bool uniform = false; };
  ForceRange up_;
  ForceRange side_;
  float zoomModifier_ = 1.0f;
  std::vector<float> graph_;
  std::optional<std::uint32_t> firstRecoilAction_;
  RecoilDelta force_;
};

}  // namespace obf2::game
