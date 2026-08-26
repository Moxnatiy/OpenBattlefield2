#pragma once
// `.collisionmesh` — окрема геометрія для зіткнень.
//
// У BF2 вона **не збігається з видимою**: спрощена, без деталей, і поділена
// на кілька шарів під різних споживачів. Саме тому куля може пролетіти крізь
// поручень, який зупиняє солдата.
//
// Розкладка (за Project Dalian, engine/formats/collision):
//
//   u32 versionMajor, u32 versionMinor        (у BF2 це 0 і 10)
//   u32 geometryPartCount
//     u32 geometryCount
//       u32 colCount
//         u32 colType                          див. ColType
//         u32 faceCount, далі по 4 u16: v1,v2,v3, матеріал
//         u32 vertexCount, далі float3 на вершину
//         u16 на вершину — матеріал вершини
//         float3 boundsMin, float3 boundsMax
//         u8 маркер BSP: '1' означає, що далі йде дерево
//         (versionMinor >= 10) u32 adjacencyCount і стільки ж i32
//
// BSP-дерево ми пропускаємо: воно потрібне для швидкого пошуку всередині
// одного меша, а ми будуємо власний просторовий індекс по всьому рівню.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "obf2/mesh/bf2_mesh.h"

namespace mesh_detail {}  // silence -Wunused

namespace obf2::mesh {

// Під кого призначений шар зіткнень. Значення взяті з даних гри.
enum class ColType : std::uint32_t {
  Projectile = 0,  // кулі й снаряди
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

  // Шар для заданого споживача; nullptr, якщо його немає.
  const CollisionLayer* layer(ColType type) const;
};

std::optional<CollisionMesh> loadCollisionMesh(std::span<const std::byte> bytes,
                                               std::string* error = nullptr);

}  // namespace obf2::mesh
