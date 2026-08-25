#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/mesh/bf2_mesh.h"

using namespace obf2::mesh;

namespace {

// Збирач бінарного .staticmesh. Тест не має залежати від наявності гри на
// диску, тому мінімальний валідний меш будуємо руками — заодно це фіксує
// розкладку формату в коді, а не лише в документації.
class MeshBuilder {
 public:
  void u32(std::uint32_t value) { raw(&value, sizeof(value)); }
  void u16(std::uint16_t value) { raw(&value, sizeof(value)); }
  void u8(std::uint8_t value) { raw(&value, sizeof(value)); }
  void f32(float value) { raw(&value, sizeof(value)); }
  void vec3(float x, float y, float z) { f32(x); f32(y); f32(z); }

  void string(const std::string& value) {
    u32(static_cast<std::uint32_t>(value.size()));
    raw(value.data(), value.size());
  }

  void identityMatrix() {
    for (int i = 0; i < 16; ++i) f32(i % 5 == 0 ? 1.0f : 0.0f);
  }

  const std::vector<std::byte>& bytes() const { return bytes_; }

 private:
  void raw(const void* data, std::size_t size) {
    const auto* source = static_cast<const std::byte*>(data);
    bytes_.insert(bytes_.end(), source, source + size);
  }
  std::vector<std::byte> bytes_;
};

// Один трикутник: позиція + нормаль + UV, stride 32 байти.
std::vector<std::byte> buildTriangleMesh(std::uint32_t version = 11) {
  MeshBuilder mesh;

  mesh.u32(0);        // header.u1
  mesh.u32(version);  // header.version
  mesh.u32(0);        // header.u3
  mesh.u32(0);        // header.u4
  mesh.u32(0);        // header.u5
  mesh.u8(0);         // маркер гри: 0 = BF2, не Play4Free

  mesh.u32(1);  // geom
  mesh.u32(1);  // lod-ів у geom 0

  mesh.u32(3);                       // атрибутів
  mesh.u16(0); mesh.u16(0);  mesh.u16(2); mesh.u16(0);  // POSITION float3 @0
  mesh.u16(0); mesh.u16(12); mesh.u16(2); mesh.u16(3);  // NORMAL   float3 @12
  mesh.u16(0); mesh.u16(24); mesh.u16(1); mesh.u16(5);  // TEXCOORD float2 @24

  mesh.u32(4);   // vertexFormat: розмір компонента
  mesh.u32(32);  // vertexStride
  mesh.u32(3);   // вершин
  mesh.vec3(0.0f, 0.0f, 0.0f); mesh.vec3(0.0f, 1.0f, 0.0f); mesh.f32(0.0f); mesh.f32(0.0f);
  mesh.vec3(1.0f, 0.0f, 0.0f); mesh.vec3(0.0f, 1.0f, 0.0f); mesh.f32(1.0f); mesh.f32(0.0f);
  mesh.vec3(0.0f, 0.0f, 1.0f); mesh.vec3(0.0f, 1.0f, 0.0f); mesh.f32(0.0f); mesh.f32(1.0f);

  mesh.u32(3);  // індексів
  mesh.u16(0); mesh.u16(1); mesh.u16(2);

  mesh.u32(0);  // u2 — є в усіх, крім skinned

  // lod 0
  mesh.vec3(0.0f, 0.0f, 0.0f);  // min
  mesh.vec3(1.0f, 0.0f, 1.0f);  // max
  if (version <= 6) mesh.vec3(0.0f, 0.0f, 0.0f);  // pivot
  mesh.u32(1);                                    // вузлів
  mesh.identityMatrix();

  // матеріали lod 0
  mesh.u32(1);
  mesh.u32(0);                     // alphaMode
  mesh.string("StaticMesh.fx");    // fxFile
  mesh.string("Base");             // technique
  mesh.u32(1);                     // текстур
  mesh.string("objects/test.dds");
  mesh.u32(0);  // vertexStart
  mesh.u32(0);  // indexStart
  mesh.u32(3);  // indexCount
  mesh.u32(3);  // vertexCount
  mesh.u32(0);  // nodeIndex
  mesh.u16(0);
  mesh.u16(0);
  if (version == 11) {  // bounds лише у версії 11
    mesh.vec3(0.0f, 0.0f, 0.0f);
    mesh.vec3(1.0f, 0.0f, 1.0f);
  }

  return mesh.bytes();
}

}  // namespace

