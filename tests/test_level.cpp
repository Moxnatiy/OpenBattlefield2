#include <cstdio>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/level/level.h"

namespace fs = std::filesystem;
using namespace obf2;

namespace {

// The level's own loader, in the shape every one of the game's 22 levels has
// it: the editor's branch above, the game's below. The game's runs the files in
// this order and does **not** run StaticObjects.con — that one the caller runs
// itself, because in the original it is reached through a `tmp.con` the game
// writes at load time and the shipped one is empty.
constexpr const char* kInit =
    "if v_arg1 == BF2Editor\n"
    "run Heightdata.con\n"
    "run Terrain.con BF2Editor\n"
    "run StaticObjects.con BF2Editor\n"
    "run Sky.con BF2Editor\n"
    "run Water.con\n"
    "else\n"
    "run Heightdata.con\n"
    "run Terrain.con v_arg2\n"
    "run Sky.con v_arg2\n"
    "run Water.con\n"
    "endIf\n";

// A level is read from the FileSystem, so the test makes a real directory with
// files — that also checks mounting and path resolution, not only the parsing.
class TempLevel {
 public:
  TempLevel() {
    root_ = fs::temp_directory_path() /
            ("obf2_level_test_" + std::to_string(::getpid()));
    fs::remove_all(root_);
    fs::create_directories(root_ / "Levels" / "testmap");
  }
  ~TempLevel() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  void write(const std::string& name, const std::string& text) const {
    std::ofstream out(root_ / "Levels" / "testmap" / name, std::ios::binary);
    out << text;
  }

  // The two files every level is loaded through, and neither of them was here
  // while we ran the editor's branch: Init.con is the chain the game follows,
  // and the compiled terrain is what its Terrain.con asks for.
  void writeCommon() const {
    write("Init.con", kInit);
    writeTerrainRaw();
  }

