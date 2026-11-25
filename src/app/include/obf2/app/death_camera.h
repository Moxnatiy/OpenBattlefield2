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

// Man-down view (docs/functions/man-down.md, "The wounded view"). Message 0x18
// (0x5a7570) makes no view-mode switch: the soldier camera stays, and while the
// ragdoll is on, `Soldier::applyCameraAnimation` (BF2.exe 0x61cb60, called with an
// identity matrix from 0x5a2986) makes it from two soldier fields:
//   * going down (`Soldier::enableRagDoll`, BF2.exe 0x61fc4c; Linux 0x556f28):
//     angles = −getRotationZXY(last first-person camera matrix, soldier +0x374),
//     i.e. −(yaw, pitch, roll); their target = (0, −90, +90) turned by
//     setRotationZXY(0, 0, roll) — the identity for our camera, which has no roll;
//     the head = that matrix's position (+0x3a4).
//   * each frame (0x5a63a0): angles += (target − angles) · dt · 1.4
//     (`soldier-mandown-rotspeed`, 0x5a6525); head += (its target − head) · dt · 3.0
//     (`soldier-mandown-speed`, 0x5a651d).
//   * the camera (0x61cbfa): rotation setRotationZXY(−angles.z, −angles.y,
//     −angles.x), position the head. Settled, that is roll −90, pitch +90, yaw 0:
//     straight at the sky, as the retail capture shows.
inline constexpr float kManDownRotSpeed = 1.4f;   // `soldier-mandown-rotspeed`, 0x9ec358
inline constexpr float kManDownHeadSpeed = 3.0f;  // `soldier-mandown-speed`, 0x9ec2a4

// `setRotationZXY(m, z, x, y)` (Linux 0x732060): Z(z)·X(x)·Y(y), rows, degrees —
// X has row 2 (0, sin, cos), so +x turns the view up. Rows 1 and 2 are the view's
// up and forward.
struct Rotation3 { Vec3f row[3]; };
Rotation3 setRotationZXY(float zDegrees, float xDegrees, float yDegrees);

struct DownedView { Vec3f eye, target, up; };
class ManDownView {
 public:
  // `enableRagDoll`: the last first-person eye and the direction it looked.
  void start(const Vec3f& eye, const Vec3f& forward);
  void stop() { active_ = false; }
  bool active() const { return active_; }
  // One frame. `headTarget`: where the head eases to. The engine's is the
  // object's position plus the skeleton's vector at +0xbf0 (Linux 0x548d71),
  // which bone that is is **not established**; callers pass manDownHeadTarget.
  DownedView update(float dt, const Vec3f& headTarget);
  Vec3f angles() const { return angles_; }

 private:
  bool active_ = false;
  Vec3f angles_;  // soldier +0x35c (Linux +0x528)
  Vec3f target_;  // soldier +0x368 (Linux +0x534)
  Vec3f head_;    // soldier +0x350 (Linux +0x51c)
};

// Where our eye eases to: the body on the ground plus 0.22 m. **Not measured** —
// the engine's skeleton point (+0xbf0) is not identified (docs/debt.md).
Vec3f manDownHeadTarget(const Vec3f& body, float groundHeight);

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
