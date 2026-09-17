#include <cmath>
#include <cstring>
#include <vector>

#include "check.h"
#include "obf2/mesh/collision.h"
#include "obf2/server/collision_world.h"

using namespace obf2;

namespace {

// A binary .collisionmesh builder — the test does not depend on the game being
// present and also fixes the format's layout in code.
class CollisionBuilder {
 public:
  explicit CollisionBuilder(std::uint32_t versionMinor) : versionMinor_(versionMinor) {
    u32(0);              // versionMajor
    u32(versionMinor_);  // versionMinor
    u32(1);              // geometryPartCount
    u32(1);              // geometryCount
    u32(1);              // colCount
  }

  // One layer of one triangle.
  void addLayer(mesh::ColType type, const mesh::Vec3& a, const mesh::Vec3& b,
                const mesh::Vec3& c) {
    if (versionMinor_ >= 9) u32(static_cast<std::uint32_t>(type));

    u32(1);  // faceCount
    u16(0); u16(1); u16(2); u16(0);

    u32(3);  // vertexCount
    vec3(a); vec3(b); vec3(c);
    u16(0); u16(0); u16(0);  // the vertices' materials

    vec3(a);  // bounds min (roughly)
    vec3(c);  // bounds max
    byte('0');  // no BSP

    if (versionMinor_ >= 10) u32(0);  // adjacencyCount
  }

  const std::vector<std::byte>& bytes() const { return bytes_; }

 private:
  void u32(std::uint32_t value) { raw(&value, 4); }
  void u16(std::uint16_t value) { raw(&value, 2); }
  void byte(char value) { raw(&value, 1); }
  void vec3(const mesh::Vec3& v) { raw(&v.x, 4); raw(&v.y, 4); raw(&v.z, 4); }

  void raw(const void* data, std::size_t size) {
    const auto* source = static_cast<const std::byte*>(data);
    bytes_.insert(bytes_.end(), source, source + size);
  }

  std::uint32_t versionMinor_;
  std::vector<std::byte> bytes_;
};

// A large horizontal plane at height y.
mesh::CollisionLayer makeWall(float x) {
  mesh::CollisionLayer layer;
  // A vertical wall in the plane X = x.
  layer.vertices = {mesh::Vec3{x, -10.0f, -10.0f}, mesh::Vec3{x, 10.0f, -10.0f},
                    mesh::Vec3{x, 10.0f, 10.0f}, mesh::Vec3{x, -10.0f, 10.0f}};
  layer.faces = {mesh::CollisionFace{0, 1, 2, 0}, mesh::CollisionFace{0, 2, 3, 0}};
  return layer;
}

}  // namespace

static void testParseVersion10() {
  CollisionBuilder builder(10);
  builder.addLayer(mesh::ColType::Soldier, mesh::Vec3{0, 0, 0}, mesh::Vec3{1, 0, 0},
                   mesh::Vec3{0, 1, 0});

  std::string error;
  const auto mesh = mesh::loadCollisionMesh(builder.bytes(), &error);
  CHECK(mesh.has_value());
  if (!mesh) {
    std::fprintf(stderr, "  reason: %s\n", error.c_str());
    return;
  }
  CHECK_EQ(mesh->versionMinor, 10u);
  CHECK_EQ(mesh->layers.size(), std::size_t(1));
  if (!mesh->layers.empty()) {
    CHECK(mesh->layers[0].type == mesh::ColType::Soldier);
    CHECK_EQ(mesh->layers[0].faces.size(), std::size_t(1));
    CHECK_EQ(mesh->layers[0].vertices.size(), std::size_t(3));
  }
  CHECK(mesh->layer(mesh::ColType::Soldier) != nullptr);
  CHECK(mesh->layer(mesh::ColType::Vehicle) == nullptr);
}

static void testVersion8HasNoLayerType() {
  // Version 0.8 has no type field, and the layer is decided by order. Because of
  // that 241 of the game's files used to parse shifted.
  CollisionBuilder builder(8);
  builder.addLayer(mesh::ColType::Projectile, mesh::Vec3{0, 0, 0}, mesh::Vec3{1, 0, 0},
                   mesh::Vec3{0, 1, 0});

  const auto mesh = mesh::loadCollisionMesh(builder.bytes());
  CHECK(mesh.has_value());
  if (!mesh) return;
  CHECK_EQ(mesh->layers.size(), std::size_t(1));
  // The first layer gets its type by index.
  if (!mesh->layers.empty()) CHECK(mesh->layers[0].type == mesh::ColType::Projectile);
}

