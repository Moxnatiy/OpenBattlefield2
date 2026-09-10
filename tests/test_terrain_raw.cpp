// The compiled terrain, `Levels/<name>/terraindata.raw`. The blob here is built
// the way the writer builds it — `TerrainEditable::save` (`RendDX9.dll`,
// 0x1010cd70), field by field, docs/formats/terraindata.md — so the test says
// the reader walks the writer's order and not merely its own.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/level/terrain_raw.h"

using namespace obf2;

namespace {

// The blob is little-endian and unaligned: everything is written in order with
// no padding, and the strings carry a newline instead of a length.
class Blob {
 public:
  void u32(std::uint32_t value) { raw(&value, sizeof(value)); }
  void u8(std::uint8_t value) { raw(&value, sizeof(value)); }
  void f32(float value) { raw(&value, sizeof(value)); }
  void vec3(float x, float y, float z) { f32(x); f32(y); f32(z); }
  void string(const std::string& text) {
    bytes_.insert(bytes_.end(), text.begin(), text.end());
    bytes_.push_back('\n');
  }

  std::span<const std::byte> span() const {
    return {reinterpret_cast<const std::byte*>(bytes_.data()), bytes_.size()};
  }
  std::size_t size() const { return bytes_.size(); }

 private:
  void raw(const void* from, std::size_t size) {
    const auto* at = static_cast<const char*>(from);
    bytes_.insert(bytes_.end(), at, at + size);
  }

