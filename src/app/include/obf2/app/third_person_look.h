#pragma once
// Where a third-person soldier looks: the spine and head turned after the clips.
//
// `Soldier::updateThirdPersonAnimations` (Linux server 0x555020) measures the
// camera against the soldier's body, spreads the turn over the spine and head
// and hands each bone a post rotation (`mesh::BonePost`). docs/functions/soldier-look-3p.md.
#include <cstdint>
#include <vector>

#include "obf2/mesh/skinning.h"

namespace obf2::app {

// The bones of `Objects/Soldiers/Common/Animations/3p_setup.ske` (`ske_info`)
// whose post transform the function writes: skeleton Bone +0x40, bones 0x50
// apart (0x555020 stores at +0x3b0, +0x400, +0x450, +0xef0, +0x1760).
inline constexpr std::uint32_t kLookSpine2 = 11;  // +0x3b0
inline constexpr std::uint32_t kLookSpine3 = 12;  // +0x400
inline constexpr std::uint32_t kLookTorso = 13;   // +0x450
inline constexpr std::uint32_t kLookHead = 47;    // +0xef0
inline constexpr std::uint32_t kLookMesh15 = 74;  // +0x1760, the back gear part

// The pitch limits, degrees, positive down. Registered by the Linux server at
// 0x551afb..0x551b46 (`tools/linuxded/vars_defaults.py 0x551a00 0x551c00`); the
// game's data sets none of them.
inline constexpr float kSoldierLookUp = -70.0f;       // soldier-lookUp
inline constexpr float kSoldierLookDown = 70.0f;      // soldier-lookDown
inline constexpr float kSoldierProneLookUp = -30.0f;  // soldier-proneLookUp
inline constexpr float kSoldierProneLookDown = 5.0f;  // soldier-proneLookDown

// The posts for one soldier.
//   pitch   degrees, positive down (the soldier state's 0x10, docs in soldier_state.h);
//   aimYaw  degrees, the look's yaw from the body (the state's 0x4);
//   pose    the state's pose: 0 stand, 1 crouch, 2 prone, 3 swim.
// The head's own yaw (the idle look, `soldier-lookIdleTime` / `lookIdleAngle`,
// this +0x404..+0x414) is not ported: it is 0 here.
std::vector<mesh::BonePost> soldierLookPosts(float pitch, float aimYaw, std::uint32_t pose);

}  // namespace obf2::app
