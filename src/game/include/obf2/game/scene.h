#pragma once
// Assembling an object from an ObjectTemplate into geometry.
//
// In BF2 a vehicle is ONE .bundledmesh whose parts all lie in their own local
// coordinates, plus an ObjectTemplate tree that says where each part stands.
// The link between them is `ObjectTemplate.geometryPart N`: the part's number
// matches the value of the BLENDINDICES attribute in the vertices.
//
// That is why a .bundledmesh has no node matrices: the transforms live in the .con.
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/game/object_template.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::game {

struct PartPlacement {
  std::string templateName;
  std::string className;
  int geometryPart = -1;  // -1 = a template with no part of its own in the root's mesh
  Mat4 transform;         // accumulated from the root
  int depth = 0;

  // This node's own mesh, if it has one. A vehicle has one piece of geometry for
  // the whole tree and it lies at the root, whereas a control point has no mesh
  // of its own: `ObjectTemplate.create ControlPoint ...` and then
  // `addTemplate flagpole`, where the flag is a separate template with its own geometry.
  std::string geometryName;
  std::string file;  // where this template was created: the mesh file is searched from there
};

struct ObjectInstance {
  std::string rootName;
  std::string geometryName;  // the root's ObjectTemplate.geometry
  std::vector<PartPlacement> parts;

  // How many templates in the tree were not found in the registry. Not an error:
  // some vehicles refer to objects from other mods.
  int unresolved = 0;
  // How many times the walk met a template already on the current path.
  int cycles = 0;
  int maxDepth = 0;
};

// A walk of the child tree accumulating the transforms.
std::optional<ObjectInstance> flattenObject(const Registry& registry, std::string_view rootName,
                                            int maxDepth = 16);

// Puts every mesh part in its place: vertices with BLENDINDICES == N are
// transformed by part N's matrix. The indices and the materials' ranges are left
// alone — every vertex belongs to exactly one part, so the buffer never has to
// be split.
// Returns the number of vertices transformed.
std::size_t applyPartTransforms(mesh::RenderMesh& target,
                                const std::unordered_map<int, Mat4>& partTransforms);

// A convenience wrapper: build the map "part number -> transform" from an instance.
std::unordered_map<int, Mat4> partTransformMap(const ObjectInstance& instance);

}  // namespace obf2::game
