#include "obf2/game/weapon_hud.h"

#include <algorithm>
#include <cmath>

namespace obf2::game {
namespace {

std::optional<float> componentFloat(const ObjectTemplate& weapon, std::string_view component,
                                    std::string_view property, std::size_t argument = 0) {
  const Component* found = weapon.component(component);
  if (found == nullptr) return std::nullopt;
  const auto at = found->properties.find(std::string(property));
  if (at == found->properties.end() || at->second.empty()) return std::nullopt;
  return at->second.back().asFloat(argument);
}

void take(float& field, std::optional<float> value) {
  if (value) field = *value;
}

void takeInt(int& field, std::optional<float> value) {
  if (value) field = static_cast<int>(*value);
}

// One accumulator of 0x60ad60. `add` is what the input asked for this time,
// `fall` its decay, `ceiling` its cap. The three shapes the function has:
// nothing asked → fall to 0; already at the cap → fall but not below the cap;
// otherwise → grow (`addFalls`: the turn and misc terms subtract the fall
// even then) and stop at the cap.
float accumulate(float value, float add, float fall, float ceiling, bool addFalls) {
  if (add == 0.0f) {
    value -= fall;
    return value < 0.0f ? 0.0f : value;
  }
  if (ceiling <= value) {
    value -= fall;
    return value < ceiling ? ceiling : value;
  }
  value += addFalls ? add - fall : add;
  if (value > ceiling) return ceiling;
  return value < 0.0f ? 0.0f : value;
}

}  // namespace

WeaponHudTemplate weaponHudOf(const ObjectTemplate& weapon) {
  WeaponHudTemplate hud;
  takeInt(hud.guiIndex, componentFloat(weapon, "WeaponHud", "guiindex"));
  takeInt(hud.altGuiIndex, componentFloat(weapon, "WeaponHud", "altguiindex"));
  takeInt(hud.deviationFactor, componentFloat(weapon, "WeaponHud", "deviationfactor"));
  takeInt(hud.maxVisualDeviation, componentFloat(weapon, "WeaponHud", "maxvisualdeviation"));
  takeInt(hud.minVisualDeviation, componentFloat(weapon, "WeaponHud", "minvisualdeviation"));
  takeInt(hud.crosshairIconSize, componentFloat(weapon, "WeaponHud", "crosshairiconsize"));
  return hud;
}

DeviationTemplate deviationOf(const ObjectTemplate& weapon) {
  // The properties are `ObjectTemplate.deviation.*` whatever the component is
  // called (`SoldierDeviationComp`), so the registry files them under
  // "deviation". The argument order of the three setters is the server's
  // (`setFireDev` Linux 0x5ef230, `setTurnDev` 0x5f1060, `setSpeedDev`
  // 0x5f1080, `setMiscDev` 0x5f10a0).
  DeviationTemplate t;
  take(t.fireMax, componentFloat(weapon, "deviation", "setfiredev", 0));
  take(t.fireAdd, componentFloat(weapon, "deviation", "setfiredev", 1));
  take(t.fireFall, componentFloat(weapon, "deviation", "setfiredev", 2));
  take(t.minDev, componentFloat(weapon, "deviation", "mindev"));
  take(t.turnMax, componentFloat(weapon, "deviation", "setturndev", 0));
  take(t.turnPitch, componentFloat(weapon, "deviation", "setturndev", 1));
  take(t.turnYaw, componentFloat(weapon, "deviation", "setturndev", 2));
  take(t.turnFall, componentFloat(weapon, "deviation", "setturndev", 3));
  take(t.speedMax, componentFloat(weapon, "deviation", "setspeeddev", 0));
  take(t.speedForward, componentFloat(weapon, "deviation", "setspeeddev", 1));
  take(t.speedStrafe, componentFloat(weapon, "deviation", "setspeeddev", 2));
  take(t.speedFall, componentFloat(weapon, "deviation", "setspeeddev", 3));
  take(t.miscMax, componentFloat(weapon, "deviation", "setmiscdev", 0));
  take(t.miscAdd, componentFloat(weapon, "deviation", "setmiscdev", 1));
  take(t.miscFall, componentFloat(weapon, "deviation", "setmiscdev", 2));
  take(t.modStand, componentFloat(weapon, "deviation", "devmodstand"));
  take(t.modCrouch, componentFloat(weapon, "deviation", "devmodcrouch"));
  take(t.modLie, componentFloat(weapon, "deviation", "devmodlie"));
  take(t.modZoom, componentFloat(weapon, "deviation", "devmodzoom"));
  return t;
}

void SoldierDeviation::update(const DeviationTemplate& t, const DeviationInput& input) {
  // 0x60ad60 compares each axis against 0.01 (`ABS(x) > 0.01`).
  constexpr float kStill = 0.01f;
  const auto moving = [](float axis) { return std::fabs(axis) > kStill; };

  // Speed: a fixed amount per axis held, not the axis's size.
  float speedAdd = 0.0f;
  if (moving(input.throttle)) speedAdd = t.speedForward;
  if (moving(input.strafe)) speedAdd += t.speedStrafe;
  speed_ = accumulate(speed_, speedAdd, t.speedFall, t.speedMax, false);

  // Turn: the mouse's size, Y first.
  float turnAdd = 0.0f;
  if (moving(input.mouseY)) turnAdd = std::fabs(input.mouseY) * t.turnPitch;
  if (moving(input.mouseX)) turnAdd += std::fabs(input.mouseX) * t.turnYaw;
  turn_ = accumulate(turn_, turnAdd, t.turnFall, t.turnMax, true);

  // Misc: the jump or the lie-down, by the template's own amount.
  const float miscAdd = input.jumpOrLie ? t.miscAdd : 0.0f;
  misc_ = accumulate(misc_, miscAdd, t.miscFall, t.miscMax, true);

  // The pose's mod; the pose component's +0x38, anything else is stand.
  float pose = t.modStand;
  if (input.pose == 1) pose = t.modCrouch;
  if (input.pose == 2) pose = t.modLie;
  const float zoom = input.zoomed ? t.modZoom : 1.0f;

  // The shot term falls unless the weapon is firing (0x60b0fd).
  if (!input.firing) {
    shot_ -= t.fireFall;
    if (shot_ < 0.0f) shot_ = 0.0f;
  }

  deviation_ = (t.minDev + misc_ + speed_ + turn_ + shot_) * zoom * pose;
}

CrosshairTicks crosshairTicks(const WeaponHudTemplate& hud, float deviation) {
  // 0x7a5690: the spread in HUD units, `ceil` (the msvcrt import at 0x87f5f4)
  // then truncated; the minimum plus it, capped at the maximum.
  const int spread =
      static_cast<int>(std::ceil(static_cast<double>(deviation) * hud.deviationFactor));
  const int offset = std::min(hud.minVisualDeviation + spread, hud.maxVisualDeviation);
  // The HUD's own view is 800×600 (0x7ad450), so its centre is ceil(800·0.5),
  // ceil(600·0.5); the tick is 8 (the object's +0xf8, constructor 0x7a5c66).
  constexpr int kCentreX = 400;
  constexpr int kCentreY = 300;
  constexpr int kTick = 8;
  CrosshairTicks ticks;
  ticks.up = kCentreY - kTick - offset;
  ticks.down = kCentreY + offset + 1;
  ticks.left = kCentreX - kTick - offset - 1;
  ticks.right = kCentreX + offset;
  return ticks;
}

}  // namespace obf2::game
