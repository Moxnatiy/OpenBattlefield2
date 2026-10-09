#pragma once
// The first-person camera spring: the sway of the arms and weapon against the
// eye when the soldier turns, moves and fires.
//
// Two halves, as in the engine. The numbers are an animation system's
// (`dice::anim::CameraSpring`, 0x50 bytes, written by `animationSystem.cameraSpring.*`
// in an `AnimationSystem1p.inc`); the state is the soldier's (+0x474, +0x478,
// +0x47c), stepped and turned into an offset by `Soldier::updateFirstPersonAnimations`
// (Linux server 0x552b30). docs/functions/soldier-model.md, "The camera spring".
#include "obf2/core/math.h"

namespace obf2::anim {

struct CameraSpringTemplate {
  // `animationSystem.cameraSpring.use` — whether the system has one at all.
  bool use = false;
  // The fields in their order in memory, as `CameraSpring::makeScript` (Linux
  // 0x6b9c10) writes them out: amplitude +0x00, zoomDamping +0x0c, stiffness
  // +0x18, move +0x24, hit +0x30, fire +0x3c, look +0x48/+0x4c. All zero from the
  // constructor (0x6b9850).
  Vec3f amplitude;
  Vec3f zoomDamping;
  Vec3f stiffness;
  Vec3f move;
  Vec3f hit;   // read by nothing in 0x552b30; purpose not established
  Vec3f fire;  // the pitch recoil's share
  float look[2] = {0.0f, 0.0f};

  // `CameraSpring::merge` (Linux 0x6b98b0): every field of `other` that is not
  // zero replaces ours — a vector when any of its three parts differs from zero
  // by more than 1.19e-7 (0xb2bfb0), `look` when |x| + |y| does. The update merges
  // the weapon's spring over the soldier's (0x553564).
  void merge(const CameraSpringTemplate& other);
};

// The soldier's spring state and one frame of it.
class CameraSpring {
 public:
  struct Input {
    float step = 0.0f;  // the frame's time, seconds
    // The last action's turn, yaw and pitch (+0x480, +0x484), in the look's own
    // units — `RemoteWorld::lastTurn`.
    float turn[2] = {0.0f, 0.0f};
    // The soldier's velocity less the ground's (`getCollidingMeshTangentSpeed`),
    // in the world, and the movement matrix's three axes: right, up, forward.
    Vec3f velocity;
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    Vec3f forward{0.0f, 0.0f, 1.0f};
    float pitchRecoil = 0.0f;  // `Soldier::getPitchRecoil` (0x548690); ours has none
  };

  // One frame, 0x552d55..0x5530d0.
  void update(const CameraSpringTemplate& spring, const Input& input);

  // How far the skeleton's root moves along the camera's own axes — right, up,
  // forward — this frame: the state times the amplitude, an amplitude below 0.001
  // (0xb2f3c4) counting as none (0x553197..0x5532c5). Without a zoomed weapon:
  // the zoom's blend toward `amplitude / (zoom factor × zoomDamping)` is not taken.
  Vec3f offset(const CameraSpringTemplate& spring) const;

  const Vec3f& state() const { return state_; }
  void reset() { state_ = Vec3f{}; }

 private:
  Vec3f state_;  // +0x474, +0x478, +0x47c
};

// `applySpringFunction(stiffness, value, step)` (Linux 0x552160): the value falls
// toward zero by `max(clamp, value² × stiffness × step)` and stops there — a value
// that would come out within 1e-4 of zero (0xb355d0, 0xb4fa50) becomes zero.
// `clamp` is `anim-soldier-spring-clamp`, 0.001 (`BF2.exe` 0x85aeb0, `0x3a83126f`).
void applySpringFunction(float stiffness, float& value, float step);

}  // namespace obf2::anim
