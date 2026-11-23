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
  const std::vector<CollisionPiece>& soldierPieces(const std::string& templateName) {
    return pieces(templateName, mesh::ColType::Soldier);
  }
  // The same walk for another lod (`ColType`).
  const std::vector<CollisionPiece>& pieces(const std::string& templateName, mesh::ColType type);

  // A template's collision mesh by its `collisionMesh` name, searched beside the
  // file the template was created in; nullptr when there is none.
  const mesh::CollisionMesh* meshOf(const game::ObjectTemplate& owner);

 private:
  void walkChildren(const game::ObjectTemplate& parent, const Mat4& parentTransform,
                    const mesh::CollisionMesh& mesh, mesh::ColType type,
                    std::vector<CollisionPiece>& out, int depth);

  FileSystem& files_;
  const game::Registry& registry_;
  std::unordered_map<std::string, std::unique_ptr<mesh::CollisionMesh>> meshes_;
  std::unordered_map<std::string, std::vector<CollisionPiece>> pieces_[4];  // by ColType
};

// Object flag 0x100 (`IObject` +8 on Linux, msvc +4): `SimpleObject::SimpleObject`
// (Linux 0x58a50c → 0x58a6e5) sets it with `updateFlags(0x100, 0)` when the
// template's +0xe8 bit 2 is on, which `SimpleObjectTemplate::makeScript`
// (0x58d04b, string 0xb5ccf0) writes as `ObjectTemplate.hasCollisionPhysics`.
// `SimpleObjectTemplate::SimpleObjectTemplate` clears it (0x58ba4d, `andb
// $0x80`), `DynamicBundleTemplate::DynamicBundleTemplate` sets it with
// hasMobilePhysics (0x57ccb5, `orb $0x6`); no other template constructor
// touches it. Later changes at run time (`DestroyableWindow::setIsBroken`
// 0x515aa9, `Ladder::attachSoldier` 0x52a084, `Kit::init` 0x526e0c) are not
// ported.
bool hasCollisionPhysics(const game::ObjectTemplate& object);

// The level's placed objects in one grid: every piece of every object's tree,
// the soldier's lod unless another is asked for. `collisionPhysicsOnly` keeps
// only the pieces whose object has flag 0x100 (`hasCollisionPhysics`), what a
// predicator asking for that flag sees (the sound's, BF2.exe 0x7df780).
std::unique_ptr<CollisionWorld> buildCollisionWorld(CollisionLibrary& library,
                                                    const std::vector<level::StaticObject>& objects,
                                                    mesh::ColType type = mesh::ColType::Soldier,
                                                    bool collisionPhysicsOnly = false);

// The pieces `collisionPhysicsOnly` keeps.
std::vector<CollisionPiece> collisionPhysicsPieces(const std::vector<CollisionPiece>& pieces);

}  // namespace obf2::server
