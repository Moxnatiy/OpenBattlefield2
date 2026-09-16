#pragma once
// Skeletal animation: a pose from a skeleton and a clip, and mesh deformation.
//
// The chain is the same as in the original:
//
//   .ske  — bones with a local rotation and translation (the rest pose);
//   .baf  — a rotation and translation per frame for some of the bones;
//   .skinnedmesh — vertices with a pair of bone ids **in the rig** and a weight,
//                  plus the rig itself: the bone's index in the skeleton and the
//                  inverse bind matrix.
//
// The matrix a vertex is moved by:
//   world(bone) * inverse_bind(rig)
#include <cstddef>
#include <cstdint>
#include <vector>

#include "obf2/mesh/animation.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/skeleton.h"

namespace obf2::mesh {

// One clip in a pose: the animation, the frame and the weight.
//
// The model comes from the engine (`Skeleton::applySimpleAnimationStage`): every
// bone has its own stack of applied clips, **no more than five**, and a clip with
// a weight of 1 or more **clears** that stack — that is, owns the bone entirely.
// A clip touches only the bones listed in itself, and that is exactly how BF2
// gets the upper body from the weapon and the legs from the movement at once.
struct PoseStage {
  const BoneAnimation* animation = nullptr;
  std::uint32_t frame = 0;
  float weight = 1.0f;
};

// How many clips the engine keeps per bone.
inline constexpr int kMaxPoseStagesPerBone = 5;

// A pose made of several clips.
std::vector<Mat4> poseSkeleton(const Skeleton& skeleton, const std::vector<PoseStage>& stages);

// The world matrices of every bone. When a clip is given, the bones in it take
// their rotation and translation at the stated frame, and the rest stay at rest.
//
// The hierarchy in the file is already ordered (a parent always precedes its
// child), so one forward pass is enough.
std::vector<Mat4> poseSkeleton(const Skeleton& skeleton, const BoneAnimation* animation,
                               std::uint32_t frame);

// The matrices one rig's bones move their vertices by: for every entry of the
// rig, `world(bone) * inverse_bind`. An entry whose bone is not in the pose
// gets the identity rather than being dropped, so the numbering the vertices
// index by is kept.
//
// This is what the original hands the vertex shader as `mBoneArray`
// (`Shaders_client.zip:SkinnedMesh.fx:46`, `mat4x3 mBoneArray[26] :
// BoneArray`) — one array per material, because a vertex's pair of bone ids
// indexes its own material's rig and nothing else.
// The result is written into `out` rather than returned: a frame asks for it
// once per range per soldier, and the caller's vector is then allocated once.
void rigPalette(const std::vector<Bone>& bones, const std::vector<Mat4>& boneWorld,
                std::vector<Mat4>& out);

// How many bones one rig may have. The engine's own limit, and the size of the
// shader's array (`Shaders_client.zip:SkinnedMesh.fx:46`).
inline constexpr std::size_t kMaxRigBones = 26;

// Deforms the vertices by a pose. `bindPose` is the mesh as it lies in the file,
// `out` receives the moved positions and normals. Both are the same size.
void skinMesh(const RenderMesh& bindPose, const std::vector<Mat4>& boneWorld, RenderMesh& out);

// Appends one skinned mesh to another that is posed on the same skeleton — a
// soldier's body and the kit worn over it. Ranges keep their own rigs: the rig
// numbers of the appended mesh are moved past the target's, and its vertices keep
// their bindings.
void appendSkinned(RenderMesh& target, const RenderMesh& source);

// The bone a weapon's first part hangs on, and it is the engine's own number.
// The tail of `Soldier::updateThirdPersonAnimations` (Linux server 0x555020)
// takes the weapon out (`queryInterface(IID_IWeaponObject)`, 0x5560ba), asks it
// for its geometry (vtable +0x2c0) and that for its number of parts (+0xc0),
// **caps the count at eight** (0x5560eb), fills a list with `0x40 + i` for each
// one (0x556110) and hands it to `Skeleton::transformUsedBones` (0x55614a).
//
// 0x40 is 64, which is `mesh1` in `3p_setup.ske`, and `mesh1`..`mesh8` are
// exactly the eight the cap allows (`ske_info`). The weapons' own clips agree:
// the M4's mesh has five parts and `3p_m4_crouchStill.baf` animates bones 64..68,
// the knife has one and `3p_Knife_crouchStrafeLeft.baf` animates 64 alone
// (`baf_info`).
inline constexpr std::uint32_t kFirstWeaponBone = 64;

// How many of a weapon's parts the skeleton can carry — the cap at 0x5560eb.
inline constexpr std::size_t kMaxWeaponParts = 8;

// A BundledMesh whose parts are carried by bones, given the bindings that let it
// be posed like a skinned one: every vertex is tied whole (weight 1) to the bone
// `firstBone + its part`, and every range gets a rig naming those bones with an
// identity inverse bind — a part's vertices are already in its bone's own frame.
//
// This is how a weapon reaches the soldier's hands without a path of its own: it
// is appended to his mesh (`appendSkinned`) and skinned by the same shader.
// `vertexPart` empty leaves the mesh untouched.
void bindPartsToBones(RenderMesh& mesh, std::uint32_t firstBone = kFirstWeaponBone);

}  // namespace obf2::mesh
