#pragma once
// The spawn screen's kit column, filled from the game's own kit templates.
//
// The seven rows are not a list of ours. The level names them:
//
//   gameLogic.setKit 2 0 "US_Specops" "us_light_soldier"
//
// and everything a row shows lives in that kit's `ObjectTemplate` and in the
// templates it carries. The engine copies it into the HUD's variables every
// frame: `HudInformationLayer` (`BF2.exe`, 0x468510) walks seven kit indices,
// asks the kit manager for (team, index), and writes the row's caption, its
// icons and its bar into the fields the `.con` reads by name
// (docs/functions/hud-kits.md).
//
// What each field of a row is has been checked against a frame dump of the
// original on Strike at Karkand — the pictures the game really put on screen,
// named through `Menu/Atlas/MemeAtlas.tai` (docs/research/spawn-screen-named.md).
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/game/object_template.h"

namespace obf2::hud {

// A row of the column, ready to be poured into the HUD's variables.
struct KitRow {
  std::string kitTemplate;   // "US_Specops" — the name the level gave
  std::string nameKey;       // KitName<N>String
  std::string icon;          // KitIcon<N>Path
  std::string weaponIcon;    // KitWeaponIcon<N>Path
  std::string altWeaponIcon; // KitAltWeaponIcon<N>Path
  // Kit<N>AbilityIcon<M>PathString, and the show flag of each is simply whether
  // this list reaches that index. The row draws them right to left: index 0 sits
  // at x 234, index 4 at x 162 (`HudElementsSpawn.con`).
  std::vector<std::string> abilityIcons;
  float sprintAbility = 1.0f;  // Kit<N>SprintAbility — the bar's value
  bool unlock = false;         // KitUnlock<N>Show — this kit has an unlock to show
};

// A row holds at most five ability icons: `Kit<N>AbilityIcon0..4` is where the
// data stops, and the engine's loop stops at the same five (0x468510, the
// `if (4 < local_c)` guards).
inline constexpr std::size_t kMaxAbilityIcons = 5;

// The kit's primary weapon is the item whose `ObjectTemplate.itemIndex` is 3.
// Measured: for all seven kits of the US side the picture the original puts in
// the row is the `weaponHud.weaponIcon` of exactly that item — the assault kit
// is the telling one, because it carries both `USRIF_M203` (3) and `USRGL_M203`
// (4) and the row shows the rifle's.
inline constexpr int kPrimaryItemIndex = 3;

// What `KitAltWeaponIcon<N>Path` holds when there is no unlock to show. The
// string is in the binary at the end of 0x468510, right before the branch that
// replaces it with the unlock's own picture.
inline constexpr std::string_view kEmptyIcon = "Ingame/GeneralIcons/empty.tga";

// Builds one row from a kit's template name. An unknown name gives an empty row
// rather than an invented one.
KitRow buildKitRow(const game::Registry& registry, std::string_view kitTemplate);

}  // namespace obf2::hud
