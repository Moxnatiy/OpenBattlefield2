// The spawn screen: what the game assembles from a side's name.
//
// Here we guard against a mistake we have already made twice: the baked-in guess
// "team 1 is US, team 2 is Ch". In fact the side is named by the level itself
// (`gameLogic.setTeamName`), and for Dalian_plant that is CH and US — in exactly
// that order.
#include "check.h"
#include "obf2/hud/spawn.h"

using namespace obf2;

// Three cases are written out separately in the game (BF2.exe 0x787110), the rest
// are assembled from a prefix.
static void testArmyLabelKeys() {
  CHECK_EQ(hud::armyLabelKey("CH"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_CHINA"));
  CHECK_EQ(hud::armyLabelKey("US"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_USMC"));
  CHECK_EQ(hud::armyLabelKey("MEC"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_MEC"));
  // EU has no special case and goes through the general branch.
  CHECK_EQ(hud::armyLabelKey("EU"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_EU"));
  // No name means no key either.
  CHECK(hud::armyLabelKey("").empty());
}

// The flag's path is the template 0x931030 with the side's name in place of %s.
static void testTeamFlagIcon() {
  CHECK_EQ(hud::teamFlagIcon("CH"),
           std::string("Ingame/Flags/Icons/Hud/Score/CH/scoreBoard_Flag.tga"));
  CHECK_EQ(hud::teamFlagIcon("MEC"),
           std::string("Ingame/Flags/Icons/Hud/Score/MEC/scoreBoard_Flag.tga"));
  CHECK(hud::teamFlagIcon("").empty());
}

// A capture point's icon is the template 0x925af8. The neutral side has its own
// ready string with Neutral (0x925b28), so an empty name gives exactly that.
static void testControlPointIcon() {
  CHECK_EQ(hud::controlPointIcon("US"),
           std::string("Ingame/Flags/Icons/Minimap/US/miniMap_CP.tga"));
  CHECK_EQ(hud::controlPointIcon("CH"),
           std::string("Ingame/Flags/Icons/Minimap/CH/miniMap_CP.tga"));
  CHECK_EQ(hud::controlPointIcon(""),
           std::string("Ingame/Flags/Icons/Minimap/Neutral/miniMap_CP.tga"));
}

// Dalian_plant: team one is Chinese, team two American. Had we baked in the
// opposite again, this check would have caught it.
static void testDalianOrder() {
  const char* teamNames[3] = {"", "CH", "US"};
  CHECK_EQ(hud::controlPointIcon(teamNames[1]),
           std::string("Ingame/Flags/Icons/Minimap/CH/miniMap_CP.tga"));
  CHECK_EQ(hud::controlPointIcon(teamNames[2]),
           std::string("Ingame/Flags/Icons/Minimap/US/miniMap_CP.tga"));
  CHECK_EQ(hud::armyLabelKey(teamNames[1]), std::string("HUD_TEXT_MENU_SPAWN_ARMY_CHINA"));
}

// Seven kits in the screen's order, each with all three fields.
static void testKitList() {
  CHECK_EQ(hud::spawnKits().size(), std::size_t(7));
  CHECK_EQ(std::string(hud::spawnKits().front().nameKey),
           std::string("HUD_TEXT_MENU_SPAWN_KIT_SPECIALFORCES"));
  CHECK_EQ(std::string(hud::spawnKits().back().nameKey),
           std::string("HUD_TEXT_MENU_SPAWN_KIT_ANTITANK"));
  for (const hud::KitSlot& kit : hud::spawnKits()) {
    CHECK(kit.nameKey != nullptr && *kit.nameKey != '\0');
    CHECK(kit.icon != nullptr && *kit.icon != '\0');
    CHECK(kit.weapon != nullptr && *kit.weapon != '\0');
  }
}

TEST_MAIN({
  testArmyLabelKeys();
  testTeamFlagIcon();
  testControlPointIcon();
  testDalianOrder();
  testKitList();
})
