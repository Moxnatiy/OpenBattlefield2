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

  // Terrain.con is read by the editor branch.
  CHECK_EQ(level->terrain.patchSize, 4);
  CHECK_EQ(level->terrain.colormapBase, std::string("Levels/testmap/Colormaps/tx"));

  CHECK(level->terrain.waterColor.y > 0.49f && level->terrain.waterColor.y < 0.51f);

  CHECK(level->heightAt(0, 0) > 49.9f && level->heightAt(0, 0) < 50.1f);
  CHECK(level->heightAt(2, 2) > 99.9f && level->heightAt(2, 2) < 100.1f);
  CHECK(level->heightAt(4, 4) > 19.9f && level->heightAt(4, 4) < 20.1f);
  CHECK_EQ(level->heightAt(-1, 0), 0.0f);   // outside the bounds
  CHECK_EQ(level->heightAt(0, 99), 0.0f);
}

static void testStaticObjects() {
  TempLevel temp;
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

TEST_MAIN({
  testLoadLevel();
  testStaticObjects();
  testTerrainCentredOnOrigin();
  testPatchesSkipMissingColormaps();
  testWaterPlane();
  testMissingHeightmapIsReported();
})
