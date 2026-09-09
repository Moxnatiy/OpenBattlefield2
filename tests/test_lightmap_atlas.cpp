#include <string>

#include "check.h"
#include "obf2/level/lightmap_atlas.h"

using namespace obf2;
using namespace obf2::level;

namespace {

// The header the game's own file carries, and four of its entries, copied
// verbatim out of Strike at Karkand's `LightmapAtlas.tai`. Tabs and all: the
// name and the rest are separated by two tabs there, and the parser must not
// care how many.
constexpr const char* kTai =
    "# mods/bf2/levels/strike_at_karkand/lightmaps/objects/LightmapAtlas.tai\r\n"
    "# \r\n"
    "# <filename>\t\t<atlas filename>, <atlas idx>, <woffset>, <hoffset>, <width>, <height>\r\n"
    "# \r\n"
    "levels/strike_at_karkand/lightmaps/objects/house_high_06=00=-226=166=59.dds\t\t"
    "levels/strike_at_karkand/lightmaps/objects/LightmapAtlas0.dds, 0, 0, 0, 0.5, 0.5\r\n"
    "levels/strike_at_karkand/lightmaps/objects/gas_station=00=-134=167=-277.dds\t\t"
    "levels/strike_at_karkand/lightmaps/objects/LightmapAtlas1.dds, 1, 0.5, 0, 0.5, 0.5\r\n"
    "levels/strike_at_karkand/lightmaps/objects/house_high_06=01=-226=166=59.dds\t\t"
    "levels/strike_at_karkand/lightmaps/objects/LightmapAtlas9.dds, 9, 0.25, 0, 0.25, 0.25\r\n"
    "levels/strike_at_karkand/lightmaps/objects/woodencrate_1m=02=-200=156=-14.dds\t\t"
    "levels/strike_at_karkand/lightmaps/objects/LightmapAtlas20.dds, 20, 0.179688, 0.648438, "
    "0.0078125, 0.0078125\r\n";

void testParsesTheGamesOwnFile() {
  const ObjectLightmaps maps = ObjectLightmaps::parse(kTai, "Strike_at_Karkand");
  // Four entries, and the comment lines are not among them.
  CHECK_EQ(maps.size(), std::size_t(4));
  // The highest index seen plus one, so the caller knows how many to load.
  CHECK_EQ(maps.atlasCount(), 21);

  const LightmapPlacement* house = maps.find("house_high_06", Vec3f{-226.0f, 166.0f, 59.0f});
  CHECK(house != nullptr);
  if (house != nullptr) {
    CHECK_EQ(house->atlas, 0);
    // woffset/hoffset are the offset, width/height the scale — the order in the
    // file is offset first, which is the opposite of the shader's xy/zw.
    CHECK_EQ(house->offsetU, 0.0f);
    CHECK_EQ(house->offsetV, 0.0f);
    CHECK_EQ(house->scaleU, 0.5f);
    CHECK_EQ(house->scaleV, 0.5f);
  }

  const LightmapPlacement* station = maps.find("gas_station", Vec3f{-134.0f, 167.0f, -277.0f});
  CHECK(station != nullptr);
  if (station != nullptr) {
    CHECK_EQ(station->atlas, 1);
    CHECK_EQ(station->offsetU, 0.5f);
    CHECK_EQ(station->scaleU, 0.5f);
  }
}

// The position in the key has its fraction cut off, not rounded. Measured over
// Strike at Karkand: truncation matches all 823 objects that have an entry,
// while rounding disagrees on 1115 positions and matches none of them.
void testPositionIsTruncatedNotRounded() {
  const ObjectLightmaps maps = ObjectLightmaps::parse(kTai, "Strike_at_Karkand");

  // 166.999 truncates to 166 and would round to 167.
  CHECK(maps.find("house_high_06", Vec3f{-226.4f, 166.999f, 59.7f}) != nullptr);
  // A negative one truncates towards zero: -226.8 -> -226, not -227.
  CHECK(maps.find("house_high_06", Vec3f{-226.8f, 166.2f, 59.1f}) != nullptr);
  // And a position that really is elsewhere finds nothing.
  CHECK(maps.find("house_high_06", Vec3f{-227.0f, 166.0f, 59.0f}) == nullptr);
}

// We draw lod 0, so `=00=` is the entry we want. The lower lods are in the same
// file under the same name and must not be picked up by mistake.
void testOnlyLodZeroIsFound() {
  const ObjectLightmaps maps = ObjectLightmaps::parse(kTai, "Strike_at_Karkand");
  const LightmapPlacement* house = maps.find("house_high_06", Vec3f{-226.0f, 166.0f, 59.0f});
  CHECK(house != nullptr);
  // The `=01=` entry of the same object sits on atlas 9; taking it would be
  // wrong, so the one we get must be the `=00=` on atlas 0.
  if (house != nullptr) CHECK_EQ(house->atlas, 0);

  // `woodencrate_1m` is in the file only at lod 2, so it has no light map for us.
  CHECK(maps.find("woodencrate_1m", Vec3f{-200.0f, 156.0f, -14.0f}) == nullptr);
}

void testTemplateNameIsCaseInsensitive() {
  const ObjectLightmaps maps = ObjectLightmaps::parse(kTai, "Strike_at_Karkand");
  // StaticObjects.con writes the names in the modeller's case; the file has
  // them lowercased.
  CHECK(maps.find("House_High_06", Vec3f{-226.0f, 166.0f, 59.0f}) != nullptr);
  CHECK(maps.find("GAS_STATION", Vec3f{-134.0f, 167.0f, -277.0f}) != nullptr);
}

void testAtlasPath() {
  const ObjectLightmaps maps = ObjectLightmaps::parse(kTai, "Strike_at_Karkand");
  CHECK_EQ(maps.atlasPath(7),
           std::string("Levels/Strike_at_Karkand/lightmaps/Objects/LightmapAtlas7.dds"));
}

// A level without the file is normal, not a failure.
void testEmptyInputIsEmpty() {
  const ObjectLightmaps none = ObjectLightmaps::parse("", "testmap");
  CHECK_EQ(none.size(), std::size_t(0));
  CHECK_EQ(none.atlasCount(), 0);
  CHECK(none.find("anything", Vec3f{0.0f, 0.0f, 0.0f}) == nullptr);

  // Header only, no entries.
  const ObjectLightmaps headerOnly = ObjectLightmaps::parse("# just a comment\r\n", "testmap");
  CHECK_EQ(headerOnly.size(), std::size_t(0));
}

}  // namespace

TEST_MAIN({
  testParsesTheGamesOwnFile();
  testPositionIsTruncatedNotRounded();
  testOnlyLodZeroIsFound();
  testTemplateNameIsCaseInsensitive();
  testAtlasPath();
  testEmptyInputIsEmpty();
})
