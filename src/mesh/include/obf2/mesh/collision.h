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
//         u8 BSP marker: '1' means a tree follows
//         (versionMinor >= 10) u32 adjacencyCount and that many i32
//
// We skip the BSP tree: it is meant for fast lookup inside one mesh, while we
// build our own spatial index over the whole level.
#include <cstddef>
#include <cstdint>
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

struct CollisionLayer {
  ColType type = ColType::Projectile;
  std::vector<Vec3> vertices;
  std::vector<CollisionFace> faces;
  Aabb bounds;
};

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
};

std::optional<CollisionMesh> loadCollisionMesh(std::span<const std::byte> bytes,
                                               std::string* error = nullptr);

}  // namespace obf2::mesh
