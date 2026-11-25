#pragma once
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "obf2/anim/player.h"
#include "obf2/server/physics.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// Soldier::updateJumpAndLandAnimation, BF2.exe 0x61ded0 / caller 0x61e1a0.
class JumpLandingSoundState {
 public:
  struct Result {
    bool jump = false;
    bool landing = false;
    std::optional<int> material;
    float animationUp = 0; // Recovered output, not consumed by the 1P pose yet.
  };
  using Trace = std::function<std::optional<int>(const Vec3f& motion)>;
  static Vec3f traceStart(const Vec3f& feet, float pivotHeight) {
    // Our BodyState stores feet; Retail subtracts literal 1 from root pivot.
    return feet + Vec3f{0, pivotHeight - 1.f, 0};
  }
  void reset() { jumped_ = landed_ = false; }
  Result update(bool grounded, const Vec3f& velocity, const server::PhysicsConstants& physics,
                const Trace& trace);
 private:
  bool jumped_ = false;
  bool landed_ = false;
};

class MovementSoundLatch {
 public:
  void reset() { half_ = true; }  // BF2.exe SoldierSound ctor 0x7e2920, +0x34.
  std::optional<unsigned> update(anim::Player::MovementTime phase, float speed,
                                 bool grounded, int pose, bool sprinting) {
    // BF2.exe 0x7e2f90: airborne dry movement returns before updating the latch.
    if (!grounded) return std::nullopt;
    if (phase.duration < 0.0f) phase = {0.0f, 0.0f}; // 0x61e790, dry fallback.
    const bool half = phase.time < phase.duration * 0.5f;
    const bool changed = half != half_;
    half_ = half;
    if (!changed || speed < 0.1f) return std::nullopt;
    switch (pose) {
      case 0: return sprinting ? 3u : 0u;
      case 1: return 1u;
      case 2: return 2u;
      default: return std::nullopt; // Water is deliberately outside this path.
    }
  }
 private:
  bool half_ = true;
};

struct InfantrySoundCue {
  std::string name;
  std::vector<std::string> files;
  float gain = 0.0f;
  float reverb = 0.0f;
};

class InfantrySounds {
 public:
  bool load(FileSystem& files);
  const InfantrySoundCue* cue(int material, unsigned slot) const;
 private:
  std::map<int, std::array<std::optional<InfantrySoundCue>, 4>> materials_;
};

}  // namespace obf2::app
