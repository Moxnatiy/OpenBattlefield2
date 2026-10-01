#pragma once
// The spawn screen: what the game assembles from a side's name.
//
// The name comes from the level itself — `gameLogic.setTeamName 1 "CH"` in
// `Init.con`. It is not merely a caption: the engine substitutes it into path
// templates and translates it into a localisation key. Across all 22 levels the
// set of names is exactly **CH, EU, MEC, US**, and the icon directories in `Menu_client.zip` match.
//
// It is precisely these conversions that are gathered here, because we have got
// them wrong twice already: a baked-in guess "1 is US, 2 is Ch" flipped the flags
// on the map — once on the big map and a second time on the minimap, because the
// same logic lived in two places.
#include <string>
#include <string_view>

namespace obf2::hud {

// The localisation key for a team tab's caption.
//
// Reversed from BF2.exe 0x787110: three cases are written out separately, the
// rest are assembled from a prefix. An empty name gives an empty key — the same
// as in the game (there it is the branch with an empty string).
std::string armyLabelKey(std::string_view teamName);

// A side's flag — on the spawn screen's tab and on the scoreboard.
// The template `Ingame/Flags/Icons/Hud/Score/%s/scoreBoard_Flag.tga` lies in the
// binary at 0x931030, and 0x787260 fills it in.
std::string teamFlagIcon(std::string_view teamName);

// A capture point's icon on the map.
// The template `Ingame/Flags/Icons/Minimap/%s/miniMap_CP.tga` at 0x925af8, filled
// in by 0x74fb70. For the neutral side there is a separate ready-made string with
// `Neutral` (0x925b28) — so an empty name gives exactly that.
//
// A side's main base gets `miniMap_CPBase.tga` instead, and the original's spawn
// screen on Strike at Karkand draws exactly one of them against four ordinary
// points. Which point is a base is **not read out of the binary**: what we go by
// is the level's own `CombatArea`-side flag `unableToChangeTeam`, and on that
// level it picks out the one point the dump shows a base icon for.
std::string controlPointIcon(std::string_view teamName, bool isBase = false);

// The bar across the top of the spawn screen (`SpawnInfoString`, the layer's
// +0x510) while the player is dead — `HudInformationLayer`'s per-frame update,
// BF2.exe 0x4668d0, from 0x4672b0:
//
//   no group chosen on the server (`getSpawnGroup` < 1):
//     the spawn screen is up (states 1, 0xd, 0x11, 0x12) and a circle is chosen
//                                         -> HUD_CENTERINFOBOX_presstospawn
//     otherwise                           -> HUD_CENTERINFOBOX_selectspawnpoint
//   a group chosen, and it is the circle's:
//     `getTimeToSpawn` > 0                -> HUD_CENTERINFOBOX_timetospawn, #TIME#
//                                            the time rounded up (0x463930)
//     otherwise                           -> HUD_CENTERINFOBOX_instantspawn
//   a group chosen that is not the circle's -> HUD_CENTERINFOBOX_invalidspawnpoint
struct SpawnInfo {
  int serverGroup = 0;      // `player->getSpawnGroup()`, from his state (0x2)
  int chosenGroup = 0;      // the circle chosen on the screen; 0 for none
  bool screenUp = false;    // the HUD state is 1, 0xd, 0x11 or 0x12
  float timeToSpawn = 0.0f; // seconds, `getTimeToSpawn`
};
// The key, and for `timetospawn` the number to put for #TIME#.
struct SpawnInfoText {
  std::string key;
  int time = -1;
};
SpawnInfoText spawnInfoText(const SpawnInfo& info);


}  // namespace obf2::hud