  std::vector<char> bytes_;
};

// The header the game's own levels carry, with two materials instead of six —
// the count is read from the file, so the reader must not assume the six.
Blob makeTerrain(std::uint32_t materialCount = 2) {
  Blob b;
  b.u32(0x0001001a);
  b.vec3(2.0f, 1.0f, 2.0f);   // primaryWorldScale
  b.vec3(4.0f, 1.0f, 4.0f);   // secondaryWorldScale
  b.u32(0xcdcdcdcd);          // the float the writer never initialises
  b.f32(120.5f);              // highest height
  b.f32(-3.25f);              // lowest height
  b.u32(64);                  // patchSize
  b.u8(1);                    // subdividePatches
  b.u32(16);                  // patches per side
  b.u32(512);                 // patchColormapSize
  b.u32(1024);                // lowDetailmapSize
  b.string("Levels/testmap/Colormaps/tx");
  b.string("Levels/testmap/Detailmaps/tx");
  b.string("Levels/testmap/TX");
  b.string("Levels/testmap/Lightmaps/tx");
  b.f32(5.0f);                // farSideTiling
  b.f32(6.0f);
  b.f32(24.0f);               // farTopTilingHi
  b.f32(4.0f);                // farTopTilingLow
  b.f32(0.5f);                // farYOffset
  b.vec3(1.0f, 0.9f, 0.8f);   // terrain.sunColor
  b.vec3(0.2f, 0.3f, 0.4f);   // terrain.GIColor
  b.vec3(0.1f, 0.2f, 0.3f);   // terrainWaterColor
  b.u32(materialCount);
  for (std::uint32_t i = 0; i < materialCount; ++i) {
    b.string("common/terrain/textures/detail/detail_rock0" + std::to_string(i));
    b.u8(static_cast<std::uint8_t>(i == 0 ? 1 : 0));  // tri-planar
    b.f32(32.0f + static_cast<float>(i));             // side tiling x
    b.f32(16.0f + static_cast<float>(i));             // side tiling y
    b.f32(50.0f);                                     // top tiling
    b.f32(0.25f);                                     // y offset
    b.u8(static_cast<std::uint8_t>(i == 5 ? 1 : 0));  // environment map
  }
  // What follows in a real file is one block per patch; the reader stops here.
  b.u32(0xdeadbeef);
  return b;
}

void testHeader() {
  const Blob blob = makeTerrain();
  std::string error;
  const auto raw = level::readTerrainRaw(blob.span(), &error);
  CHECK(raw.has_value());
  if (!raw) {
    std::fprintf(stderr, "  %s\n", error.c_str());
    return;
  }
  CHECK_EQ(raw->version, 0x0001001au);
  CHECK_EQ(raw->primaryWorldScale.x, 2.0f);
  CHECK_EQ(raw->secondaryWorldScale.x, 4.0f);
  CHECK_EQ(raw->highestHeight, 120.5f);
  CHECK_EQ(raw->lowestHeight, -3.25f);
  CHECK_EQ(raw->patchSize, 64);
  CHECK_EQ(raw->subdividePatches, true);
  CHECK_EQ(raw->patchesPerSide, 16);
  CHECK_EQ(raw->patchColormapSize, 512);
  CHECK_EQ(raw->lowDetailmapSize, 1024);
  CHECK_EQ(raw->colormapBase, std::string("Levels/testmap/Colormaps/tx"));
  CHECK_EQ(raw->detailmapBase, std::string("Levels/testmap/Detailmaps/tx"));
  CHECK_EQ(raw->lowDetailmapBase, std::string("Levels/testmap/TX"));
  CHECK_EQ(raw->lightmapBase, std::string("Levels/testmap/Lightmaps/tx"));
  CHECK_EQ(raw->farSideTiling[0], 5.0f);
  CHECK_EQ(raw->farSideTiling[1], 6.0f);
  CHECK_EQ(raw->farTopTilingHi, 24.0f);
  CHECK_EQ(raw->farTopTilingLow, 4.0f);
  CHECK_EQ(raw->farYOffset, 0.5f);
  CHECK_EQ(raw->sunColor.y, 0.9f);
  CHECK_EQ(raw->giColor.z, 0.4f);
  CHECK_EQ(raw->waterColor.x, 0.1f);
}

void testMaterials() {
  const Blob blob = makeTerrain(6);
  const auto raw = level::readTerrainRaw(blob.span());
  CHECK(raw.has_value());
  if (!raw) return;
  CHECK_EQ(raw->materials.size(), std::size_t(6));
  CHECK_EQ(raw->materials[0].texture,
           std::string("common/terrain/textures/detail/detail_rock00"));
  CHECK_EQ(raw->materials[0].triPlanar, true);
  CHECK_EQ(raw->materials[0].sideTilingX, 32.0f);
  CHECK_EQ(raw->materials[0].sideTilingY, 16.0f);
  CHECK_EQ(raw->materials[0].topTiling, 50.0f);
  CHECK_EQ(raw->materials[0].yOffset, 0.25f);
  CHECK_EQ(raw->materials[0].envMap, false);
  CHECK_EQ(raw->materials[5].texture,
           std::string("common/terrain/textures/detail/detail_rock05"));
  CHECK_EQ(raw->materials[5].sideTilingX, 37.0f);
  CHECK_EQ(raw->materials[5].triPlanar, false);
  CHECK_EQ(raw->materials[5].envMap, true);
}

// A blob that ends in the middle must fail rather than hand back a half-read
// header: the file comes out of the user's own archives.
void testShortFile() {
  const Blob blob = makeTerrain();
  for (const std::size_t cut : {std::size_t(0), std::size_t(9), blob.size() / 2}) {
    std::string error;
    const auto raw = level::readTerrainRaw(blob.span().subspan(0, cut), &error);
    CHECK(!raw.has_value());
    CHECK(!error.empty());
  }
}

// A wild count means the walk has gone off the rails, and the reader says so
// instead of allocating whatever the number asks for.
void testImplausibleCount() {
  const Blob blob = makeTerrain(1000);
  std::string error;
  const auto raw = level::readTerrainRaw(blob.span(), &error);
  CHECK(!raw.has_value());
  CHECK(error.find("materials") != std::string::npos);
}

}  // namespace

int main() {
  testHeader();
  testMaterials();
  testShortFile();
  testImplausibleCount();
  if (obf2test::g_failures == 0) std::printf("test_terrain_raw: ok\n");
  return obf2test::g_failures == 0 ? 0 : 1;
}