static void testKindFromExtension() {
  CHECK(kindFromExtension("staticmesh") == Kind::Static);
  CHECK(kindFromExtension("bundledmesh") == Kind::Bundled);
  CHECK(kindFromExtension("skinnedmesh") == Kind::Skinned);
  CHECK(!kindFromExtension("dds").has_value());
}

static void testParseTriangle() {
  const auto bytes = buildTriangleMesh();
  std::string error;
  const auto mesh = load(bytes, Kind::Static, &error);
  CHECK(mesh.has_value());
  if (!mesh) {
    std::fprintf(stderr, "  причина: %s\n", error.c_str());
    return;
  }

  CHECK_EQ(mesh->header.version, 11u);
  CHECK_EQ(mesh->vertexCount, 3u);
  CHECK_EQ(mesh->vertexStride, 32u);
  CHECK_EQ(mesh->floatsPerVertex(), std::size_t(8));
  CHECK_EQ(mesh->indices.size(), std::size_t(3));
  CHECK_EQ(mesh->geometries.size(), std::size_t(1));
  CHECK_EQ(mesh->geometries[0].lods.size(), std::size_t(1));

  const Lod& lod = mesh->geometries[0].lods[0];
  CHECK_EQ(lod.nodes.size(), std::size_t(1));
  CHECK_EQ(lod.materials.size(), std::size_t(1));
  CHECK_EQ(lod.materials[0].fxFile, std::string("StaticMesh.fx"));
  CHECK_EQ(lod.materials[0].technique, std::string("Base"));
  CHECK_EQ(lod.materials[0].maps.size(), std::size_t(1));
  CHECK(lod.materials[0].hasBounds);
}

static void testExtractGeometry() {
  const auto bytes = buildTriangleMesh();
  const auto mesh = load(bytes, Kind::Static);
  CHECK(mesh.has_value());
  if (!mesh) return;

  std::string error;
  const auto render = extract(*mesh, 0, 0, &error);
  CHECK(render.has_value());
  if (!render) {
    std::fprintf(stderr, "  причина: %s\n", error.c_str());
    return;
  }

  CHECK_EQ(render->vertices.size(), std::size_t(3));
  CHECK_EQ(render->indices.size(), std::size_t(3));
  CHECK_EQ(render->ranges.size(), std::size_t(1));
  // Канали мають потрапити у свої поля, а не переплутатися місцями.
  CHECK_EQ(render->vertices[1].position.x, 1.0f);
  CHECK_EQ(render->vertices[2].position.z, 1.0f);
  CHECK_EQ(render->vertices[0].normal.y, 1.0f);
  CHECK_EQ(render->vertices[2].uv[1], 1.0f);
  CHECK_EQ(render->bounds.max.x, 1.0f);
}

static void testVersion6HasPivot() {
  // У версіях <= 6 у lod додається pivot; якщо його не прочитати, поїде
  // весь подальший розбір.
  const auto bytes = buildTriangleMesh(6);
  const auto mesh = load(bytes, Kind::Static);
  CHECK(mesh.has_value());
  if (mesh) {
    CHECK_EQ(mesh->geometries[0].lods[0].materials.size(), std::size_t(1));
    // bounds пишуться лише у версії 11
    CHECK(!mesh->geometries[0].lods[0].materials[0].hasBounds);
  }
}

static void testTruncatedFilesAreRejected() {
  // Файли приходять з архівів користувача: обрізаний або зіпсований меш має
  // давати помилку, а не читання за межами буфера.
  const auto full = buildTriangleMesh();
  for (std::size_t size = 0; size < full.size(); size += 7) {
    std::vector<std::byte> truncated(full.begin(), full.begin() + static_cast<long>(size));
    std::string error;
    const auto mesh = load(truncated, Kind::Static, &error);
    if (mesh.has_value()) {
      std::fprintf(stderr, "FAIL: обрізаний до %zu байт меш розібрався\n", size);
      ++obf2test::g_failures;
    } else {
      CHECK(!error.empty());
    }
  }
}

static void testAbsurdCountsAreRejected() {
  // Класична атака на парсер: лічильник у заголовку значно більший за файл.
  auto bytes = buildTriangleMesh();
  const std::uint32_t absurd = 0xFFFFFFFFu;
  std::memcpy(bytes.data() + 21, &absurd, sizeof(absurd));  // geometryCount

  std::string error;
  CHECK(!load(bytes, Kind::Static, &error).has_value());
  CHECK(!error.empty());
}

TEST_MAIN({
  testKindFromExtension();
  testParseTriangle();
  testExtractGeometry();
  testVersion6HasPivot();
  testTruncatedFilesAreRejected();
  testAbsurdCountsAreRejected();
})