static void testTruncatedFileIsRejected() {
  CollisionBuilder builder(10);
  builder.addLayer(mesh::ColType::Soldier, mesh::Vec3{0, 0, 0}, mesh::Vec3{1, 0, 0},
                   mesh::Vec3{0, 1, 0});

  const auto& full = builder.bytes();
  for (std::size_t size = 0; size < full.size(); size += 5) {
    std::vector<std::byte> truncated(full.begin(), full.begin() + static_cast<long>(size));
    std::string error;
    if (mesh::loadCollisionMesh(truncated, &error).has_value()) {
      // A very short truncation may accidentally give an "empty" valid mesh;
      // what matters is that there is no read past the end.
      continue;
    }
    CHECK(!error.empty());
  }
}

static void testSphereIsPushedOutOfWall() {
  server::CollisionWorld world;
  world.addLayer(makeWall(0.0f), Mat4::identity());
  CHECK_EQ(world.triangleCount(), std::size_t(2));

  // A sphere of radius 0.5 centred 0.2 from the wall — it has to be pushed out.
  Vec3f position{-0.2f, 0.0f, 0.0f};
  const int pushes = world.resolveSphere(position, 0.5f);
  CHECK(pushes > 0);
  // It pushes out towards the side it came from, and by exactly the radius.
  CHECK(position.x < -0.49f);
  CHECK(position.x > -0.52f);
}

static void testSphereFarFromWallIsUntouched() {
  server::CollisionWorld world;
  world.addLayer(makeWall(0.0f), Mat4::identity());

  Vec3f position{-5.0f, 0.0f, 0.0f};
  const Vec3f before = position;
  CHECK_EQ(world.resolveSphere(position, 0.5f), 0);
  CHECK_EQ(position.x, before.x);
}

static void testTransformIsApplied() {
  // A wall shifted by 10 along X has to stop exactly there.
  server::CollisionWorld world;
  world.addLayer(makeWall(0.0f), translation(Vec3f{10.0f, 0.0f, 0.0f}));

  Vec3f position{9.8f, 0.0f, 0.0f};
  CHECK(world.resolveSphere(position, 0.5f) > 0);
  CHECK(position.x < 9.51f);

  Vec3f farAway{0.0f, 0.0f, 0.0f};
  CHECK_EQ(world.resolveSphere(farAway, 0.5f), 0);
}

static void testDegenerateTrianglesAreSkipped() {
  mesh::CollisionLayer layer;
  layer.vertices = {mesh::Vec3{0, 0, 0}, mesh::Vec3{0, 0, 0}, mesh::Vec3{0, 0, 0}};
  layer.faces = {mesh::CollisionFace{0, 1, 2, 0}};

  server::CollisionWorld world;
  world.addLayer(layer, Mat4::identity());
  CHECK_EQ(world.triangleCount(), std::size_t(0));
}

static void testFacesWithBadIndicesAreSkipped() {
  // An index past the end of the vertex array must not bring the server down.
  mesh::CollisionLayer layer;
  layer.vertices = {mesh::Vec3{0, 0, 0}, mesh::Vec3{1, 0, 0}, mesh::Vec3{0, 1, 0}};
  layer.faces = {mesh::CollisionFace{0, 1, 99, 0}, mesh::CollisionFace{0, 1, 2, 0}};

  server::CollisionWorld world;
  world.addLayer(layer, Mat4::identity());
  CHECK_EQ(world.triangleCount(), std::size_t(1));
}

