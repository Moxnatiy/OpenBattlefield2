#include <cmath>
#include <cstring>
#include <vector>

#include "check.h"
#include "obf2/mesh/collision.h"
#include "obf2/server/collision_world.h"

using namespace obf2;

namespace {

// Збирач бінарного .collisionmesh — тест не залежить від наявності гри
// і заодно фіксує розкладку формату в коді.
class CollisionBuilder {
 public:
  explicit CollisionBuilder(std::uint32_t versionMinor) : versionMinor_(versionMinor) {
    u32(0);              // versionMajor
    u32(versionMinor_);  // versionMinor
    u32(1);              // geometryPartCount
    u32(1);              // geometryCount
    u32(1);              // colCount
  }

  // Один шар з одного трикутника.
  void addLayer(mesh::ColType type, const mesh::Vec3& a, const mesh::Vec3& b,
                const mesh::Vec3& c) {
    if (versionMinor_ >= 9) u32(static_cast<std::uint32_t>(type));

    u32(1);  // faceCount
    u16(0); u16(1); u16(2); u16(0);

    u32(3);  // vertexCount
    vec3(a); vec3(b); vec3(c);
    u16(0); u16(0); u16(0);  // матеріали вершин

    vec3(a);  // bounds min (грубо)
    vec3(c);  // bounds max
    byte('0');  // BSP немає

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

// Велика горизонтальна площина на висоті y.
mesh::CollisionLayer makeWall(float x) {
  mesh::CollisionLayer layer;
  // Вертикальна стіна в площині X = x.
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
    std::fprintf(stderr, "  причина: %s\n", error.c_str());
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
  // У версії 0.8 поля типу немає, і шар визначається порядком. Через це
  // 241 файл гри раніше розбирався зі зсувом.
  CollisionBuilder builder(8);
  builder.addLayer(mesh::ColType::Projectile, mesh::Vec3{0, 0, 0}, mesh::Vec3{1, 0, 0},
                   mesh::Vec3{0, 1, 0});

  const auto mesh = mesh::loadCollisionMesh(builder.bytes());
  CHECK(mesh.has_value());
  if (!mesh) return;
  CHECK_EQ(mesh->layers.size(), std::size_t(1));
  // Перший шар отримує тип за індексом.
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
      // Дуже короткий обрізок може випадково дати «порожній» валідний меш;
      // головне, щоб не було читання за межами.
      continue;
    }
    CHECK(!error.empty());
  }
}

static void testSphereIsPushedOutOfWall() {
  server::CollisionWorld world;
  world.addLayer(makeWall(0.0f), Mat4::identity());
  CHECK_EQ(world.triangleCount(), std::size_t(2));

  // Сфера радіусом 0.5 з центром за 0.2 від стіни — має бути виштовхнута.
  Vec3f position{-0.2f, 0.0f, 0.0f};
  const int pushes = world.resolveSphere(position, 0.5f);
  CHECK(pushes > 0);
  // Виштовхує в той бік, з якого підійшли, і рівно на радіус.
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
  // Стіна, посунута на 10 по X, має зупиняти саме там.
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
  // Індекс за межами масиву вершин не має валити сервер.
  mesh::CollisionLayer layer;
  layer.vertices = {mesh::Vec3{0, 0, 0}, mesh::Vec3{1, 0, 0}, mesh::Vec3{0, 1, 0}};
  layer.faces = {mesh::CollisionFace{0, 1, 99, 0}, mesh::CollisionFace{0, 1, 2, 0}};

  server::CollisionWorld world;
  world.addLayer(layer, Mat4::identity());
  CHECK_EQ(world.triangleCount(), std::size_t(1));
}

TEST_MAIN({
  testParseVersion10();
  testVersion8HasNoLayerType();
  testTruncatedFileIsRejected();
  testSphereIsPushedOutOfWall();
  testSphereFarFromWallIsUntouched();
  testTransformIsApplied();
  testDegenerateTrianglesAreSkipped();
  testFacesWithBadIndicesAreSkipped();
})
