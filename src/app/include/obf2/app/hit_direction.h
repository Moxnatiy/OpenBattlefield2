#pragma once
// The hit direction on the HUD: `HitDirection` in HUD/HudSetup/HudElementsHit.con,
// a red `hitIndicator.tga` turned by `HitDirectionAngle` and shown by
// `HitDirectionShow` (in 0.1 s, out over 2 s). docs/functions/hud-hit-direction.md.
//
// The information layer (`HudInformationLayer`) holds it:
//   +0x19c  the time left               (0 at 0x468fd9)
//   +0x1a0  HitDirectionAngle           (0 at 0x468fdf)
//   +0x1a4  the time it shows: 0.1 s    (0x3dcccccd at 0x468fe5)
//   +0x1a8  HitDirectionShow            (0 at 0x468fef)
//
// HUD event 0x34 (0x465b50) sets the time left to +0x1a4 and calls 0x464aa0(1, 0);
// the layer's frame (0x467548) calls 0x464aa0(+0x1a8, dt). 0x464aa0:
//   show := the argument; while shown and the local player has a vehicle (+0x44)
//   whose +0x3c is set (purpose not established), the angle is
//   fmod(atan2(f.x, f.z), 2π) − getHitFromDirection(), f the camera matrix's third
//   row (`getCamera` +0x17c, its +0x10(0), +0x20/+0x28) flattened and normalised —
//   so it follows the player's turning while shown, and stays put while it fades;
//   then: time left ≤ 0 → time left := 0.1, hidden; else, if shown, −= dt.
#include "obf2/core/math.h"

namespace obf2::app {

class HitDirection {
 public:
  // HUD event 0x34.
  void cue(const Vec3f& cameraForward, float hitFromDirection);
  // The layer's frame.
  void update(float dt, const Vec3f& cameraForward, float hitFromDirection);
  void reset() { *this = {}; }

  bool show() const { return show_; }
  float angle() const { return angle_; }

 private:
  void apply(bool show, float dt, const Vec3f& cameraForward, float hitFromDirection);

  float timeLeft_ = 0.0f;  // +0x19c
  float angle_ = 0.0f;     // +0x1a0
  bool show_ = false;      // +0x1a8
};

inline constexpr float kHitDirectionTime = 0.1f;  // +0x1a4, `BF2.exe` 0x468fe5

}  // namespace obf2::app
