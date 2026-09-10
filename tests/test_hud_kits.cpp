// A kit row of the spawn screen, built out of the kit's own ObjectTemplate.
//
// The kits below are cut down from the game's real ones (`Kits/US/*.con`), and
// what is checked is what a frame dump of the original really drew on Strike at
// Karkand (docs/research/spawn-screen-named.md, docs/functions/hud-kits.md).
#include <map>
#include <string>

#include "check.h"
#include "obf2/hud/kit_list.h"

using namespace obf2;

namespace {

class MemoryFiles : public con::FileProvider {
 public:
  std::map<std::string, std::string> files;
  std::optional<std::string> loadText(std::string_view path) override {
    const auto it = files.find(std::string(path));
    return it == files.end() ? std::nullopt : std::optional<std::string>{it->second};
  }
};

game::Registry runInto(MemoryFiles& files, const std::string& entry) {
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile(entry);
  return registry;
}

// A weapon, the way the game writes one: an item index, and a WeaponHud with the
// three pictures a kit row can want from it.
std::string weapon(const char* name, int itemIndex, const char* icon, const char* alt,
                   const char* ability) {
  std::string text = std::string("ObjectTemplate.create GenericFireArm ") + name + "\n";
  text += "ObjectTemplate.itemIndex " + std::to_string(itemIndex) + "\n";
  text += "ObjectTemplate.createComponent WeaponHud\n";
  if (icon != nullptr) text += std::string("ObjectTemplate.weaponHud.weaponIcon ") + icon + "\n";
  if (alt != nullptr) text += std::string("ObjectTemplate.weaponHud.altWeaponIcon ") + alt + "\n";
  if (ability != nullptr) {
    text += std::string("ObjectTemplate.weaponHud.specialAbilityIcon ") + ability + "\n";
  }
  return text;
}

}  // namespace

// US_Specops: the row shows the M4 (item index 3), the level 2 unlock's mini
// picture, the kit icon and two ability icons in the order the items were added.
static void testSpecops() {
  MemoryFiles files;
  files.files["kits.con"] =
      "ObjectTemplate.create Kit US_Specops\n"
      "ObjectTemplate.unlockIndex 4\n"
      "ObjectTemplate.addTemplate USPIS_92FS_silencer\n"
      "ObjectTemplate.addTemplate USHGR_M67\n"
      "ObjectTemplate.addTemplate c4_explosives\n"
      "ObjectTemplate.addTemplate kni_knife\n"
      "ObjectTemplate.addTemplate UnlockUSSpecops\n"
      "ObjectTemplate.addTemplate UnlockUSSpecops2\n"
      "ObjectTemplate.addTemplate USRIF_M4\n"
      "ObjectTemplate.createComponent VehicleHud\n"
      "ObjectTemplate.vehicleHud.hudName \"HUD_TEXT_MENU_SPAWN_KIT_SPECIALFORCES\"\n"
      "ObjectTemplate.vehicleHud.vehicleIcon \"Ingame\\Kits\\Icons\\kit_Specops.tga\"\n"
      "ObjectTemplate.sprintStaminaDissipationFactor 0.2\n" +
      weapon("USPIS_92FS_silencer", 2, "Ingame\\Weapons\\Icons\\Hud\\USPIS_92FS.tga",
             "Ingame\\Weapons\\Icons\\Hud\\USPIS_92FS_mini.tga", nullptr) +
      weapon("USHGR_M67", 4, nullptr, nullptr,
             "Ingame\\Weapons\\Icons\\Hud\\SpecialKitIcons\\handgrenade.tga") +
      weapon("c4_explosives", 5, nullptr, nullptr,
             "Ingame\\Weapons\\Icons\\Hud\\SpecialKitIcons\\c4.tga") +
      weapon("kni_knife", 1, nullptr, nullptr, nullptr) +
      weapon("USRIF_M4", 3, "Ingame\\Weapons\\Icons\\Hud\\USRIF_M4.tga",
             "Ingame\\Weapons\\Icons\\Hud\\USRIF_M4_mini.tga", nullptr) +
      weapon("usrif_g36c", 3, "Ingame\\Weapons\\Icons\\Hud\\usrif_g36c.tga",
             "Ingame\\Weapons\\Icons\\Hud\\usrif_g36c_mini.tga", nullptr) +
      weapon("usrif_fnscarl", 3, "Ingame\\Weapons\\Icons\\Hud\\usrif_fnscarl.tga",
             "Ingame\\Weapons\\Icons\\Hud\\usrif_fnscarl_mini.tga", nullptr) +
      "ObjectTemplate.create ItemContainer UnlockUSSpecops\n"
      "ObjectTemplate.addTemplate usrif_g36c\n"
      "ObjectTemplate.unlockLevel 1\n"
      "ObjectTemplate.create ItemContainer UnlockUSSpecops2\n"
      "ObjectTemplate.addTemplate usrif_fnscarl\n"
      "ObjectTemplate.unlockLevel 2\n";

  const game::Registry registry = runInto(files, "kits.con");
  const hud::KitRow row = hud::buildKitRow(registry, "US_Specops");

  CHECK_EQ(row.nameKey, std::string("HUD_TEXT_MENU_SPAWN_KIT_SPECIALFORCES"));
  CHECK_EQ(row.icon, std::string("Ingame/Kits/Icons/kit_Specops.tga"));
  // The pistol also has a weaponIcon; the row takes the item whose index is 3.
  CHECK_EQ(row.weaponIcon, std::string("Ingame/Weapons/Icons/Hud/USRIF_M4.tga"));
  // The unlock of the highest level, not the first one found.
  CHECK_EQ(row.altWeaponIcon, std::string("Ingame/Weapons/Icons/Hud/usrif_fnscarl_mini.tga"));
  CHECK(row.unlock);
  CHECK_EQ(row.abilityIcons.size(), std::size_t(2));
  CHECK_EQ(row.abilityIcons[0],
           std::string("Ingame/Weapons/Icons/Hud/SpecialKitIcons/handgrenade.tga"));
  CHECK_EQ(row.abilityIcons[1], std::string("Ingame/Weapons/Icons/Hud/SpecialKitIcons/c4.tga"));
  CHECK(row.sprintAbility > 0.79f && row.sprintAbility < 0.81f);
}

