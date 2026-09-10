#include "obf2/hud/spawn.h"

namespace obf2::hud {

std::string armyLabelKey(std::string_view teamName) {
  if (teamName.empty()) return {};
  if (teamName == "MEC") return "HUD_TEXT_MENU_SPAWN_ARMY_MEC";
  if (teamName == "US") return "HUD_TEXT_MENU_SPAWN_ARMY_USMC";
  if (teamName == "CH") return "HUD_TEXT_MENU_SPAWN_ARMY_CHINA";
  // The general branch. EU goes through it: the game has no special case for it,
  // and the key is assembled from a prefix.
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

}  // namespace obf2::hud
