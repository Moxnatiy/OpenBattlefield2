#pragma once
// `.collisionmesh` — separate geometry for collisions.
//
// In BF2 it **does not match the visible one**: simplified, without detail, and
// split into several layers for different consumers. That is exactly why a
// bullet can fly through a railing that stops a soldier.
//
// The layout (after Project Dalian, engine/formats/collision):
//
//   u32 versionMajor, u32 versionMinor        (0 and 10 in BF2)
//   u32 geometryPartCount                      one CollisionMeshTemplate each
//     u32 geometryCount                        the template's geoms
//       u32 colCount                           the geom's lods
//         u32 colType                          see ColType
//         u32 faceCount, then 4 u16 each: v1,v2,v3, material
//         u32 vertexCount, then a float3 per vertex
//         u16 per vertex — the vertex's material
//         float3 boundsMin, float3 boundsMax
//         u8 BSP marker: '1' means a tree follows (CollisionBsp)
//         (versionMinor >= 10) u32 adjacencyCount and that many i32
//
// The tree is kept: a line test inside one mesh returns the face its walk
// accepts first, not the nearest (docs/functions/collision-bsp.md).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "obf2/mesh/bf2_mesh.h"

namespace mesh_detail {}  // silence -Wunused

namespace obf2::mesh {

// Who a collision layer is meant for. The values come from the game's data;
// the engine asks for a lod by this number (`checkSoldierVsMesh`, Linux
// 0x6f4ab7, asks for 2). Type 4 occurs in the game's files (the olive trees'
// meshes carry one triangle of it); its purpose is not established.
enum class ColType : std::uint32_t {
  Projectile = 0,  // bullets and shells
  Vehicle = 1,
  Soldier = 2,
  Ai = 3,
};

// One geom of one part, the way `CollisionMeshTemplate::load` (Linux 0x71f600)
// keeps it: its lods are a vector indexed by the type a lod was read with, and a
// five-slot table (Geom +0x18..+0x28) says which lod answers a request for each
// type.
struct CollisionGeometry {
  // The index into `CollisionMesh::layers` of the lod at each position; -1 where
  // the engine's vector holds a null. For version 0.9 and later every lod read
  // resizes the vector to its type + 1, erasing past it (0x71f600, the
  // `_M_fill_insert` / `erase` pair); before 0.9 the vector is the lod count and
  // the position is the order.
  std::vector<int> lods;
  // The table: a type read writes its own number into its slot. An `Ai` lod is
  // skipped whole unless the setting `keepAINav` is on (it is off in a plain
  // server) and its slot left at -1. After the geom: an empty soldier slot takes
  // the vehicle's, an empty AI slot the soldier's.
  int table[5] = {-1, -1, -1, -1, -1};
  // The lod count the file gave, skipped AI lods included.
  std::uint32_t colCount = 0;
};

struct CollisionPart {
  std::vector<CollisionGeometry> geometries;
};

struct CollisionFace {
  std::uint16_t a = 0, b = 0, c = 0;
  std::uint16_t material = 0;
};

// One node of a lod's tree, 16 bytes in the file (`BspV8::load`, Linux
// 0x7107b0; read by `BspV8::getCollidingFaces` 0x70fc40).
struct CollisionBspNode {
  float split = 0.0f;  // +0x0: the plane's position along the axis
  // +0x4: bits 0-1 the axis; bit 2 child 0 is a leaf, bit 3 child 1; bits 16-23
  // child 0's face count, bits 24-31 child 1's. Bits 4-15 are not read.
  std::uint32_t flags = 0;
  // +0x8, +0xc: below / at or above the split. A node index, or for a leaf the
  // first entry in `CollisionBsp::faceRefs`.
  std::uint32_t child[2] = {0, 0};

