#pragma once
// A `KilledByEvent` turned into what the HUD's kill feed needs: the players by
// name and side, and the weapon's HUD name — what `BF2HudManager::
// handleGameLogicEvent` case 3 resolves from the player manager and the
// template manager before it makes the line (OpenBF2 BF2HudManagerGameLogicEvents
// .cpp; BF2.exe 0x753de0). The line itself is `hud::Manager::killedBy`.
//
// The weapon's name, in the original's order:
//   * the vehicle template (the killer's soldier on foot) and the weapon template
//     are looked up by number;
//   * when both are there and the vehicle is a soldier, a ladder or a parachute,
//     the weapon's `weaponHud.hudName` — and a weapon without a weapon HUD marks
//     the kill "no weapon" (the line then reads "killed", not "kills with");
//   * otherwise the vehicle's `vehicleHud.hudName`, else the weapon's;
//   * with no vehicle template the original asks the killer's current vehicle —
//     not ours (we do not keep which vehicle a remote player is in by template).
//
// The template numbers are ours (template_numbers.h), right for the mod's own
// templates — every weapon and vehicle — and not for the level's.
#include <cstdint>
#include <map>
#include <string>

#include "obf2/game/object_template.h"
#include "obf2/game/template_numbers.h"
#include "obf2/hud/manager.h"
#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_world.h"

namespace obf2::app {

struct WeaponName {
  std::string key;  // a localisation key, empty when none was found
  bool missingWeapon = false;
};

WeaponName killWeaponName(const net::bf2::Event::KilledBy& kill,
                          const game::TemplateNumbers& numbers, const game::Registry& registry);

// The whole kill for the HUD; `ownPlayer` is the local player's id.
hud::Manager::Kill killForHud(const net::bf2::Event::KilledBy& kill,
                              const std::map<std::uint32_t, net::bf2::RemotePlayer>& players,
                              int ownPlayer, const game::TemplateNumbers& numbers,
                              const game::Registry& registry);
hud::Manager::Local localForHud(const std::map<std::uint32_t, net::bf2::RemotePlayer>& players,
                                int ownPlayer, int ownTeam);

}  // namespace obf2::app
