#include "obf2/game/scene.h"

#include <algorithm>
#include <limits>

namespace obf2::game {
namespace {

Mat4 childTransform(const ChildTemplate& child) {
  Mat4 transform = Mat4::identity();
  if (child.hasRotation) {
    transform = rotationYawPitchRoll(child.rotation.x, child.rotation.y, child.rotation.z);
  }
  if (child.hasPosition) {
    transform = translation(Vec3f{child.position.x, child.position.y, child.position.z}) * transform;
  }
  return transform;
}

void walk(const Registry& registry, const ObjectTemplate& object, const Mat4& parentTransform,
          int depth, int maxDepth, ObjectInstance& instance,
          std::vector<const ObjectTemplate*>& path) {
  // A guard against cycles in the data: a template must not occur twice on ONE
  // path from the root. Repeats in different branches are normal (six identical
  // wheels), but A->B->A would loop the walk to the depth limit.
  if (std::find(path.begin(), path.end(), &object) != path.end()) {
    ++instance.cycles;
    return;
  }
  path.push_back(&object);
  struct Pop {
    std::vector<const ObjectTemplate*>& v;
    ~Pop() { v.pop_back(); }
  } pop{path};

  instance.maxDepth = std::max(instance.maxDepth, depth);

  PartPlacement placement;
  placement.templateName = object.name;
  placement.className = object.className;
  placement.transform = parentTransform;
  placement.depth = depth;
  if (const Property* part = object.property("geometryPart")) {
    if (const auto value = part->asInt(0)) placement.geometryPart = *value;
  }
  placement.geometryName = std::string(object.text("geometry"));
  placement.file = object.file;
  instance.parts.push_back(std::move(placement));

  if (depth >= maxDepth) return;

  for (const ChildTemplate& child : object.children) {
    const ObjectTemplate* resolved = registry.find(child.name);
    if (resolved == nullptr) {
      ++instance.unresolved;
      continue;
    }
    // The transforms multiply from the top down: the turret relative to the hull,
    // the barrel relative to the turret.
    walk(registry, *resolved, parentTransform * childTransform(child), depth + 1, maxDepth,
         instance, path);
  }
}

}  // namespace

std::optional<ObjectInstance> flattenObject(const Registry& registry, std::string_view rootName,
                                            int maxDepth) {
  const ObjectTemplate* root = registry.find(rootName);
  if (root == nullptr) return std::nullopt;

  ObjectInstance instance;
  instance.rootName = root->name;
  instance.geometryName = std::string(root->text("geometry"));
  std::vector<const ObjectTemplate*> path;
  walk(registry, *root, Mat4::identity(), 0, maxDepth, instance, path);
  return instance;
}

std::unordered_map<int, Mat4> partTransformMap(const ObjectInstance& instance) {
  std::unordered_map<int, Mat4> out;
  for (const PartPlacement& placement : instance.parts) {
    if (placement.geometryPart < 0) continue;
    // When several templates claim one part, the first in walk order wins —
    // that is, the one closest to the root.
    out.emplace(placement.geometryPart, placement.transform);
  }
  return out;
}

std::size_t applyPartTransforms(mesh::RenderMesh& target,
                                const std::unordered_map<int, Mat4>& partTransforms) {
  if (target.vertexPart.size() != target.vertices.size()) return 0;

  std::size_t moved = 0;
  mesh::Aabb bounds{};
  bool first = true;

  for (std::size_t i = 0; i < target.vertices.size(); ++i) {
    const auto found = partTransforms.find(static_cast<int>(target.vertexPart[i]));
    mesh::Vertex& vertex = target.vertices[i];

    if (found != partTransforms.end()) {
      const Vec3f position = transformPoint(
          found->second, Vec3f{vertex.position.x, vertex.position.y, vertex.position.z});
      const Vec3f normal = transformDirection(
          found->second, Vec3f{vertex.normal.x, vertex.normal.y, vertex.normal.z});
      vertex.position = {position.x, position.y, position.z};
      vertex.normal = {normal.x, normal.y, normal.z};
      ++moved;
    }

    // The bounds are recomputed by us: the ones in the lod describe the mesh
    // before assembly, and after it they no longer match reality.
    if (first) {
      bounds.min = bounds.max = vertex.position;
      first = false;
    } else {
      bounds.min.x = std::min(bounds.min.x, vertex.position.x);
      bounds.min.y = std::min(bounds.min.y, vertex.position.y);
      bounds.min.z = std::min(bounds.min.z, vertex.position.z);
      bounds.max.x = std::max(bounds.max.x, vertex.position.x);
      bounds.max.y = std::max(bounds.max.y, vertex.position.y);
      bounds.max.z = std::max(bounds.max.z, vertex.position.z);
    }
  }

  if (!first) target.bounds = bounds;
  return moved;
}

}  // namespace obf2::game
