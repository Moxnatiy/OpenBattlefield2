#pragma once
// The server's collision world.
//
// The triangles of every static object are converted into world coordinates once
// at load time and spread over a uniform grid. Testing 2.5 M triangles every
// frame is impossible, while a cell a few metres across leaves only a handful
// to test.
//
// Collision is a server matter: in BF2 the server owns the world, and it is the
// server that decides where the player actually got to.
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/mesh/collision.h"

namespace obf2::server {

struct CollisionTriangle {
  Vec3f a, b, c;
  Vec3f normal;
};

class CollisionWorld {
 public:
  // The grid cell's size. 8 metres is a compromise: a finer grid means more
  // memory for the index, a coarser one more wasted tests.
  explicit CollisionWorld(float cellSize = 8.0f) : cellSize_(cellSize) {}

  // Adds a collision layer transformed into world coordinates.
  void addLayer(const mesh::CollisionLayer& layer, const Mat4& transform);

  // Pushes a sphere out of the geometry. Returns how many times it had to push —
  // zero means the way is clear.
  int resolveSphere(Vec3f& position, float radius) const;

  // The highest surface under a point that a soldier can stand on. Needed to
  // stand **on objects** rather than always on the terrain: the engine computes
  // the ground from more than the height map.
  //
  // minNormalY is how gentle the surface has to be (`phy-soldier-
  // feet-contact-normal` = 0.5, that is a slope of up to 60°). Returns false when
  // there is nothing under the feet within maxDrop.
  bool groundHeight(const Vec3f& from, float maxDrop, float minNormalY, float* outHeight) const;

  std::size_t triangleCount() const { return triangles_.size(); }
  // How many triangles face up with their normal and how many down — to check
  // the data's orientation rather than guess it.
  void normalStats(std::size_t* up, std::size_t* down) const;
  std::size_t cellCount() const { return cells_.size(); }

 private:
  // A cell's key: three integer coordinates folded into one number.
  static std::uint64_t cellKey(int x, int y, int z);
  void forEachNearby(const Vec3f& position, float radius,
                     const std::function<void(const CollisionTriangle&)>& visit) const;

  float cellSize_;
  std::vector<CollisionTriangle> triangles_;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;
};

}  // namespace obf2::server
