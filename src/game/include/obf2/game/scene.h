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
#include <span>
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

// One networkable of a vehicle seat's descriptor that the client can decode
// (docs/functions/network-events.md, "Engine and Spring networkables").
struct DescriptorMember {
  enum class Kind { Rotational, Engine, Spring };
  Kind kind = Kind::Rotational;
  std::string templateName;
  // The object's class (RotationalBundle, Wing, Engine, Spring): FUN_006a5e20
  // compares rotation only between equal classes (or RotationalBundle/Camera).
  std::string className;
  // Engine: bits of the gear field (mask 0x2), from the template's number of
  // gears (EngineTemplate +0x448, Linux 0x630ee0).
  unsigned gearBits = 0;
  // Engine: getEngineType() & 0x20 reads mask 0x100; bit 7 reads mask 0x400.
  bool engineField100 = false;
  bool engineBlock400 = false;
  // Spring: the extra 1 + 16 bits (SpringNetworkable +0x74, set in reset,
  // Linux 0x5dfa20).
  bool springExtra = false;
  // Objects grouped with this one (BF2.exe 0x6aaa10, FUN_006a5e20): they have
  // no networkable of their own and take this member's state.
  std::vector<std::string> sharing;
};
// The consecutive decodable members of the seat's descriptor, in tree order
// (BF2.exe 0x6a8970). Stops at the first member whose layout is not read.
std::vector<DescriptorMember> vehicleDescriptorMembers(const Registry& registry, std::string_view vehicle,
                                                       unsigned seat);
// BF2 engine types (Linux operator<<(EngineType), 0x630fa0); nullopt for a name not listed.
std::optional<int> engineTypeValue(std::string_view name);

// Turret joints in the order of the retail descriptor for this vehicle seat:
// the rotational members of `vehicleDescriptorMembers`; empty when there are none.
std::vector<std::string> vehicleTurretBundles(const Registry& registry, std::string_view vehicle,
                                             unsigned seat);
// The same bundles, each with the objects that take its state (the bundle
// first, then DescriptorMember::sharing).
std::vector<std::vector<std::string>> vehicleTurretBundleObjects(const Registry& registry,
                                                                 std::string_view vehicle, unsigned seat);
Mat4 turretRotationAroundRest(const Mat4& rest, Vec3f angles);
// BF2.exe 0x66dcc0: subtract root-relative parent heading before applying yaw.
Mat4 composeTurretRotation(const Mat4& parentMotion, const Mat4& rest,
                          const Mat4& parentRest, Vec3f angles, bool stabilizeYaw);

// A child's place in its parent: `setPosition` after `setRotation`.
Mat4 childTransform(const ChildTemplate& child);

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
