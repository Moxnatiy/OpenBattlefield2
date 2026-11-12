#pragma once
// The soldier's pose: standing, crouching, prone — what the keys ask for, when
// the change happens, and how the speed blends across it.
//
//   `Soldier::updateSoldierPose`   Linux 0x5468d0 — the keys into a requested pose
//   `Soldier::soldierCanGoProne`   Linux 0x546840 — water and the two delays
//   `Soldier::executePoseChange`   Linux 0x54eb70 — the pose takes the request
//   `Soldier::updateSoldierSpeed`  Linux 0x54ec9d..0x54ece0 — the speed blend
//   `SoldierResponsePhysics::poseChangeTest`, `BF2.exe` 0x6a4450 — whether it fits
//
// Notes: docs/functions/soldier-physics.md, "The pose".
#include <functional>

#include "obf2/server/physics.h"

namespace obf2::server {

// The input's half of the pose (`updateSoldierPose`, 0x5468d0):
//
//   swimming (+0x48c) or skydiving (+0x4b0): nothing
//   prone key, its latch bit 2 not set:
//     requested = the pose if under 2, else 0 (prone toggles back up)
//     unless prone with `stand-delay-from-prone` (+0x5ac) run out:
//       `canGoProne` → requested = 2
//     prone → standing: +0x5a8 = jump-delay-after-prone, +0x5b0 =
//       prone-delay-from-stand, +0x5b8 = fire-delay-from-prone
//     standing → prone: +0x5ac = stand-delay-from-prone
//     the latch takes bit 2
//   no prone key: the latch drops bit 2, and then
//     crouch key: on its press (latch bit 1 not set) requested = 1, latch = 1;
//       prone with requested 1: the three delays as above
//     no crouch key: latch drops bits 1 and 2; a crouching soldier asks to stand
void updateSoldierPose(BodyState& body, bool prone, bool crouch, bool swimming, bool canGoProne,
                       const PhysicsConstants& constants);

// `soldierCanGoProne` (0x546840): the water no higher than the pivot plus the
// radius less `soldier-prone-inwater-limit`, and +0x5a4 (prone-delay-after-jump)
// and +0x5b0 (prone-delay-from-stand) both run out. `waterLevel` is the water's
// height at the soldier, or a very low number where there is none.
bool soldierCanGoProne(const BodyState& body, float waterLevel, const PhysicsConstants& constants);

// `executePoseChange` (0x54eb70): the speed of the old state is kept as the
// blend's start (+0x49c), the progress (+0x494) starts at 0, the pose takes the
// request, the speed state is recomputed, and the blend's length (+0x498) is the
// transition table's [old state][new state].
void executePoseChange(BodyState& body, bool sprinting, bool zoomed, float mouseSpeedMod,
                       const PhysicsConstants& constants);

// The speed after the blend (0x54ec9d..0x54ece0): while the length is over the
// progress, the progress grows by `dt` and the speed is
// `(speed - start) * progress / length + start`.
float blendPoseSpeed(BodyState& body, float speed, float dt);

// Whether the requested pose fits (`poseChangeTest`, 0x6a4450, from B2BF's
// reading): swimming refuses; going prone, and crouching from standing, always
// fit; standing up, and crouching from prone, are tested against the world.
// `fits(height)` answers whether a column that tall fits at the soldier — the
// engine's `expandSoldierCollision` is **not read**; ours tests the new pose's
// sphere column against the objects.
bool poseChangeFits(const BodyState& body, const std::function<bool(int pose)>& fits);

// The eye's height approach (`updateCameraTransformation`, B2BF's reading of
// Linux 0x5527a0): every input tick the camera moves `camera-movement-speed-on-*`
// of the way to the pose's camera position.
float poseCameraFollow(int pose, const PhysicsConstants& constants);

}  // namespace obf2::server
