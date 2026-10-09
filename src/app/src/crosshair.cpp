#include "obf2/app/crosshair.h"

namespace obf2::app {

const game::ObjectTemplate* weaponOut(const game::Registry& registry, const std::string& kit,
                                      int itemIndex) {
  if (kit.empty() || itemIndex < 0) return nullptr;
  const game::ObjectTemplate* kitTemplate = registry.find(kit);
  if (kitTemplate == nullptr) return nullptr;
  for (const game::ChildTemplate& child : kitTemplate->children) {
    const game::ObjectTemplate* item = registry.find(child.name);
    if (item != nullptr && item->number("itemindex") == static_cast<float>(itemIndex)) {
      return item;
    }
  }
  return nullptr;
}

int spawnItemIndex(const game::Registry& registry, const std::string& kit) {
  if (kit.empty()) return -1;
  const game::ObjectTemplate* kitTemplate = registry.find(kit);
  if (kitTemplate == nullptr) return -1;
  // The occupied slots: each child's `itemIndex`.
  int fromThree = -1;
  int belowThree = -1;
  for (const game::ChildTemplate& child : kitTemplate->children) {
    const game::ObjectTemplate* item = registry.find(child.name);
    if (item == nullptr) continue;
    const auto index = item->number("itemindex");
    if (!index) continue;
    const int slot = static_cast<int>(*index);
    if (slot >= 3) {
      if (fromThree < 0 || slot < fromThree) fromThree = slot;
    } else if (slot >= 0) {
      if (belowThree < 0 || slot < belowThree) belowThree = slot;
    }
  }
  return fromThree >= 0 ? fromThree : belowThree;
}

void Crosshair::setWeapon(const game::ObjectTemplate* weapon) {
  if (weapon == weapon_) return;
  weapon_ = weapon;
  deviation_ = game::SoldierDeviation{};
  if (weapon_ == nullptr) return;
  hud_ = game::weaponHudOf(*weapon_);
  template_ = game::deviationOf(*weapon_);
}

void Crosshair::onAction(const net::bf2::PlayerAction& action, int pose, bool inVehicle) {
  if (weapon_ == nullptr) return;
  game::DeviationInput input;
  // The axes as the engine plays them: the wire value × 0.01 (0x5bc6a0).
  input.strafe = net::bf2::axisFromWire(action.axes[net::bf2::kAxisYaw]);
  input.throttle = net::bf2::axisFromWire(action.axes[net::bf2::kAxisThrottle]);
  input.mouseX = net::bf2::axisFromWire(action.axes[net::bf2::kAxisMouseX]);
  input.mouseY = net::bf2::axisFromWire(action.axes[net::bf2::kAxisMouseY]);
  // The jump is the soldier's after his own vetoes (the jump timer +0x3dc, his
  // +0x2d4, a prone pose — 0x5ae2a3..0x5ae2b3); those are not modelled, so a
  // pressed jump counts. Lying down counts as pressed.
  const bool jump = (action.buttons & net::bf2::kButtonAction) != 0;
  const bool lie = (action.buttons & kButtonLie) != 0;
  input.jumpOrLie = !inVehicle && (jump || lie);
  // We do not fire: `getFireState` is 0 and the shot term only falls.
  input.firing = false;
  input.pose = pose;
  // Zoom is not modelled: `setZoom` is never called.
  input.zoomed = false;
  deviation_.update(template_, input);
}

void Crosshair::apply(std::map<std::string, float>& values) const {
  if (weapon_ == nullptr) {
    values["GuiIndex"] = 0.0f;
    values.erase("CrosshairUpPos");
    values.erase("CrosshairDownPos");
    values.erase("CrosshairLeftPos");
    values.erase("CrosshairRightPos");
    return;
  }
  // 0x78b340: on foot the weapon's `getGuiIndex(flag)`; the flag's other answer
  // (0 when the owner's vfn +0x258 says so) and `altGuiIndex` are not modelled.
  values["GuiIndex"] = static_cast<float>(hud_.guiIndex);
  // Before the first action the deviation has never been updated; the engine's
  // component starts at 0 too (the constructor), so the ticks stand at the minimum.
  const game::CrosshairTicks ticks = game::crosshairTicks(hud_, deviation_.deviation());
  values["CrosshairUpPos"] = static_cast<float>(ticks.up);
  values["CrosshairDownPos"] = static_cast<float>(ticks.down);
  values["CrosshairLeftPos"] = static_cast<float>(ticks.left);
  values["CrosshairRightPos"] = static_cast<float>(ticks.right);
}

}  // namespace obf2::app
