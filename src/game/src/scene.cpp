#include "obf2/game/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include "obf2/core/path.h"
#include <limits>

namespace obf2::game {

Mat4 composeTurretRotation(const Mat4& parentMotion, const Mat4& rest,
                          const Mat4& parentRest, Vec3f angles, bool stabilizeYaw) {
  if (stabilizeYaw) {
    const auto forward=transformDirection(parentMotion*parentRest,{0,0,1});
    float heading=std::asin(std::clamp(forward.x,-1.0f,1.0f))*180/std::numbers::pi_v<float>;
    if (forward.z<0) heading=180-heading;
    angles.x-=heading;
  }
  return parentMotion*turretRotationAroundRest(rest,angles);
}

std::optional<int> engineTypeValue(std::string_view name) {
  const auto key = normalizeAssetPath(name);
  static constexpr std::pair<std::string_view, int> types[] = {
      {"c_etplane", 0x1}, {"c_etcar", 0x2}, {"c_ettank", 0x6}, {"c_etship", 0x9},
      {"c_etrocket", 0x11}, {"c_ettorpedo", 0x19}, {"c_ethelicopter", 0x21},
      {"c_etnewcar", 0x42}, {"c_etnewcar2", 0xc2}, {"c_etnewtank", 0x46}};
  for (const auto& [text, value] : types) {
    if (key == text) return value;
  }
  return std::nullopt;
}

namespace {

// FUN_006a5e20's property comparison of two rotational bundle templates.
// Missing properties compare as their default (zero, no input). The byte at
// +0x298 is not mapped to a property and is not compared.
bool sameNetworkedRotation(const ObjectTemplate* a, const ObjectTemplate* b) {
  if (!a || !b) return false;
  for (const auto name : {"setInputToYaw", "setInputToPitch", "setInputToRoll"}) {
    if (normalizeAssetPath(a->text(name)) != normalizeAssetPath(b->text(name))) return false;
  }
  for (const auto name : {"setMinRotation", "setMaxRotation", "setMaxSpeed", "setAcceleration"}) {
    const auto vector = [&](const ObjectTemplate* t) {
      const auto* value = t->property(name);
      const auto data = value ? value->asVec3() : std::nullopt;
      return data ? std::array{data->x, data->y, data->z} : std::array{0.0f, 0.0f, 0.0f};
    };
    if (vector(a) != vector(b)) return false;
  }
  return true;
}

}  // namespace

std::vector<DescriptorMember> vehicleDescriptorMembers(const Registry& registry, std::string_view vehicle,
                                                       unsigned seat) {
  const auto instance = flattenObject(registry, vehicle);
  if (!instance) return {};
  std::vector<std::size_t> path;
  std::vector<int> seatAt(instance->parts.size(), -1);
  // The nearest Engine above each part (Spring::getEngine, Linux 0x63b740).
  std::vector<std::optional<int>> engineAbove(instance->parts.size());
  int nextSeat = 0;
  std::vector<DescriptorMember> result;
  for (std::size_t i = 0; i < instance->parts.size(); ++i) {
    const auto& part = instance->parts[i];
    path.resize(static_cast<std::size_t>(part.depth));
    const auto* object = registry.find(part.templateName);
    const int parentSeat = path.empty() ? -1 : seatAt[path.back()];
    seatAt[i] = part.className == "PlayerControlObject" && object && object->property("setNetworkableInfo")
        ? nextSeat++ : parentSeat;
    engineAbove[i] = path.empty() ? std::nullopt : engineAbove[path.back()];
    std::optional<int> engineType;
    if (part.className == "Engine" && object) {
      engineType = engineTypeValue(object->text("setEngineType"));
      engineAbove[i] = engineType;
    }
    path.push_back(i);
    // BF2.exe 0x6a8970: descriptor members are in tree order. Decoding stops
    // at the first member whose layout is not read: T90's firearm intervenes
    // before its HUD spring. GenericFireArm has its class networkable even
    // without an explicit setNetworkableInfo property on the template.
    if (seatAt[i]==static_cast<int>(seat) && part.className=="GenericFireArm") break;
    if (seatAt[i] != static_cast<int>(seat) || !object ||
        !object->property("setNetworkableInfo") || part.className=="PlayerControlObject") continue;
    // BF2.exe 0x6aaa10 groups the walked objects: one that FUN_006a5e20 matches
    // with an earlier group's first object joins that group and has no
    // networkable of its own; it takes that object's state. Matched: the same
    // template, or two rotational bundles whose templates agree on the byte
    // +0x298, inputToYaw/Pitch/Roll (+0x28c/+0x290/+0x294, getters 0x4be0d0,
    // 0x63cb20, 0x63cb30) and four vectors (+0x22c, +0x238, +0x250, +0x268),
    // compared here as min/maxRotation, maxSpeed and acceleration
    // (docs/functions/network-events.md, "Shared networkables").
    if (const auto shared = std::ranges::find_if(result, [&](const DescriptorMember& earlier) {
          if (normalizeAssetPath(earlier.templateName) == normalizeAssetPath(part.templateName)) return true;
          // Same class id (Wing 0x9483 with Wing, RotationalBundle 0x947d with
          // RotationalBundle), or RotationalBundle with Camera (0x9472), and the
          // earlier one IsA RotationalBundle (Linux CID_*Template, .rodata).
          const bool classes = earlier.className == part.className ||
              (earlier.className == "RotationalBundle" && part.className == "Camera");
          if (earlier.kind != DescriptorMember::Kind::Rotational || !classes) return false;
          return sameNetworkedRotation(registry.find(earlier.templateName), object);
        });
        shared != result.end()) {
      shared->sharing.push_back(part.templateName);
      continue;
    }
    DescriptorMember member;
    member.templateName = part.templateName;
    member.className = part.className;
    // A Wing (WingTemplate, class 0x9483) inherits RotationalBundle's
    // networkable (vtable slot initNetworkableComp is
    // RotationalBundle::initNetworkableComp, Linux) and its rotation step.
    if (part.className == "RotationalBundle" || part.className == "Wing") {
      member.kind = DescriptorMember::Kind::Rotational;
    } else if (part.className == "Engine") {
      // EngineNetworkable::setNetUpdate, Linux 0x5c9bd0. Without a known
      // engine type the record cannot be cut. The gear count defaults to 1
      // (EngineTemplate::EngineTemplate, Linux 0x632538, +0x448); the M1A2
      // sets none, and its 2-bit gear field is measured on the live stream.
      const auto gears = object->number("setNumberOfGears").value_or(1);
      if (!engineType || gears < 0) break;
      member.kind = DescriptorMember::Kind::Engine;
      const auto values = static_cast<unsigned>(gears) + 1;
      while (((1u << member.gearBits) - 1) < values) ++member.gearBits;
      member.engineField100 = (*engineType & 0x20) != 0;
      member.engineBlock400 = (*engineType & 0x80) != 0;
    } else if (part.className == "Spring") {
      // SpringNetworkable::setNetUpdate, Linux 0x5e0080. The extra field
      // needs the spring's engine type bit 7 and a second condition
      // (`+0x90`'s vtbl+0x100 & 8, reset 0x5dfa20); +0x90 is not named (not
      // the object, its template or its PhysicsSpring at +0x88). Taken as set:
      // the FAAV's moving records end on their last bit with it
      // (docs/functions/network-events.md).
      member.kind = DescriptorMember::Kind::Spring;
      member.springExtra = engineAbove[i] && (*engineAbove[i] & 0x80) != 0;
    } else {
      break;
    }
    result.push_back(std::move(member));
  }
  return result;
}

std::vector<std::string> vehicleTurretBundles(const Registry& registry, std::string_view vehicle,
                                             unsigned seat) {
  std::vector<std::string> result;
  for (const auto& member : vehicleDescriptorMembers(registry, vehicle, seat)) {
    if (member.kind == DescriptorMember::Kind::Rotational) result.push_back(member.templateName);
  }
  return result;
}

std::vector<std::vector<std::string>> vehicleTurretBundleObjects(const Registry& registry,
                                                                 std::string_view vehicle, unsigned seat) {
  std::vector<std::vector<std::string>> result;
  for (const auto& member : vehicleDescriptorMembers(registry, vehicle, seat)) {
    if (member.kind != DescriptorMember::Kind::Rotational) continue;
    auto& objects = result.emplace_back(1, member.templateName);
    objects.insert(objects.end(), member.sharing.begin(), member.sharing.end());
  }
  return result;
}

Mat4 turretRotationAroundRest(const Mat4& rest, Vec3f angles) {
  Mat4 inverse = Mat4::identity();
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) inverse.m[column * 4 + row] = rest.m[row * 4 + column];
  }
  const auto origin = transformPoint(rest, {});
  return rest * rotationYawPitchRoll(angles.x, angles.y, angles.z) *
         inverse * translation(origin * -1.0f);
}

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

namespace {

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
