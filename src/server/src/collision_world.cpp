#include "obf2/server/collision_world.h"

#include <algorithm>
#include <cmath>

namespace obf2::server {
namespace {

// Найближча точка трикутника до заданої — класичний розбір за областями
// Вороного. Потрібен, щоб знати, наскільки й куди виштовхувати сферу.
Vec3f closestPointOnTriangle(const Vec3f& point, const CollisionTriangle& triangle) {
  const Vec3f ab = triangle.b - triangle.a;
  const Vec3f ac = triangle.c - triangle.a;
  const Vec3f ap = point - triangle.a;

  const float d1 = dot(ab, ap);
  const float d2 = dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) return triangle.a;

  const Vec3f bp = point - triangle.b;
  const float d3 = dot(ab, bp);
  const float d4 = dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) return triangle.b;

  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
    const float v = d1 / (d1 - d3);
    return triangle.a + ab * v;
  }

  const Vec3f cp = point - triangle.c;
  const float d5 = dot(ab, cp);
  const float d6 = dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) return triangle.c;

  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
    const float w = d2 / (d2 - d6);
    return triangle.a + ac * w;
  }

  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return triangle.b + (triangle.c - triangle.b) * w;
  }

  const float denom = 1.0f / (va + vb + vc);
  const float v = vb * denom;
  const float w = vc * denom;
  return triangle.a + ab * v + ac * w;
}

}  // namespace

std::uint64_t CollisionWorld::cellKey(int x, int y, int z) {
  // Зсуваємо в додатні числа й пакуємо по 21 біту на вісь: цього вистачає
  // на карту в мільйони комірок.
  const std::uint64_t ux = static_cast<std::uint64_t>(x + 0x100000) & 0x1FFFFF;
  const std::uint64_t uy = static_cast<std::uint64_t>(y + 0x100000) & 0x1FFFFF;
  const std::uint64_t uz = static_cast<std::uint64_t>(z + 0x100000) & 0x1FFFFF;
  return (ux << 42) | (uy << 21) | uz;
}

void CollisionWorld::addLayer(const mesh::CollisionLayer& layer, const Mat4& transform) {
  for (const mesh::CollisionFace& face : layer.faces) {
    if (face.a >= layer.vertices.size() || face.b >= layer.vertices.size() ||
        face.c >= layer.vertices.size()) {
      continue;  // зіпсована грань — пропускаємо, а не падаємо
    }

    CollisionTriangle triangle;
    auto toWorld = [&](const mesh::Vec3& v) {
      return transformPoint(transform, Vec3f{v.x, v.y, v.z});
    };
    triangle.a = toWorld(layer.vertices[face.a]);
    triangle.b = toWorld(layer.vertices[face.b]);
    triangle.c = toWorld(layer.vertices[face.c]);

    const Vec3f edge1 = triangle.b - triangle.a;
    const Vec3f edge2 = triangle.c - triangle.a;
    const Vec3f normal = cross(edge1, edge2);
    const float area = length(normal);
    if (area < 1e-6f) continue;  // вироджений трикутник
    triangle.normal = normal * (1.0f / area);

    const auto index = static_cast<std::uint32_t>(triangles_.size());
    triangles_.push_back(triangle);

    // Трикутник потрапляє в усі комірки, які перетинає його габарит.
    const Vec3f minimum{std::min({triangle.a.x, triangle.b.x, triangle.c.x}),
                        std::min({triangle.a.y, triangle.b.y, triangle.c.y}),
                        std::min({triangle.a.z, triangle.b.z, triangle.c.z})};
    const Vec3f maximum{std::max({triangle.a.x, triangle.b.x, triangle.c.x}),
                        std::max({triangle.a.y, triangle.b.y, triangle.c.y}),
                        std::max({triangle.a.z, triangle.b.z, triangle.c.z})};

    for (int x = static_cast<int>(std::floor(minimum.x / cellSize_));
         x <= static_cast<int>(std::floor(maximum.x / cellSize_)); ++x) {
      for (int y = static_cast<int>(std::floor(minimum.y / cellSize_));
           y <= static_cast<int>(std::floor(maximum.y / cellSize_)); ++y) {
        for (int z = static_cast<int>(std::floor(minimum.z / cellSize_));
             z <= static_cast<int>(std::floor(maximum.z / cellSize_)); ++z) {
          cells_[cellKey(x, y, z)].push_back(index);
        }
      }
    }
  }
}

void CollisionWorld::forEachNearby(
    const Vec3f& position, float radius,
    const std::function<void(const CollisionTriangle&)>& visit) const {
  const int x0 = static_cast<int>(std::floor((position.x - radius) / cellSize_));
  const int x1 = static_cast<int>(std::floor((position.x + radius) / cellSize_));
  const int y0 = static_cast<int>(std::floor((position.y - radius) / cellSize_));
  const int y1 = static_cast<int>(std::floor((position.y + radius) / cellSize_));
  const int z0 = static_cast<int>(std::floor((position.z - radius) / cellSize_));
  const int z1 = static_cast<int>(std::floor((position.z + radius) / cellSize_));

  // Один трикутник може лежати в кількох комірках, тому стежимо, щоб не
  // обробити його двічі.
  std::vector<std::uint32_t> seen;
  for (int x = x0; x <= x1; ++x) {
    for (int y = y0; y <= y1; ++y) {
      for (int z = z0; z <= z1; ++z) {
        const auto cell = cells_.find(cellKey(x, y, z));
        if (cell == cells_.end()) continue;
        for (const std::uint32_t index : cell->second) {
          if (std::find(seen.begin(), seen.end(), index) != seen.end()) continue;
          seen.push_back(index);
          visit(triangles_[index]);
        }
      }
    }
  }
}

int CollisionWorld::resolveSphere(Vec3f& position, float radius) const {
  int pushes = 0;

  // Кілька проходів: виштовхування з однієї стіни може загнати в іншу,
  // а в кутку потрібні щонайменше два.
  constexpr int kIterations = 4;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    bool moved = false;

    forEachNearby(position, radius, [&](const CollisionTriangle& triangle) {
      const Vec3f closest = closestPointOnTriangle(position, triangle);
      const Vec3f away = position - closest;
      const float distance = length(away);
      if (distance >= radius || distance < 1e-6f) return;

      // Виштовхуємо рівно настільки, щоб торкатися поверхні.
      position = position + away * ((radius - distance) / distance);
      moved = true;
      ++pushes;
    });

    if (!moved) break;
  }
  return pushes;
}

}  // namespace obf2::server
