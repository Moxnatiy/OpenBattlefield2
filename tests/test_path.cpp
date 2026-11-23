#include "check.h"
#include "obf2/core/path.h"

#include <chrono>
#include <fstream>

using obf2::assetExtension;
using obf2::assetParentDir;
using obf2::joinAssetPath;
using obf2::normalizeAssetPath;

static void testNormalize() {
  // The main case: BF2's asset paths are Windows-style and of any case.
  CHECK_EQ(normalizeAssetPath(R"(Ingame\Weapons\Icons\Hud\icon_M16m203.tga)"),
           std::string("ingame/weapons/icons/hud/icon_m16m203.tga"));
  CHECK_EQ(normalizeAssetPath("Objects//Weapons///Handheld/"),
           std::string("objects/weapons/handheld"));
  CHECK_EQ(normalizeAssetPath("a/./b/../c"), std::string("a/c"));
  CHECK_EQ(normalizeAssetPath("../../x"), std::string("x"));
  CHECK_EQ(normalizeAssetPath(""), std::string(""));
}

static void testJoinAndSplit() {
  CHECK_EQ(joinAssetPath("weapons/handheld/ammokit", "ammokit.tweak"),
           std::string("weapons/handheld/ammokit/ammokit.tweak"));
  CHECK_EQ(joinAssetPath("weapons/handheld/ammokit", R"(..\shared\common.con)"),
           std::string("weapons/handheld/shared/common.con"));
  CHECK_EQ(std::string(assetParentDir("a/b/c.con")), std::string("a/b"));
  CHECK_EQ(std::string(assetParentDir("c.con")), std::string(""));
  CHECK_EQ(std::string(assetExtension(normalizeAssetPath("a/b.TWEAK"))), std::string("tweak"));
  CHECK_EQ(std::string(assetExtension("a.b/c")), std::string(""));
}

TEST_MAIN({
  testNormalize();
  testJoinAndSplit();
  namespace fs = std::filesystem;
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = fs::temp_directory_path() / ("obf2-path-" + std::to_string(suffix));
  const auto archive = root / "Levels" / "Test_Map" / "Client.ZIP";
  fs::create_directories(archive.parent_path());
  std::ofstream(archive).put('x');
  CHECK(fs::equivalent(obf2::resolveAssetPath(root, R"(levels\test_map\client.zip)"), archive));
  CHECK(obf2::resolveAssetPath(root, "../Client.ZIP").empty());
  CHECK(obf2::resolveAssetPath(root, "Levels/Test_Map/server.zip").empty());
  fs::remove_all(root);
})
