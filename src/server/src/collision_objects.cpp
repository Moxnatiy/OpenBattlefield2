#include "obf2/server/collision_objects.h"

#include <cstdio>

#include "obf2/core/path.h"
#include "obf2/game/scene.h"

namespace obf2::server {
namespace {

// Deeper than any tree in the game; a guard against a cycle in the data.
constexpr int kMaxDepth = 16;

int collisionPart(const game::ObjectTemplate& object) {
  if (const game::Property* part = object.property("collisionPart")) {
    if (const auto value = part->asInt(0)) return *value;
  }
  return 0;
}

}  // namespace

const mesh::CollisionMesh* CollisionLibrary::meshOf(const game::ObjectTemplate& owner) {
  const std::string_view name = owner.text("collisionMesh");
  if (name.empty()) return nullptr;
  const std::string_view dir = assetParentDir(owner.file);
  for (const char* subdirectory : {"meshes/", ""}) {
    const std::string path =
        joinAssetPath(dir, std::string(subdirectory) + std::string(name) + ".collisionmesh");
    const auto cached = meshes_.find(path);
    if (cached != meshes_.end()) return cached->second.get();
    if (!files_.exists(path)) continue;
    std::unique_ptr<mesh::CollisionMesh> loaded;
    if (const auto bytes = files_.read(path)) {
      if (auto parsed = mesh::loadCollisionMesh(*bytes)) {
        loaded = std::make_unique<mesh::CollisionMesh>(std::move(*parsed));
      }
    }
    return meshes_.emplace(path, std::move(loaded)).first->second.get();
  }
  return nullptr;
}

const std::vector<CollisionPiece>& CollisionLibrary::soldierPieces(
    const std::string& templateName) {
  const auto cached = pieces_.find(templateName);
  if (cached != pieces_.end()) return cached->second;

  std::vector<CollisionPiece> pieces;
  if (const game::ObjectTemplate* root = registry_.find(templateName)) {
    if (const mesh::CollisionMesh* mesh = meshOf(*root)) {
      if (const auto* layer = mesh->validLayer(0, 0, mesh::ColType::Soldier)) {
        pieces.push_back({layer, Mat4::identity()});
      }
      walkChildren(*root, Mat4::identity(), *mesh, pieces, 1);
    }
  }
  return pieces_.emplace(templateName, std::move(pieces)).first->second;
}

void CollisionLibrary::walkChildren(const game::ObjectTemplate& parent,
                                    const Mat4& parentTransform, const mesh::CollisionMesh& mesh,
                                    std::vector<CollisionPiece>& out, int depth) {
  if (depth > kMaxDepth) return;
  // The child list is taken in `addTemplate` order; the order the engine links
  // an object's children in is not established.
  for (const game::ChildTemplate& child : parent.children) {
    const game::ObjectTemplate* object = registry_.find(child.name);
    if (object == nullptr) continue;
    const Mat4 transform = parentTransform * game::childTransform(child);
    // A child with a mesh of its own ends the walk (0x574760's loop condition);
    // it carries its own tree the way the root does.
    if (const mesh::CollisionMesh* own = meshOf(*object)) {
      if (const auto* layer = own->validLayer(0, 0, mesh::ColType::Soldier)) {
        out.push_back({layer, transform});
      }
      walkChildren(*object, transform, *own, out, depth + 1);
      return;
    }
    const int part = collisionPart(*object);
    if (part > 0) {
      if (const auto* layer =
              mesh.validLayer(static_cast<std::size_t>(part), 0, mesh::ColType::Soldier)) {
        out.push_back({layer, transform});
      }
    }
    walkChildren(*object, transform, mesh, out, depth + 1);
  }
}

std::unique_ptr<CollisionWorld> buildCollisionWorld(CollisionLibrary& library,
                                                    const std::vector<level::StaticObject>& objects) {
  auto world = std::make_unique<CollisionWorld>();
  int withCollision = 0, withoutCollision = 0;
  for (const level::StaticObject& object : objects) {
    const auto& pieces = library.soldierPieces(object.templateName);
    if (pieces.empty()) {
      ++withoutCollision;
      continue;
    }
    // Vegetation arrives as a ready matrix, the rest as a position with angles.
    Mat4 transform = object.transform;
    if (!object.hasTransform) {
      transform = translation(object.position);
      if (object.hasRotation) {
        transform = transform * rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                     object.rotation.z);
      }
    }
    for (const CollisionPiece& piece : pieces) world->addLayer(*piece.layer, transform * piece.local);
    ++withCollision;
  }
  std::printf("  collision: %d objects, %zu triangles in %zu cells (without geometry %d)\n",
              withCollision, world->triangleCount(), world->cellCount(), withoutCollision);
  return world;
}

}  // namespace obf2::server