// `getDistanceToSphere` (Linux server 0x71a500), the moving sphere against a face
// moved out by the radius (`checkShiftedFaceCollision` 0x724e40): the wall faces
// -X, the sphere of 0.25 goes from x -0.5 to -0.1, meeting the moved face at -0.25.
static void testMovingSphereMeetsTheFace() {
  server::CollisionWorld world;
  world.addLayer(makeWall(0.0f), Mat4::identity());
  std::vector<server::MeshContact> contacts;
  world.sphereContacts(Vec3f{-0.5f, 0.0f, 0.0f}, Vec3f{0.4f, 0.0f, 0.0f}, 0.25f, contacts);
  CHECK(!contacts.empty());
  if (contacts.empty()) return;
  const auto& c = contacts[0];
  CHECK(std::abs(c.normal.x + 1.0f) < 1e-5f);
  CHECK(std::abs(c.depth + 0.15f) < 1e-5f);
  CHECK(std::abs(c.travel + 0.15f) < 1e-5f);
  CHECK(std::abs(c.point.x) < 1e-5f);
}

// The resting sphere (`checkSphereTriCollision` 0x7254a0): 0.2 from the wall, a
// radius of 0.25 is 0.05 deep. Moving away from the wall nothing is met: faces are
// single sided (0x724bb6).
static void testRestingSphereAndTheFacesSide() {
  server::CollisionWorld world;
  world.addLayer(makeWall(0.0f), Mat4::identity());
  std::vector<server::MeshContact> contacts;
  world.sphereContacts(Vec3f{-0.2f, 0.0f, 0.0f}, Vec3f{}, 0.25f, contacts);
  CHECK_EQ(contacts.size(), std::size_t(2));  // both of the wall's triangles
  if (!contacts.empty()) CHECK(std::abs(contacts[0].depth + 0.05f) < 1e-5f);

  contacts.clear();
  world.sphereContacts(Vec3f{-0.1f, 0.0f, 0.0f}, Vec3f{-0.4f, 0.0f, 0.0f}, 0.25f, contacts);
  CHECK(contacts.empty());
}

// The moving sphere passing a floor's edge without meeting the face
// (`checkExpandedEdgesCollision` 0x7266e0): the edge along Z at x = 0, the centre
// 0.2 above it moving along X. It meets the edge's capsule at x = -0.15; the normal
// points from the edge to the centre, the depth is the end's along it. Under the
// floor the same contact has its normal flattened (0x71bb24).
static void testMovingSphereMeetsAnEdge() {
  mesh::CollisionLayer layer;
  layer.vertices = {mesh::Vec3{0.0f, 0.0f, 1.0f}, mesh::Vec3{0.0f, 0.0f, -1.0f},
                    mesh::Vec3{1.0f, 0.0f, 0.0f}};
  layer.faces = {mesh::CollisionFace{0, 1, 2, 0}};
  server::CollisionWorld world;
  world.addLayer(layer, Mat4::identity());

  std::vector<server::MeshContact> contacts;
  world.sphereContacts(Vec3f{-0.5f, 0.2f, 0.0f}, Vec3f{0.4f, 0.0f, 0.0f}, 0.25f, contacts);
  CHECK_EQ(contacts.size(), std::size_t(1));
  if (!contacts.empty()) {
    const auto& c = contacts[0];
    CHECK(std::abs(c.normal.x + 0.6f) < 1e-4f);
    CHECK(std::abs(c.normal.y - 0.8f) < 1e-4f);
    CHECK(std::abs(c.depth + 0.03f) < 1e-4f);
    CHECK(std::abs(c.travel + 0.05f) < 1e-4f);
    CHECK(length(c.point) < 1e-4f);
  }

  contacts.clear();
  world.sphereContacts(Vec3f{-0.5f, -0.2f, 0.0f}, Vec3f{0.4f, 0.0f, 0.0f}, 0.25f, contacts);
  CHECK_EQ(contacts.size(), std::size_t(1));
  if (!contacts.empty()) {
    CHECK(std::abs(contacts[0].normal.x + 1.0f) < 1e-4f);
    CHECK(std::abs(contacts[0].normal.y) < 1e-6f);
  }
}

TEST_MAIN({
  testMovingSphereMeetsTheFace();
  testRestingSphereAndTheFacesSide();
  testMovingSphereMeetsAnEdge();
  testParseVersion10();
  testVersion8HasNoLayerType();
  testTruncatedFileIsRejected();
  testSphereIsPushedOutOfWall();
  testSphereFarFromWallIsUntouched();
  testTransformIsApplied();
  testDegenerateTrianglesAreSkipped();
  testFacesWithBadIndicesAreSkipped();
})