  // A `terraindata.raw` in the writer's order (docs/formats/terraindata.md),
  // small but real: the reader walks every field before it reaches the ones
  // this test looks at.
  void writeTerrainRaw() const {
    std::vector<char> bytes;
    auto raw = [&bytes](const void* from, std::size_t size) {
      const auto* at = static_cast<const char*>(from);
      bytes.insert(bytes.end(), at, at + size);
    };
    auto u32 = [&raw](std::uint32_t v) { raw(&v, sizeof(v)); };
    auto u8 = [&raw](std::uint8_t v) { raw(&v, sizeof(v)); };
    auto f32 = [&raw](float v) { raw(&v, sizeof(v)); };
    auto vec3 = [&f32](float x, float y, float z) { f32(x); f32(y); f32(z); };
    auto text = [&bytes](const std::string& value) {
      bytes.insert(bytes.end(), value.begin(), value.end());
      bytes.push_back('\n');
    };

    u32(0x0001001a);
    vec3(2.0f, 0.5f, 2.0f);   // primaryWorldScale
    vec3(4.0f, 1.0f, 4.0f);   // secondaryWorldScale
    u32(0xcdcdcdcd);          // the float the writer never initialises
    f32(100.0f);              // highest
    f32(0.0f);                // lowest
    u32(4);                   // patchSize
    u8(1);                    // subdividePatches
    u32(1);                   // patches per side
    u32(512);                 // patchColormapSize
    u32(256);                 // lowDetailmapSize
    text("Levels/testmap/Colormaps/tx");
    text("Levels/testmap/Detailmaps/tx");
    text("Levels/testmap/LowDetailmaps/tx");
    text("Levels/testmap/Lightmaps/tx");
    f32(5.0f); f32(6.0f);     // farSideTiling
    f32(24.0f);               // farTopTilingHi
    f32(4.0f);                // farTopTilingLow
    f32(0.0f);                // farYOffset
    vec3(0.75f, 0.71f, 0.57f);  // terrain.sunColor
    vec3(0.73f, 0.64f, 0.33f);  // terrain.GIColor
    vec3(0.34f, 0.28f, 0.16f);  // terrainWaterColor
    u32(1);                   // one material is enough to reach the end
    text("common/terrain/textures/detail/detail_rock04");
    u8(1); f32(32.0f); f32(16.0f); f32(50.0f); f32(0.0f); u8(0);

    std::ofstream out(root_ / "Levels" / "testmap" / "terraindata.raw", std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }

  void writeHeights(const std::string& name, const std::vector<std::uint16_t>& values) const {
    std::ofstream out(root_ / "Levels" / "testmap" / name, std::ios::binary);
    for (const std::uint16_t value : values) {
      const char bytes[2] = {static_cast<char>(value & 0xFF),
                             static_cast<char>((value >> 8) & 0xFF)};
      out.write(bytes, 2);
    }
  }

  const fs::path& root() const { return root_; }

 private:
  fs::path root_;
};

// 5x5 nodes = 4 quads per side: exactly one patch of 4.
constexpr const char* kHeightdata =
    "heightmapcluster.create HeighmapCluster\n"
    "heightmapcluster.addHeightmap Heightmap 0 0\n"
    "heightmap.setSize 5 5\n"
    "heightmap.setScale 2/0.5/2\n"
    "heightmap.setBitResolution 16\n"
    "heightmap.loadHeightData Levels/testmap/HeightmapPrimary.raw\n"
    "rem --- the secondary map: it must not replace the main one ---\n"
    "heightmapcluster.addHeightmap Heightmap 0 -1\n"
    "heightmap.setSize 257 257\n"
    "heightmap.setScale 8/1.64/8\n"
    "heightmap.setBitResolution 8\n"
    "heightmap.loadHeightData Levels/testmap/HeightmapSecondary_U1.raw\n"
    "heightmapcluster.setSeaWaterLevel 10.5\n";

constexpr const char* kTerrain =
    "if v_arg1 == BF2Editor\n"
    "terrain.create TerrainEditable\n"
    "terrain.patchSize 4\n"
    "terrain.colormapBaseName \"Levels/testmap/Colormaps/tx\"\n"
    "terrain.init\n"
    "else\n"
    "terrain.create Terrain\n"
    "terrain.load Levels/testmap/terraindata.raw\n"
    "endIf\n";

constexpr const char* kStaticObjects =
    "if v_arg1 == BF2Editor\n"
    "run /objects/staticobjects/test/test.con\n"
    "endIf\n"
    "\n"
    "rem *** hangar ***\n"
    "Object.create hangar\n"
    "Object.absolutePosition -59.987/176.238/-269.162\n"
    "Object.layer 1\n"
    "\n"
    "rem *** tower ***\n"
    "Object.create tower\n"
    "Object.absolutePosition -26.928/188.040/-163.616\n"
    "Object.rotation -66.6/1.5/0.0\n";

}  // namespace

static void testLoadLevel() {
  TempLevel temp;
  temp.writeCommon();
  temp.write("Heightdata.con", kHeightdata);
  temp.write("Terrain.con", kTerrain);
  temp.write("StaticObjects.con", kStaticObjects);
  temp.write("Water.con", "renderer.waterColor 0.25/0.5/0.75\n");

  // 25 nodes; the heights are multiplied by the scale 0.5.
  std::vector<std::uint16_t> heights(25, 0);
  heights[0] = 100;    // -> 50
  heights[12] = 200;   // the centre -> 100
  heights[24] = 40;    // -> 20
  temp.writeHeights("HeightmapPrimary.raw", heights);

  FileSystem files;
  CHECK(files.mountDirectory(temp.root()));

  std::string error;
  const auto level = level::loadLevel(files, "testmap", &error);
  CHECK(level.has_value());
  if (!level) {
    std::fprintf(stderr, "  reason: %s\n", error.c_str());
    return;
  }

  // A secondary 257x257 map must not replace the main one — a real trap: in
  // addHeightmap the zeroth argument is the name, not a coordinate.
  CHECK_EQ(level->primary.size, 5);
  CHECK_EQ(level->primary.bitResolution, 16);
  CHECK(level->primary.scale.y > 0.49f && level->primary.scale.y < 0.51f);
  CHECK(level->terrain.seaLevel > 10.4f && level->terrain.seaLevel < 10.6f);

  // Terrain.con is read by the game's branch, so none of this comes from the
  // `.con` at all — `terrain.load` sends the reader to the compiled blob and
  // every one of these fields is out of it.
  CHECK_EQ(level->terrain.patchSize, 4);
  CHECK_EQ(level->terrain.colormapBase, std::string("Levels/testmap/Colormaps/tx"));
  CHECK_EQ(level->terrain.lightmapBase, std::string("Levels/testmap/Lightmaps/tx"));
  CHECK_EQ(level->terrain.lowDetailmapSize, 256);
  CHECK_EQ(level->terrain.farSideTiling[1], 6.0f);
  CHECK_EQ(level->terrain.farTopTilingHi, 24.0f);
  CHECK_EQ(level->terrain.materials.size(), std::size_t(1));
  CHECK(level->terrain.materials[0].triPlanar);
  CHECK_EQ(level->terrain.materials[0].topTiling, 50.0f);
  // And the pair the light map is multiplied by, which the blob carries and
  // Sky.con would overwrite if the level had one.
  CHECK(level->terrain.terrainSunColor.x > 0.74f && level->terrain.terrainSunColor.x < 0.76f);
  CHECK(level->terrain.terrainSkyColor.z > 0.32f && level->terrain.terrainSkyColor.z < 0.34f);

  CHECK(level->terrain.waterColor.y > 0.49f && level->terrain.waterColor.y < 0.51f);

  CHECK(level->heightAt(0, 0) > 49.9f && level->heightAt(0, 0) < 50.1f);
  CHECK(level->heightAt(2, 2) > 99.9f && level->heightAt(2, 2) < 100.1f);
  CHECK(level->heightAt(4, 4) > 19.9f && level->heightAt(4, 4) < 20.1f);
  CHECK_EQ(level->heightAt(-1, 0), 0.0f);   // outside the bounds
  CHECK_EQ(level->heightAt(0, 99), 0.0f);
}

static void testStaticObjects() {
  TempLevel temp;
  temp.writeCommon();
  temp.write("Heightdata.con", kHeightdata);
  temp.write("Terrain.con", kTerrain);
  temp.write("StaticObjects.con", kStaticObjects);
  temp.writeHeights("HeightmapPrimary.raw", std::vector<std::uint16_t>(25, 0));

  FileSystem files;
  CHECK(files.mountDirectory(temp.root()));
  const auto level = level::loadLevel(files, "testmap");
  CHECK(level.has_value());
  if (!level) return;

  CHECK_EQ(level->objects.size(), std::size_t(2));
  CHECK_EQ(level->objects[0].templateName, std::string("hangar"));
  CHECK(level->objects[0].position.y > 176.2f && level->objects[0].position.y < 176.3f);
  CHECK(!level->objects[0].hasRotation);

  CHECK_EQ(level->objects[1].templateName, std::string("tower"));
  CHECK(level->objects[1].hasRotation);
  CHECK(level->objects[1].rotation.x < -66.5f && level->objects[1].rotation.x > -66.7f);
}

static void testTerrainCentredOnOrigin() {
  TempLevel temp;
  temp.writeCommon();
  temp.write("Heightdata.con", kHeightdata);
  temp.write("Terrain.con", kTerrain);
  temp.write("StaticObjects.con", "");
  temp.writeHeights("HeightmapPrimary.raw", std::vector<std::uint16_t>(25, 0));

  FileSystem files;
  files.mountDirectory(temp.root());
  const auto level = level::loadLevel(files, "testmap");
  CHECK(level.has_value());
  if (!level) return;

  // 5 nodes with a step of 2 -> from -4 to +4, the centre at zero: the objects'
  // positions in StaticObjects.con are given in exactly this system.
  CHECK_EQ(level->worldX(0), -4.0f);
  CHECK_EQ(level->worldX(2), 0.0f);
  CHECK_EQ(level->worldZ(4), 4.0f);
}

static void testPatchesSkipMissingColormaps() {
  TempLevel temp;
  temp.writeCommon();
  temp.write("Heightdata.con", kHeightdata);
  temp.write("Terrain.con", kTerrain);
  temp.write("StaticObjects.con", "");
  temp.writeHeights("HeightmapPrimary.raw", std::vector<std::uint16_t>(25, 0));

  FileSystem files;
  files.mountDirectory(temp.root());
  const auto level = level::loadLevel(files, "testmap");
  CHECK(level.has_value());
  if (!level) return;

  // There is no colour map at all — so there must be no patches either: in the game
  // those are patches entirely under water, and it does not draw them.
  const auto empty = level::buildTerrainPatches(*level, files);
  CHECK_EQ(empty.size(), std::size_t(0));

  // Now we add the only patch's colour map.
  fs::create_directories(temp.root() / "Levels" / "testmap" / "Colormaps");
  std::ofstream(temp.root() / "Levels" / "testmap" / "Colormaps" / "tx00x00.dds",
                std::ios::binary)
      << "does not matter";

  FileSystem refreshed;
  refreshed.mountDirectory(temp.root());
  const auto patches = level::buildTerrainPatches(*level, refreshed);
  CHECK_EQ(patches.size(), std::size_t(1));
  if (patches.empty()) return;

  // 4 quads per side -> 5x5 vertices and 32 triangles.
  CHECK_EQ(patches[0].geometry.vertices.size(), std::size_t(25));
  CHECK_EQ(patches[0].geometry.indices.size(), std::size_t(4 * 4 * 6));
  CHECK_EQ(patches[0].geometry.ranges.size(), std::size_t(1));
  CHECK_EQ(patches[0].colormap, std::string("Levels/testmap/Colormaps/tx00x00.dds"));
}

static void testWaterPlane() {
  TempLevel temp;
  temp.writeCommon();
  temp.write("Heightdata.con", kHeightdata);
  temp.write("Terrain.con", kTerrain);
  temp.write("StaticObjects.con", "");
  temp.write("Water.con", "renderer.waterColor 0.25/0.5/0.75\n");
  temp.writeHeights("HeightmapPrimary.raw", std::vector<std::uint16_t>(25, 0));

  FileSystem files;
  files.mountDirectory(temp.root());
  const auto level = level::loadLevel(files, "testmap");
  CHECK(level.has_value());
  if (!level) return;

  const auto water = level::buildWaterPlane(*level);
  CHECK_EQ(water.vertices.size(), std::size_t(4));
  CHECK_EQ(water.indices.size(), std::size_t(6));
  // A plane at sea level and as wide as the map.
  CHECK(water.vertices[0].position.y > 10.4f && water.vertices[0].position.y < 10.6f);
  CHECK_EQ(water.vertices[0].position.x, -4.0f);
  CHECK_EQ(water.ranges.size(), std::size_t(1));
  CHECK_EQ(water.ranges[0].maps.at(0), std::string(level::kWaterColorMap));
}

static void testMissingHeightmapIsReported() {
  TempLevel temp;
  temp.writeCommon();
  temp.write("Heightdata.con", kHeightdata);
  temp.write("Terrain.con", kTerrain);
  temp.write("StaticObjects.con", "");
  // the .raw is deliberately not written

  FileSystem files;
  files.mountDirectory(temp.root());
  std::string error;
  CHECK(!level::loadLevel(files, "testmap", &error).has_value());
  CHECK(!error.empty());
}

// Sky.con's `Skydome.*` block. Every one of the game's 13 levels sets all
// fourteen commands, so all fourteen are read — and the texture paths in the
// data are Windows-shaped and extensionless, which is the part that has to be
// normalised on the way in.
static void testSkydomeBlock() {
  TempLevel temp;
  temp.writeCommon();
  temp.write("Heightdata.con", kHeightdata);
  temp.write("Terrain.con", kTerrain);
  temp.write("StaticObjects.con", "");
  temp.write("Water.con", "");
  // Copied from Strike at Karkand's own Sky.con, backslashes and all.
  temp.write("Sky.con",
             "Skydome.skyTemplate skydome\n"
             "Skydome.cloudTemplate cloudlayer\n"
             "Skydome.hasCloudLayer 0\n"
             "Skydome.hasCloudLayer2 1\n"
             "Skydome.scrolldirection 0.003/0.007\n"
             "Skydome.scrolldirection2 -0.001/-0.003\n"
             "Skydome.cloudTexture common\\textures\\cloud\\Cloud03\n"
             "Skydome.skyTexture common\\textures\\sky\\karkand_cloudy\n"
             "Skydome.domeRotation 60\n"
             "Skydome.fadeCloudsDistances 900/500\n"
             "Skydome.cloudLerpFactors 0.5/0.25\n"
             "Skydome.flareTexture common\\textures\\sunflare\\Sunglow_32bit_v2\n"
             "Skydome.flareDirection 0.25/-0.5/0.75\n"
             "Renderer.fogColor 163.00/135.00/86.00\n"
             "Renderer.fogStartEndAndBase 0.00/135.00/2.30/0.40\n");
  temp.writeHeights("HeightmapPrimary.raw", std::vector<std::uint16_t>(25, 0));

  FileSystem files;
  files.mountDirectory(temp.root());
  const auto level = level::loadLevel(files, "testmap");
  CHECK(level.has_value());
  if (!level) return;

  CHECK_EQ(level->sky.domeTemplate, std::string("skydome"));
  CHECK_EQ(level->sky.cloudTemplate, std::string("cloudlayer"));
  CHECK_EQ(level->sky.domeRotation, 60.0f);
  CHECK(!level->sky.hasCloudLayer);
  CHECK(level->sky.hasCloudLayer2);

  // Backslashes become slashes and `.dds` is appended: the archives are mounted
  // the other way round from how the data is written.
  CHECK_EQ(level->sky.texture, std::string("common/textures/sky/karkand_cloudy.dds"));
  CHECK_EQ(level->sky.cloudTexture, std::string("common/textures/cloud/Cloud03.dds"));
  CHECK_EQ(level->sky.flareTexture,
           std::string("common/textures/sunflare/Sunglow_32bit_v2.dds"));

  CHECK_EQ(level->sky.scrollDirection[0], 0.003f);
  CHECK_EQ(level->sky.scrollDirection2[1], -0.003f);
  CHECK_EQ(level->sky.fadeCloudsDistances[0], 900.0f);
  CHECK_EQ(level->sky.cloudLerpFactors[1], 0.25f);
  CHECK_EQ(level->sky.flareDirection.y, -0.5f);

  // The same file carries the fog, and the third and fourth numbers of
  // fogStartEndAndBase are read past without disturbing the first two.
  CHECK_EQ(level->terrain.fogStart, 0.0f);
  CHECK_EQ(level->terrain.fogEnd, 135.0f);

  // With no dome mesh in the mounted tree there is nothing to build, and that
  // is not an error: a level may name a template we do not have.
  CHECK(!level::buildSkyDome(*level, files).has_value());
}

TEST_MAIN({
  testLoadLevel();
  testSkydomeBlock();
  testStaticObjects();
  testTerrainCentredOnOrigin();
  testPatchesSkipMissingColormaps();
  testWaterPlane();
  testMissingHeightmapIsReported();
})
