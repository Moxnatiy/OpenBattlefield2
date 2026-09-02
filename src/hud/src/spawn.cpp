#include "obf2/hud/spawn.h"

namespace obf2::hud {

std::string armyLabelKey(std::string_view teamName) {
  if (teamName.empty()) return {};
  if (teamName == "MEC") return "HUD_TEXT_MENU_SPAWN_ARMY_MEC";
  if (teamName == "US") return "HUD_TEXT_MENU_SPAWN_ARMY_USMC";
  if (teamName == "CH") return "HUD_TEXT_MENU_SPAWN_ARMY_CHINA";
  // Загальна гілка. Саме нею проходить EU: окремого випадку для нього в
  // грі немає, і ключ складається з префікса.
  return "HUD_TEXT_MENU_SPAWN_ARMY_" + std::string(teamName);
}

std::string teamFlagIcon(std::string_view teamName) {
  if (teamName.empty()) return {};
  return "Ingame/Flags/Icons/Hud/Score/" + std::string(teamName) + "/scoreBoard_Flag.tga";
}

std::string controlPointIcon(std::string_view teamName) {
  const std::string faction = teamName.empty() ? "Neutral" : std::string(teamName);
  return "Ingame/Flags/Icons/Minimap/" + faction + "/miniMap_CP.tga";
}

const std::vector<KitSlot>& spawnKits() {
  static const std::vector<KitSlot> kits = {
      {"HUD_TEXT_MENU_SPAWN_KIT_SPECIALFORCES", "Ingame/Kits/Icons/kit_Specops.tga",
       "USRIF_M4.tga"},
      {"HUD_TEXT_MENU_SPAWN_KIT_SNIPER", "Ingame/Kits/Icons/kit_Sniper.tga", "USRIF_M24.tga"},
      {"HUD_TEXT_MENU_SPAWN_KIT_ASSAULT", "Ingame/Kits/Icons/kit_Light_Assault.tga",
       "USRIF_M203.tga"},
      {"HUD_TEXT_MENU_SPAWN_KIT_SUPPORT", "Ingame/Kits/Icons/kit_Heavy_Assault.tga",
       "USLMG_M249SAW.tga"},
      {"HUD_TEXT_MENU_SPAWN_KIT_ENGINEER", "Ingame/Kits/Icons/kit_Engineer.tga",
       "USRIF_Remington11-87.tga"},
      {"HUD_TEXT_MENU_SPAWN_KIT_MEDIC", "Ingame/Kits/Icons/kit_Medic.tga", "USRIF_M16a2.tga"},
      {"HUD_TEXT_MENU_SPAWN_KIT_ANTITANK", "Ingame/Kits/Icons/kit_ATAA.tga", "USRIF_MP5_A3.tga"},
  };
  return kits;
}

}  // namespace obf2::hud
