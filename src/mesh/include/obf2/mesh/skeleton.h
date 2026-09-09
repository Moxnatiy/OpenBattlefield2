#pragma once
// The Refractor 2 skeleton — the `.ske` files.
//
// The format is simple and fully verified against the game's data: the size
// computed from this layout matches the file's size byte for byte
// (`soldiers/Common/Animations/3p_setup.ske` — 80 bones, 3399 bytes).
//
//   u32  version (2)
//   u32  bone count
//   for every bone:
//     u16  the name's length including the zero
//     char name[length]
//     i16  parent (-1 at the root)
//     f32  rotation x, y, z, w (a quaternion)
//     f32  translation x, y, z
//
// The rotation and translation are **local, relative to the parent**: on a
// soldier it is exactly 0.075 from knee to shin and 0.385 from shin to foot.
// So this is the rest pose, not the inverse bind matrix.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "obf2/mesh/bf2_mesh.h"

namespace obf2::mesh {

// The name is separate from `mesh::Bone` in bf2_mesh.h: there it is a bone of
// the **mesh's rig** (an id plus a matrix), here a skeleton bone with a name and a hierarchy.
struct SkeletonBone {
  std::string name;
  int parent = -1;  // -1 at the root
  // The rotation quaternion (x, y, z, w) and the translation relative to the parent.
  float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  Vec3 position;
};

struct Skeleton {
  std::uint32_t version = 0;
  std::vector<SkeletonBone> bones;

  // A bone's index by name; -1 when there is none.
  int find(std::string_view name) const;
};

std::optional<Skeleton> loadSkeleton(std::span<const std::byte> bytes, std::string* error = nullptr);

}  // namespace obf2::mesh
