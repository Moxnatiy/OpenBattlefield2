#pragma once

#include <algorithm>

namespace obf2::app {

// Normal visible decals: RendDX9.dll constructor 0x101257e7/+0x10 defaults
// to 20 seconds; 0x101257ee/+0x1c defaults to 0.85, deriving start 17 / interval 3.
// Alpha writer 0x101aae80 and removal 0x101ad400; repeat receiver measurements
// are saved under m4-effects-20261120/retail-receiver-single-preflight*.
// Identifier decals and off-screen receiver acceleration are not modeled here.
struct DecalLifetime {
  static constexpr float seconds = 20.0f;
  static constexpr float fadeStart = seconds * 0.85f;
  float age = 0.0f;

  void advance(float step) { age += step; }
  bool expired() const { return age >= seconds; }
  float opacity() const {
    return 1.0f - std::clamp((age - fadeStart) / (seconds - fadeStart), 0.0f, 1.0f);
  }
};

}  // namespace obf2::app