  int axis() const { return static_cast<int>(flags & 3u); }
  bool leaf(int side) const { return (flags & (4u << side)) != 0; }
  std::uint32_t count(int side) const { return (flags >> (16 + 8 * side)) & 0xffu; }
};

// A lod's tree as `BspV8` keeps it (docs/functions/collision-bsp.md).
struct CollisionBsp {
  Aabb bounds;                          // +0x18, +0x24
  std::vector<CollisionBspNode> nodes;  // +0x30
  std::vector<std::uint16_t> faceRefs;  // +0x48
  // The face vector 0x70f2e0 builds (+0x4 of each 0x18-byte face): (v3 − v2) ×
  // (v2 − v1), unit, or zero for a degenerate face. Index = the lod's face.
  std::vector<Vec3> normals;
};

struct CollisionLayer {
  ColType type = ColType::Projectile;
  std::vector<Vec3> vertices;
  std::vector<CollisionFace> faces;
  Aabb bounds;
  std::optional<CollisionBsp> bsp;  // marker '1'
};

// What `checkFaceAndEdgeCollision` (Linux 0x724ae0) gives a leaf back.
struct BspFaceHit {
  Vec3 point;
  float depth = 0.0f;     // its +0x88 output
  float distance = 0.0f;  // its +0x8c output, the one the leaf keeps the smallest of
};

// The face test the leaf calls: the face, the normal (negated with flip), the
// segment's start and end. True with `hit` filled when the segment crosses it.
using BspFaceTest = std::function<bool(std::uint16_t face, const Vec3& normal, const Vec3& start,
                                       const Vec3& end, BspFaceHit& hit)>;

struct BspClosestFace {
  std::uint16_t face = 0;
  BspFaceHit hit;
};

// `BspV8::getClosestFace` (Linux 0x70ff70) in the lod's own space: the segment
// from `start` along `motion`. With `stopFirst` the first face a leaf accepts,
// in the walk's order; without it the smallest distance of every leaf reached.
// nullopt when the lod has no tree or nothing is accepted.
std::optional<BspClosestFace> closestFace(const CollisionLayer& layer, const Vec3& start,
                                          const Vec3& motion, bool twoSided, bool flip,
                                          bool stopFirst, const BspFaceTest& test);

struct CollisionMesh {
  std::uint32_t versionMajor = 0;
  std::uint32_t versionMinor = 0;
  // Every lod in the file, in file order, the skipped `Ai` ones included.
  std::vector<CollisionLayer> layers;
  std::vector<CollisionPart> parts;

  // The first layer read with exactly this type; nullptr when there is none.
  const CollisionLayer* layer(ColType type) const;

  // What the engine answers a request for a type with:
  // `CollisionMeshTemplate::getValidLod(geom, type)` (Linux 0x719810) takes the
  // table's slot, clamps it to the last lod and to zero, and returns that lod,
  // which may be a null. `hasLod` (0x717e70 → `isLodValid` 0x7194e0) is the same
  // without the clamp: the slot inside the vector and not a null. The request
  // here is `hasLod`'s, the one `checkSoldierVsMesh` stops on. A collision mesh
  // object starts on geom 0 (`CollisionMesh::CollisionMesh`, 0x717dc0, +0x24).
  const CollisionLayer* validLayer(std::size_t part, std::size_t geometry, ColType type) const;

  // `CollisionMeshTemplate::getMaxBoundingRadius` (Linux 0x719a70) of one part,
  // what `CollisionMesh::getMaxBoundingRadius` (0x718a90) answers for an object
  // (`Object+0x80`, stored by `Object::setComponent` at 0x688fc3). The first
  // geom whose lod vector is not empty (`isGeomValid`, 0x718f00); in it the
  // first type t = 0 .. lod count - 1 that `isLodValid(geom, t)` (0x7194e0)
  // accepts; that lod's radius (+0xec). The geom's later types and later geoms
  // are not looked at. The radius is copied from the lod's box (+0xc8, 0x71f600)
  // after `LocalBoundingBox::calcRadius` (0x72cb50): the length of the box's
  // farthest corner from the mesh's origin. 0 when nothing qualifies, or when
  // the part does not exist.
  float maxBoundingRadius(std::size_t part) const;
};

std::optional<CollisionMesh> loadCollisionMesh(std::span<const std::byte> bytes,
                                               std::string* error = nullptr);

}  // namespace obf2::mesh
