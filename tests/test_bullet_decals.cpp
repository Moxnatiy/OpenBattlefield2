// The material cells our rounds' impacts are looked up in (app/bullet_decals.h,
// docs/functions/own-impacts.md): `MaterialManager.createCell <projectile>
// <surface>` opens a cell, and its slot-0 `setDecalTemplate`,
// `setEffectTemplate` and `setSoundTemplate` fill it; other slots do not.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "obf2/app/bullet_decals.h"

namespace {

using namespace obf2;

void testCells() {
  const auto dir = std::filesystem::temp_directory_path() / "obf2_test_bullet_decals";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "common" / "material");
  std::ofstream(dir / "common" / "material" / "materialManagerSettings.con")
      << "MaterialManager.createCell 71 45\n"
         "MaterialManager.setDecalTemplate 0 Decal_BulletHole_Concrete\n"
         "MaterialManager.setEffectTemplate 0 e_bhit_s_concrete\n"
         "MaterialManager.setSoundTemplate 0 S_Impact_Bullet_Concrete\n"
         "MaterialManager.setEffectTemplate 1 e_ricochet\n"
         "MaterialManager.createCell 71 46\n"
         "MaterialManager.setEffectTemplate 1 only_a_ricochet\n";
  FileSystem files;
  CHECK(files.mountDirectory(dir));

  app::BulletDecals decals;
  CHECK_EQ(std::string(decals.impactEffect(files, 71, 45)), std::string("e_bhit_s_concrete"));
  CHECK_EQ(std::string(decals.impactSound(files, 71, 45)), std::string("S_Impact_Bullet_Concrete"));
  // Slot 1 is a ricochet or an exit, not the first contact.
  CHECK(decals.impactEffect(files, 71, 46).empty());
  // No cell, nothing.
  CHECK(decals.impactEffect(files, 1, 2).empty());
  CHECK_EQ(decals.count(), 0);
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  testCells();
  if (obf2test::g_failures) {
    std::fprintf(stderr, "\n%d checks failed\n", obf2test::g_failures);
    return 1;
  }
  std::puts("OK");
  return 0;
}
