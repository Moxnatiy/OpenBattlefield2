// Екран появи: те, що гра складає з назви сторони.
//
// Тут стережемо помилку, яку ми вже робили двічі: зашитий здогад
// «команда 1 це US, команда 2 це Ch». Насправді сторону називає сам
// рівень (`gameLogic.setTeamName`), і для Dalian_plant це CH та US —
// саме в такому порядку.
#include "check.h"
#include "obf2/hud/spawn.h"

using namespace obf2;

// Три випадки записані в грі окремо (BF2.exe 0x787110), решта
// складається з префікса.
static void testArmyLabelKeys() {
  CHECK_EQ(hud::armyLabelKey("CH"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_CHINA"));
  CHECK_EQ(hud::armyLabelKey("US"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_USMC"));
  CHECK_EQ(hud::armyLabelKey("MEC"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_MEC"));
  // EU окремого випадку не має і йде загальною гілкою.
  CHECK_EQ(hud::armyLabelKey("EU"), std::string("HUD_TEXT_MENU_SPAWN_ARMY_EU"));
  // Немає назви — немає й ключа.
  CHECK(hud::armyLabelKey("").empty());
}

// Шлях прапорця — шаблон 0x931030 із назвою сторони замість %s.
static void testTeamFlagIcon() {
  CHECK_EQ(hud::teamFlagIcon("CH"),
           std::string("Ingame/Flags/Icons/Hud/Score/CH/scoreBoard_Flag.tga"));
  CHECK_EQ(hud::teamFlagIcon("MEC"),
           std::string("Ingame/Flags/Icons/Hud/Score/MEC/scoreBoard_Flag.tga"));
  CHECK(hud::teamFlagIcon("").empty());
}

// Значок точки захоплення — шаблон 0x925af8. Нічийна сторона має власний
// готовий рядок із Neutral (0x925b28), тож порожня назва дає саме його.
static void testControlPointIcon() {
  CHECK_EQ(hud::controlPointIcon("US"),
           std::string("Ingame/Flags/Icons/Minimap/US/miniMap_CP.tga"));
  CHECK_EQ(hud::controlPointIcon("CH"),
           std::string("Ingame/Flags/Icons/Minimap/CH/miniMap_CP.tga"));
  CHECK_EQ(hud::controlPointIcon(""),
           std::string("Ingame/Flags/Icons/Minimap/Neutral/miniMap_CP.tga"));
}

// Dalian_plant: перша команда китайська, друга американська. Якби ми
// знову зашили зворотне, ця перевірка це впіймала б.
static void testDalianOrder() {
  const char* teamNames[3] = {"", "CH", "US"};
  CHECK_EQ(hud::controlPointIcon(teamNames[1]),
           std::string("Ingame/Flags/Icons/Minimap/CH/miniMap_CP.tga"));
  CHECK_EQ(hud::controlPointIcon(teamNames[2]),
           std::string("Ingame/Flags/Icons/Minimap/US/miniMap_CP.tga"));
  CHECK_EQ(hud::armyLabelKey(teamNames[1]), std::string("HUD_TEXT_MENU_SPAWN_ARMY_CHINA"));
}

// Сім наборів у порядку екрана, у кожного є всі три поля.
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
