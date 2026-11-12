#include "obf2/game/weapon_shift.h"

namespace obf2::game {

ShiftTemplate shiftOf(const ObjectTemplate& weapon) {
  ShiftTemplate out;
  // `ObjectTemplate.animation.*`, filed under "animation".
  const Component* animation = weapon.component("animation");
  if (animation == nullptr) return out;
  if (const auto found = animation->properties.find("useshiftanimation");
      found != animation->properties.end() && !found->second.empty()) {
    if (const auto value = found->second.back().asFloat(0)) out.useShiftAnimation = *value != 0.0f;
  }
  if (const auto found = animation->properties.find("shiftdelay");
      found != animation->properties.end() && !found->second.empty()) {
    if (const auto value = found->second.back().asFloat(0)) out.shiftDelay = *value;
  }
  return out;
}

void WeaponShift::setWeapon(const ShiftTemplate& weapon) {
  *this = WeaponShift{};
  template_ = weapon;
}

void WeaponShift::onRoundComplete(int roundsLeft) {
  if (roundsLeft > 0 && template_.useShiftAnimation) pending_ = true;
}

bool WeaponShift::update(std::uint32_t tick, bool weaponBusy) {
  bool raised = false;
  if (pending_ && !weaponBusy) {
    pending_ = false;
    startedAt_ = tick;
    ++shifts_;
    raised = true;
  }
  if (startedAt_ != 0 &&
      tick >= startedAt_ + static_cast<std::uint32_t>(template_.shiftDelayTicks())) {
    startedAt_ = 0;
  }
  return raised;
}

}  // namespace obf2::game
