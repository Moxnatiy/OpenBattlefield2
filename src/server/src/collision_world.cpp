#include "obf2/server/collision_world.h"

#include <algorithm>
#include <cmath>

namespace obf2::server {
namespace {

// The triangle's closest point to a given one — the classic Voronoi-region
// case analysis. Needed to know how far and where to push a sphere out.
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
  // Shift into positive numbers and pack 21 bits per axis: that is enough for a
  // map of millions of cells.
  const std::uint64_t ux = static_cast<std::uint64_t>(x + 0x100000) & 0x1FFFFF;
  const std::uint64_t uy = static_cast<std::uint64_t>(y + 0x100000) & 0x1FFFFF;
  const std::uint64_t uz = static_cast<std::uint64_t>(z + 0x100000) & 0x1FFFFF;
  return (ux << 42) | (uy << 21) | uz;
}

void CollisionWorld::addLayer(const mesh::CollisionLayer& layer, const Mat4& transform) {
  for (const mesh::CollisionFace& face : layer.faces) {
    if (face.a >= layer.vertices.size() || face.b >= layer.vertices.size() ||
        face.c >= layer.vertices.size()) {
      continue;  // a corrupt face — skipped rather than fatal
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
    // The normal follows the engine's left-handed convention, that is the
    // opposite of the usual right-handed cross product. That is visible in
    // Dalian Plant's own data: with this sign 12081 triangles face up against
    // 5999 down, which is what one expects of a world of roads, roofs and stairs.
    const Vec3f normal = cross(edge2, edge1);
    const float area = length(normal);
    if (area < 1e-6f) continue;  // a degenerate triangle
    triangle.normal = normal * (1.0f / area);

    const auto index = static_cast<std::uint32_t>(triangles_.size());
    triangles_.push_back(triangle);

    // A triangle goes into every cell its bounding box crosses.
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

  // One triangle may lie in several cells, so we make sure not to process it twice.
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

  // Several passes: being pushed out of one wall can drive you into another,
  // and in a corner at least two are needed.
  constexpr int kIterations = 4;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    bool moved = false;

    forEachNearby(position, radius, [&](const CollisionTriangle& triangle) {
      const Vec3f closest = closestPointOnTriangle(position, triangle);
      const Vec3f away = position - closest;
      const float distance = length(away);
      if (distance >= radius || distance < 1e-6f) return;

      // Push out exactly far enough to touch the surface.
      position = position + away * ((radius - distance) / distance);
      moved = true;
      ++pushes;
    });

    if (!moved) break;
  }
  return pushes;
}

void CollisionWorld::normalStats(std::size_t* up, std::size_t* down) const {
  std::size_t upCount = 0, downCount = 0;
  for (const CollisionTriangle& triangle : triangles_) {
    if (triangle.normal.y > 0.5f) ++upCount;
    else if (triangle.normal.y < -0.5f) ++downCount;
  }
  if (up != nullptr) *up = upCount;
  if (down != nullptr) *down = downCount;
}

bool CollisionWorld::groundHeight(const Vec3f& from, float maxDrop, float minNormalY,
                                  float* outHeight) const {
  // A strictly downward ray. The cells are walked with the same index as for a
  // sphere: we take everything nearby horizontally over the whole search depth.
  const float bottom = from.y - maxDrop;
  bool found = false;
  float best = bottom;

  const Vec3f middle{from.x, (from.y + bottom) * 0.5f, from.z};
  forEachNearby(middle, maxDrop * 0.5f + 0.5f, [&](const CollisionTriangle& triangle) {
    if (triangle.normal.y < minNormalY) return;  // a wall, not a floor

    // The intersection of the vertical ray with the triangle's plane.
    if (std::abs(triangle.normal.y) < 1e-6f) return;
    const float distance = dot(triangle.normal, triangle.a - from);
    const float t = distance / (-triangle.normal.y);
    if (t < 0.0f || t > maxDrop) return;

    const Vec3f hit{from.x, from.y - t, from.z};

    // Whether it is inside the triangle: computed in the XZ plane through the
    // signs of the cross products. For a sloped surface that is enough, because
    // the normal is not horizontal.
    const auto side = [](const Vec3f& p, const Vec3f& a, const Vec3f& b) {
      return (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
    };
    const float s1 = side(hit, triangle.a, triangle.b);
    const float s2 = side(hit, triangle.b, triangle.c);
    const float s3 = side(hit, triangle.c, triangle.a);
    const bool negative = s1 < 0.0f || s2 < 0.0f || s3 < 0.0f;
    const bool positive = s1 > 0.0f || s2 > 0.0f || s3 > 0.0f;
    if (negative && positive) return;

    if (!found || hit.y > best) {
      best = hit.y;
      found = true;
    }
  });

  if (found && outHeight != nullptr) *outHeight = best;
  return found;
}

}  // namespace obf2::server
