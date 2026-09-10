#include "obf2/hud/kit_list.h"

#include <algorithm>

namespace obf2::hud {
namespace {

// The game's data writes asset paths with backslashes; everything else here
// spells them with forward slashes. The case is left alone: the file is found
// through `normalizeAssetPath` anyway, and the name is easier to compare with
// the original's when it still reads the way the data wrote it.
std::string slashes(std::string_view raw) {
  std::string out(raw);
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  return out;
}

// A component's property, or an empty view. Component names in the data are
// spelled `VehicleHud`, `WeaponHud`; `ObjectTemplate::component` matches them
// without regard to case.
std::string componentText(const game::ObjectTemplate& object, std::string_view component,
                          std::string_view property) {
  const game::Component* found = object.component(component);
  if (found == nullptr) return {};
  const auto at = found->properties.find(std::string(property));
  if (at == found->properties.end() || at->second.empty()) return {};
  return slashes(at->second.back().value());
}

}  // namespace

KitRow buildKitRow(const game::Registry& registry, std::string_view kitTemplate) {
  KitRow row;
  const game::ObjectTemplate* kit = registry.find(kitTemplate);
  if (kit == nullptr) return row;
  row.kitTemplate = kit->name;

  // The caption and the little icon at the row's left come off the kit itself.
  //   ObjectTemplate.vehicleHud.hudName    "HUD_TEXT_MENU_SPAWN_KIT_SPECIALFORCES"
  //   ObjectTemplate.vehicleHud.vehicleIcon "Ingame\Kits\Icons\kit_Specops.tga"
  row.nameKey = componentText(*kit, "VehicleHud", "hudname");
  row.icon = componentText(*kit, "VehicleHud", "vehicleicon");

  // The bar. The engine writes `1 - <a float of the kit>` into the variable
  // (0x468510, the store into the row's float field); the only per-kit float in
  // the data that behaves that way is `sprintStaminaDissipationFactor`, and the
  // dump agrees: the kits that set it to 0.2 draw a full bar and those that set
  // it to 0.6 draw two thirds of one. That the field the binary reads is this
  // property has **not** been shown in the binary — the tie is the measurement.
  if (const auto factor = kit->number("sprintstaminadissipationfactor")) {
    row.sprintAbility = 1.0f - *factor;
  }

  // The kit's own ability icon comes first: on the assault kit the dump puts
  // `kevlarVest.tga` at x 234 — icon 0 — and the smoke grenade, which is an item,
  // beside it at 216. The engine's order is the same: the loop over the kit's own
  // icons runs before the loop over its items.
  const std::string own = componentText(*kit, "VehicleHud", "abilityicon");
  if (!own.empty()) row.abilityIcons.push_back(own);

  // The unlock. A kit carries its unlocks as `ItemContainer` children with an
  // `unlockLevel`; the picture the row shows is the `weaponHud.altWeaponIcon` of
  // the weapon inside the container of the highest level. Measured on all seven
  // US kits: the row shows the level 2 weapon (SCAR-L, L96A1, FN2000, MG36, MP7,
  // G36E, P90) and not the level 1 one. Which of the two rules that is — the
  // highest level, or the level the player has selected with
  // `spawnManager.selectNextUnlock` — the dump cannot say: every kit in the game
  // has exactly the levels 1 and 2, and the profile the dump was taken with had
  // neither unlocked (the row draws its padlock).
  int bestLevel = 0;
  for (const game::ChildTemplate& child : kit->children) {
    const game::ObjectTemplate* item = registry.find(child.name);
    if (item == nullptr) continue;
    const auto level = item->number("unlocklevel");
    if (!level) continue;
    row.unlock = true;
    if (static_cast<int>(*level) < bestLevel) continue;
    for (const game::ChildTemplate& inner : item->children) {
      const game::ObjectTemplate* weapon = registry.find(inner.name);
      if (weapon == nullptr) continue;
      std::string icon = componentText(*weapon, "WeaponHud", "altweaponicon");
      if (icon.empty()) continue;
      bestLevel = static_cast<int>(*level);
      row.altWeaponIcon = std::move(icon);
      break;
    }
  }

  // The items: the primary weapon's big picture, and every item that has an
  // ability icon, in the order `addTemplate` gave them.
  for (const game::ChildTemplate& child : kit->children) {
    const game::ObjectTemplate* item = registry.find(child.name);
    if (item == nullptr) continue;
    const auto index = item->number("itemindex");
    if (index && static_cast<int>(*index) == kPrimaryItemIndex && row.weaponIcon.empty()) {
      row.weaponIcon = componentText(*item, "WeaponHud", "weaponicon");
    }
    if (row.abilityIcons.size() >= kMaxAbilityIcons) continue;
    std::string ability = componentText(*item, "WeaponHud", "specialabilityicon");
    if (!ability.empty()) row.abilityIcons.push_back(std::move(ability));
  }

  if (row.altWeaponIcon.empty()) row.altWeaponIcon = std::string(kEmptyIcon);
  return row;
}

}  // namespace obf2::hud
