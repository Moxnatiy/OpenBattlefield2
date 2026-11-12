#pragma once
// What the bottom-right HUD says about the weapon's ammunition: the numbers
// `BF2.exe` 0x7a85f0 writes into the information layer each frame from the
// weapon's `AmmoComp`, and 0x7a5800 for the magazines (docs/functions/weapon-ammo.md).
#include <cstdint>
#include <map>
#include <string>

#include "obf2/game/weapon_ammo.h"

namespace obf2::app {

struct AmmoHud {
  std::map<std::string, float> values;
  std::map<std::string, std::string> strings;
  std::map<std::string, bool> flags;
};

// `reloadProgress` is the `PrimaryReloadTime` the last frame wrote (the layer's
// +0xb8): while it is strictly between 0 and 1 the ammunition block is skipped
// (0x7a8b2f..0x7a8b4c), so the count keeps the value it had when the reload
// began until the reload ends. A fresh layer starts at 1.
//
//   PrimaryAmmo           +0xb4  rounds ÷ magazine size          (0x7a8c3a)
//   PrimaryAmmoString     +0x144 the round count, "%i" (0x89e620)
//   PrimaryClips          +0x140 magazines left beside the one in the weapon;
//                                 the string, the three icons and their show
//                                 flags from 0x7a5800 (n>0, n>1, n>2, n>=0)
//   PrimaryReloadTime     +0xb8  1 − `getReloadTimeLeftInPercent`   (0x7a8d78)
//   PrimaryReloadTimeShow +0xbc  that value is below 1
//
//   PrimaryAmmoColor*     +0x30  `hudColor(PrimaryAmmo, 0.3)`        (0x7a8b62)
//
// Not taken: the blink flags, the endless magazines (`PrimaryEternalShow`, whose
// colour is `hudColor(1, 0.3)`, 0x7a8c6e), the icons' paths (`getHudIcons`).
AmmoHud ammoHud(const game::WeaponAmmo& ammo, std::uint32_t tick, float reloadProgress);

}  // namespace obf2::app