// US_Assault: the kit's own ability icon comes before the items', and of the two
// M203 templates the row shows the rifle's picture, not the launcher's. Both are
// what the dump of the original shows.
static void testAssaultOwnAbilityIconIsFirst() {
  MemoryFiles files;
  files.files["kits.con"] =
      "ObjectTemplate.create Kit US_Assault\n"
      "ObjectTemplate.addTemplate USRIF_M203\n"
      "ObjectTemplate.addTemplate USRGL_M203\n"
      "ObjectTemplate.addTemplate hgr_smoke\n"
      "ObjectTemplate.createComponent VehicleHud\n"
      "ObjectTemplate.vehicleHud.hudName \"HUD_TEXT_MENU_SPAWN_KIT_ASSAULT\"\n"
      "ObjectTemplate.vehicleHud.vehicleIcon \"Ingame\\Kits\\Icons\\kit_Light_Assault.tga\"\n"
      "ObjectTemplate.vehicleHud.abilityIcon "
      "\"Ingame\\Weapons\\Icons\\Hud\\SpecialKitIcons\\kevlarVest.tga\"\n"
      "ObjectTemplate.sprintStaminaDissipationFactor 0.6\n" +
      weapon("USRIF_M203", 3, "Ingame\\Weapons\\Icons\\Hud\\USRGL_M203G.tga", nullptr, nullptr) +
      weapon("USRGL_M203", 4, "Ingame\\Weapons\\Icons\\Hud\\USRGL_M203G.tga", nullptr, nullptr) +
      weapon("hgr_smoke", 5, "Ingame\\Weapons\\Icons\\Hud\\HGR_Smoke.tga", nullptr,
             "Ingame\\Weapons\\Icons\\Hud\\SpecialKitIcons\\smokegrenade.tga");

  const game::Registry registry = runInto(files, "kits.con");
  const hud::KitRow row = hud::buildKitRow(registry, "US_Assault");

  CHECK_EQ(row.weaponIcon, std::string("Ingame/Weapons/Icons/Hud/USRGL_M203G.tga"));
  CHECK_EQ(row.abilityIcons.size(), std::size_t(2));
  CHECK_EQ(row.abilityIcons[0],
           std::string("Ingame/Weapons/Icons/Hud/SpecialKitIcons/kevlarVest.tga"));
  CHECK_EQ(row.abilityIcons[1],
           std::string("Ingame/Weapons/Icons/Hud/SpecialKitIcons/smokegrenade.tga"));
  // The heavy kits dissipate stamina three times faster, and the bar says so.
  CHECK(row.sprintAbility > 0.39f && row.sprintAbility < 0.41f);
}

// No unlock, no factor: the row falls back to the picture the engine falls back
// to, and to a full bar. An unknown kit gives an empty row rather than a guess.
static void testDefaults() {
  MemoryFiles files;
  files.files["kits.con"] =
      "ObjectTemplate.create Kit Plain_Kit\n"
      "ObjectTemplate.createComponent VehicleHud\n"
      "ObjectTemplate.vehicleHud.hudName \"KEY\"\n";

  const game::Registry registry = runInto(files, "kits.con");
  const hud::KitRow row = hud::buildKitRow(registry, "Plain_Kit");
  CHECK_EQ(row.altWeaponIcon, std::string(hud::kEmptyIcon));
  CHECK(!row.unlock);
  CHECK(row.abilityIcons.empty());
  CHECK(row.sprintAbility > 0.99f);

  const hud::KitRow missing = hud::buildKitRow(registry, "No_Such_Kit");
  CHECK(missing.kitTemplate.empty());
  CHECK(missing.nameKey.empty());
}

TEST_MAIN({
  testSpecops();
  testAssaultOwnAbilityIconIsFirst();
  testDefaults();
})
