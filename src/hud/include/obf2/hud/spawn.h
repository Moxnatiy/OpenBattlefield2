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
std::string controlPointIcon(std::string_view teamName);


}  // namespace obf2::hud
