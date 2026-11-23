#include "obf2/server/collision_objects.h"

#include <cctype>
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

const std::vector<CollisionPiece>& CollisionLibrary::pieces(const std::string& templateName,
                                                           mesh::ColType type) {
  auto& cache = pieces_[static_cast<std::size_t>(type) & 3];
  const auto cached = cache.find(templateName);
  if (cached != cache.end()) return cached->second;

  std::vector<CollisionPiece> pieces;
  if (const game::ObjectTemplate* root = registry_.find(templateName)) {
    if (const mesh::CollisionMesh* mesh = meshOf(*root)) {
      if (const auto* layer = mesh->validLayer(0, 0, type)) {
        pieces.push_back({layer, Mat4::identity(), root});
      }
      walkChildren(*root, Mat4::identity(), *mesh, type, pieces, 1);
    }
  }
  return cache.emplace(templateName, std::move(pieces)).first->second;
}

void CollisionLibrary::walkChildren(const game::ObjectTemplate& parent,
                                    const Mat4& parentTransform, const mesh::CollisionMesh& mesh,
                                    mesh::ColType type, std::vector<CollisionPiece>& out, int depth) {
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
      if (const auto* layer = own->validLayer(0, 0, type)) {
        out.push_back({layer, transform, object});
      }
      walkChildren(*object, transform, *own, type, out, depth + 1);
      return;
    }
    const int part = collisionPart(*object);
    if (part > 0) {
      if (const auto* layer =
              mesh.validLayer(static_cast<std::size_t>(part), 0, type)) {
        out.push_back({layer, transform, object});
      }
    }
    walkChildren(*object, transform, mesh, type, out, depth + 1);
  }
}

bool hasCollisionPhysics(const game::ObjectTemplate& object) {
  if (const auto value = object.number("hasCollisionPhysics")) return *value != 0;
  std::string name = object.className;
  for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return name == "dynamicbundle";
}

std::vector<CollisionPiece> collisionPhysicsPieces(const std::vector<CollisionPiece>& pieces) {
  std::vector<CollisionPiece> kept;
  for (const CollisionPiece& piece : pieces)
    if (piece.materialOwner && hasCollisionPhysics(*piece.materialOwner)) kept.push_back(piece);
  return kept;
}

std::unique_ptr<CollisionWorld> buildCollisionWorld(CollisionLibrary& library,
                                                    const std::vector<level::StaticObject>& objects,
                                                    mesh::ColType type, bool collisionPhysicsOnly) {
  auto world = std::make_unique<CollisionWorld>();
  int withCollision = 0, withoutCollision = 0;
  for (const level::StaticObject& object : objects) {
    std::vector<CollisionPiece> kept;
    if (collisionPhysicsOnly) kept = collisionPhysicsPieces(library.pieces(object.templateName, type));
    const auto& pieces = collisionPhysicsOnly ? kept : library.pieces(object.templateName, type);
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
    for (const CollisionPiece& piece : pieces) {
      world->addLayer(*piece.layer, transform * piece.local, piece.materialOwner);
    }
    ++withCollision;
  }
  std::printf("  collision: %d objects, %zu triangles in %zu cells (without geometry %d)\n",
              withCollision, world->triangleCount(), world->cellCount(), withoutCollision);
  return world;
}

}  // namespace obf2::server
