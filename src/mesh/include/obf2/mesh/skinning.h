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

// Deforms the vertices by a pose. `bindPose` is the mesh as it lies in the file,
// `out` receives the moved positions and normals. Both are the same size.
void skinMesh(const RenderMesh& bindPose, const std::vector<Mat4>& boneWorld, RenderMesh& out);

}  // namespace obf2::mesh
