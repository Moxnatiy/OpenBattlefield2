#pragma once
// What of an object template's tree a soldier collides with.
//
// A tree's collision is one `.collisionmesh` split into parts. The root takes
// part 0 of its own mesh (`ObjectSpawner::getCollisionMesh`, Linux 0x52f8e7,
// asks `CollisionManager::getCollisionMeshPart(name, 0)` for what it spawns).
// Its descendants take the root's mesh too, each the part its template names
// with `ObjectTemplate.collisionPart`: `Bundle::init` (0x574860) hands the
// bundle's mesh name to `setChildPartCollisionMeshes` (0x574760), which walks the
// children and gives every one without a mesh of its own and with a part above
// zero `getCollisionMeshPart(name, part)`. The walk goes down each child and on
// to its next sibling, and stops at the first child that already has a mesh —
// its later siblings are left without one.
//
// A soldier meets the lod `CollisionMesh::hasLod(2)` answers
// (`CollisionMesh::validLayer`, collision.h) of each piece's geom 0.
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "obf2/game/object_template.h"
#include "obf2/level/level.h"
#include "obf2/mesh/collision.h"
#include "obf2/server/collision_world.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::server {

class CollisionLibrary {
 public:
  CollisionLibrary(FileSystem& files, const game::Registry& registry)
      : files_(files), registry_(registry) {}

  // The soldier's layers of a template's tree, each placed in the root's space.
  // Empty when the tree has none. The reference stays valid for the library's life.
  const std::vector<CollisionPiece>& soldierPieces(const std::string& templateName);

  // A template's collision mesh by its `collisionMesh` name, searched beside the
  // file the template was created in; nullptr when there is none.
  const mesh::CollisionMesh* meshOf(const game::ObjectTemplate& owner);

 private:
  void walkChildren(const game::ObjectTemplate& parent, const Mat4& parentTransform,
                    const mesh::CollisionMesh& mesh, std::vector<CollisionPiece>& out, int depth);

  FileSystem& files_;
  const game::Registry& registry_;
  std::unordered_map<std::string, std::unique_ptr<mesh::CollisionMesh>> meshes_;
  std::unordered_map<std::string, std::vector<CollisionPiece>> pieces_;
};

// The level's placed objects in one grid: every piece of every object's tree.
std::unique_ptr<CollisionWorld> buildCollisionWorld(CollisionLibrary& library,
                                                    const std::vector<level::StaticObject>& objects);

}  // namespace obf2::server
