#include "obf2/app/ammo_hud.h"

#include "obf2/app/player_hud.h"

namespace obf2::app {

AmmoHud ammoHud(const game::WeaponAmmo& ammo, std::uint32_t tick, float reloadProgress) {
  AmmoHud out;
  const int magSize = ammo.tmpl().magSize;
  const bool frozen = reloadProgress > 0.0f && reloadProgress < 1.0f;
  if (!frozen && magSize > 0) {
    const int rounds = ammo.roundsInMag();
    const float share = static_cast<float>(rounds) / static_cast<float>(magSize);
    out.values["PrimaryAmmo"] = share;
    // 0x7a8b4c..0x7a8b62: the colour picked by +0xb4 against 0.3, into +0x30..+0x3c.
    const auto colour = hudColor(share, 0.3f);
    out.values["PrimaryAmmoColorRed"] = colour[0];
    out.values["PrimaryAmmoColorGreen"] = colour[1];
    out.values["PrimaryAmmoColorBlue"] = colour[2];
    out.values["PrimaryAmmoColorAlpha"] = colour[3];
    out.strings["PrimaryAmmoString"] = std::to_string(rounds);
    // 0x7a5800 with `getMagVectorSize() - 1`.
    const int spare = ammo.spareMags();
    out.values["PrimaryClips"] = static_cast<float>(spare);
    out.flags["PrimaryClip1Show"] = spare > 0;
    out.flags["PrimaryClip2Show"] = spare > 1;
    out.flags["PrimaryClip3Show"] = spare > 2;
    out.flags["PrimaryClipStringShow"] = spare >= 0;
    if (spare >= 0) out.strings["PrimaryClipString"] = std::to_string(spare);
  }
  const float progress = 1.0f - ammo.reloadLeft(tick);
  out.values["PrimaryReloadTime"] = progress;
  out.flags["PrimaryReloadTimeShow"] = progress < 1.0f;
  return out;
}

}  // namespace obf2::app
