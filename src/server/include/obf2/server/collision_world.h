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
  // Which layer (placed object) the triangle came from: the engine asks each
  // object's mesh on its own, and whether the edges are tried depends on that
  // mesh's faces alone.
  std::uint32_t object = 0;
  // The collision mesh's own material index of the face; the template's mapping
  // to a global material (`CollisionMeshTemplate` `+0x28`) is not read.
  std::uint16_t material = 0;
};

// One contact of a sphere with a mesh — what
// `CollisionMeshTemplate::getDistanceToSphere` (Linux server 0x71a500) pushes
// into its result vectors. docs/functions/soldier-physics.md, "Contacts with objects".
struct MeshContact {
  Vec3f point;
  Vec3f normal;
  float depth = 0.0f;   // not above zero
  float travel = 0.0f;  // along the motion, past the surface
  std::uint16_t material = 0;
};

// One collision layer of an object's tree, in the root's space.
struct CollisionPiece {
  const mesh::CollisionLayer* layer = nullptr;
  Mat4 local = Mat4::identity();
};

class CollisionWorld {
 public:
  // The grid cell's size. 8 metres is a compromise: a finer grid means more
  // memory for the index, a coarser one more wasted tests.
  explicit CollisionWorld(float cellSize = 8.0f) : cellSize_(cellSize) {}

  // Adds a collision layer transformed into world coordinates.
  void addLayer(const mesh::CollisionLayer& layer, const Mat4& transform);

  // An object whose place the server gives and takes back — a vehicle an
  // ObjectSpawner made. It stays out of the grid and is searched by its bounds.
  // Every piece is an object of its own, the way each part of a bundle gets its
  // own CollisionMesh (`setChildPartCollisionMeshes`, Linux 0x574760): a face met
  // on one part does not turn off the edges of another. The layers are not
  // copied and have to outlive the world.
  std::uint32_t addMovable(std::vector<CollisionPiece> pieces, const Mat4& transform);
  void placeMovable(std::uint32_t handle, const Mat4& transform);
  void removeMovable(std::uint32_t handle);
  std::size_t movableCount() const;

  // A sphere of `radius` moving from `from` by `motion`, against every object's
  // mesh near it, the engine's way (`getDistanceToSphere`): a resting sphere
  // (motion no longer than `g_coll_use_fast_sphere_at_length` 0.01) against the
  // faces, their edges and corners; a moving one against the faces moved out by
  // the radius, and, for an object none of whose faces it met, against the
  // edges. Contacts are appended.
  void sphereContacts(const Vec3f& from, const Vec3f& motion, float radius,
                      std::vector<MeshContact>& out) const;

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
  std::uint32_t layers_ = 0;
  std::vector<CollisionTriangle> triangles_;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;

  struct Movable {
    std::vector<CollisionPiece> pieces;
    std::vector<std::uint32_t> objects;  // one object number per piece
    std::vector<CollisionTriangle> triangles;
    Vec3f minimum, maximum;
    bool alive = false;
  };
  void placeTriangles(Movable& movable, const Mat4& transform);
  std::vector<Movable> movables_;
};

}  // namespace obf2::server
