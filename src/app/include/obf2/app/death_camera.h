#pragma once
// What the player sees between his death and his next life.
//
// `GameLogic::killPlayer` (OpenBF2 GameLogic.cpp) puts the dead player's camera
// in view mode 6 with his soldier as the object (`camera->setViewMode(6,
// originalVehicle)`), and `FreeCamera::getTransformationDeath` (PE 0x5f4ed0, ELF
// 0x81e40f0, read whole in OpenBF2 FreeCamera.cpp) is the frame's camera from then
// on: it backs away from the body and turns, over a second or two, to look at it
// from behind its last facing — and once far enough out, from the side away from
// the one who killed him.
//
// `setViewMode(6)` (OpenBF2): the direction (+0x250..+0x258) is the body's
// matrix's third row, the target (+0x270) its position, the distance's goal
// (+0x27c) the root's visual bounding radius × 3 and at least 3, and the camera
// is computed once with a time of 0.
//
// Each frame (`getTransformationDeath`):
//   * the target follows the body while it exists;
//   * the wanted direction is −direction + (0, 0.3, 0), w 1, its xyz normalised;
//   * once the distance is past 0.8 of its goal and a killer is traced, the
//     direction becomes killer − target, flat, normalised;
//   * turn = clamp(dt × `cam-stiffness` 1.5, 0, 1); the first frame sets the
//     offset to −direction × 0.1, the distance to 0.5 and the eased target to the
//     target; later ones slerp the offset (w kept beside it) toward the wanted one;
//   * the eased target moves toward the target by min(dt × `cam-interpolate-
//     attachobject` 1.2, 1);
//   * distance = goal × turn + distance × (1 − turn);
//   * eye = eased target + offset × distance, not under the ground plus
//     `deathcam-terrainoffset` 0.8; it looks at the target, up is +Y.
#include <functional>
#include <optional>

#include "obf2/core/math.h"

namespace obf2::app {

inline constexpr float kCamStiffness = 1.5f;                // `cam-stiffness`
inline constexpr float kCamInterpolateAttachObject = 1.2f;  // `cam-interpolate-attachobject`
inline constexpr float kDeathCamTerrainOffset = 0.8f;       // `deathcam-terrainoffset`
inline constexpr float kDeathCamMinDistance = 3.0f;         // setViewMode: max(radius × 3, 3)

class DeathCamera {
 public:
  // `setViewMode(6, body)`: the body's position, its forward axis (its matrix's
  // third row) and its visual bounding radius.
  void start(const Vec3f& body, const Vec3f& forward, float radius);
  void stop() { active_ = false; }
  bool active() const { return active_; }

  struct View {
    Vec3f eye;
    Vec3f target;
  };
  // One frame: `body` while the dead soldier still exists, `killer` the traced
  // player's position while he is alive, `ground` the terrain's height.
  View update(float dt, const std::optional<Vec3f>& body, const std::optional<Vec3f>& killer,
              const std::function<float(float x, float z)>& ground);

  float distance() const { return distance_; }

 private:
  bool active_ = false;
  Vec3f direction_;        // +0x250..+0x258
  Vec3f offset_;           // +0x260..+0x268
  float offsetW_ = 1.0f;   // +0x26c
  float distance_ = 0.0f;  // +0x25c
  float goal_ = kDeathCamMinDistance;  // +0x27c
  Vec3f target_;           // +0x270
  Vec3f eased_;            // +0x280
};

}  // namespace obf2::app
