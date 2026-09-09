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
//   u32 geometryPartCount
//     u32 geometryCount
//       u32 colCount
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

// Who a collision layer is meant for. The values come from the game's data.
enum class ColType : std::uint32_t {
  Projectile = 0,  // bullets and shells
  Vehicle = 1,
  Soldier = 2,
  Ai = 3,
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
  std::vector<CollisionLayer> layers;

  // The layer for the given consumer; nullptr when there is none.
  const CollisionLayer* layer(ColType type) const;
};

std::optional<CollisionMesh> loadCollisionMesh(std::span<const std::byte> bytes,
                                               std::string* error = nullptr);

}  // namespace obf2::mesh
