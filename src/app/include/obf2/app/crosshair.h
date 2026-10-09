#pragma once
// Our own soldier's crosshair: which weapon HUD group is up (`GuiIndex`) and where
// its four ticks stand, as `BF2.exe` 0x78b340 and 0x7a5690 write them, with the
// deviation grown and shrunk once per action we play (docs/functions/hud-crosshair.md).
#include <cstdint>
#include <map>
#include <string>

#include "obf2/game/object_template.h"
#include "obf2/game/weapon_hud.h"
#include "obf2/net/bf2_protocol.h"

namespace obf2::app {

// The button mask's bit for `c_PILie` (37): the mask counts from `c_PIFire` (8),
// the rule bf2_protocol.h gives with sprint's 13 → bit 5.
inline constexpr std::uint32_t kButtonLie = 1u << (37 - 8);

// The weapon the soldier has out: the kit's child whose `itemIndex` is the one
// his state's 0x1000 carries (the same rule as soldier_model.cpp). Null when the
// kit or the index is not known, or no child has it — and then nothing is
// guessed: the engine too writes `GuiIndex` 0 with no item (0x78b47a).
const game::ObjectTemplate* weaponOut(const game::Registry& registry, const std::string& kit,
                                      int itemIndex);

// The item a soldier holds once he has picked his kit up: `Soldier::pickupKit`
// (Linux 0x54dacf..0x54db3d), when nothing is active yet (+0x358 == -1), takes the
// first occupied inventory slot from 3 upwards, and failing that the first of
// 0..2. The server never sends our own soldier's state with his weapon in it, so
// this is how our client knows what is out after a spawn. -1 for an unknown kit.
int spawnItemIndex(const game::Registry& registry, const std::string& kit);

class Crosshair {
 public:
  // The weapon changed (or went away): its HUD and deviation templates. A new
  // weapon is a new `SoldierDeviationComp`, so its deviation starts from rest.
  void setWeapon(const game::ObjectTemplate* weapon);

  // One action played: `Soldier::handlePlayerInput` hands it to the weapon's
  // `updateDeviation` (`BF2.exe` 0x5af4ea → 0x60ad60). `pose` is the soldier's
  // (0 stand, 1 crouch, 2 lie); `inVehicle` clears the jump and lie flag the way
  // the soldier's +0x28 does (0x5ae2fe).
  void onAction(const net::bf2::PlayerAction& action, int pose, bool inVehicle);

  // `GuiIndex` and the four `Crosshair*Pos` into the HUD's numbers; with no
  // weapon `GuiIndex` is 0 and the ticks are left out.
  void apply(std::map<std::string, float>& values) const;

  const game::ObjectTemplate* weapon() const { return weapon_; }
  float deviation() const { return deviation_.deviation(); }

 private:
  const game::ObjectTemplate* weapon_ = nullptr;
  game::WeaponHudTemplate hud_;
  game::DeviationTemplate template_;
  game::SoldierDeviation deviation_;
};

}  // namespace obf2::app
