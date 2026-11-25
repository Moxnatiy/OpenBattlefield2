#include "obf2/app/player_hud.h"

#include <cmath>
#include <cstdio>

namespace obf2::app {

std::array<float, 4> hudColor(float value, float threshold) {
  // `BF2.exe` .data 0x97ddd0 and 0x97dde0, four floats each.
  constexpr std::array<float, 4> kCream{0.8275f, 0.8235f, 0.702f, 1.0f};
  constexpr std::array<float, 4> kRed{242.0f / 255.0f, 47.0f / 255.0f, 47.0f / 255.0f, 1.0f};
  return !(value < threshold) ? kCream : kRed;
}

namespace {

void putColor(AmmoHud& out, const char* prefix, const std::array<float, 4>& colour) {
  static const char* const kParts[4] = {"Red", "Green", "Blue", "Alpha"};
  for (int i = 0; i < 4; ++i) {
    out.values[std::string(prefix) + kParts[i]] = colour[static_cast<std::size_t>(i)];
  }
}

}  // namespace

AmmoHud playerHud(const PlayerHudInput& input) {
  AmmoHud out;
  if (input.health) {
    const float health = *input.health;
    out.values["PlayerHealth"] = health;
    char text[16];
    std::snprintf(text, sizeof text, "%i",
                  static_cast<int>(std::ceil(static_cast<double>(health) * 100.0)));
    out.strings["PlayerHealthString"] = text;
    putColor(out, "PlayerHealthColor", hudColor(health, 0.3f));
  }
  out.values["PlayerStamina"] = input.stamina;
  out.flags["PlayerStaminaShow"] = input.sprinting || input.stamina < 1.0f;
  putColor(out, "PlayerSprintColor", hudColor(input.stamina, 0.05f));
  out.flags["KilledState"] = !input.alive || input.manDown;
  // 0x464870: `TimeInfoShow` (+0x1a9) is the time left above 0; `LevelTime` is
  // written only then.
  if (input.nextStateChangeTick) {
    const float left = timeToNextStateChange(*input.nextStateChangeTick, input.tick);
    out.flags["TimeInfoShow"] = left > 0.0f;
    if (left > 0.0f) out.strings["LevelTime"] = levelTimeText(left);
  }
  return out;
}

float timeToNextStateChange(std::uint32_t nextStateChangeTick, std::uint32_t tick) {
  return tick < nextStateChangeTick ? static_cast<float>(nextStateChangeTick - tick) / 30.0f : 0.0f;
}

std::string levelTimeText(float seconds) {
  const int total = static_cast<int>(std::nearbyint(seconds));  // ROUND, 0x46489a
  const int hours = total / 3600;
  const int minutes = (total - hours * 3600) / 60;
  const int rest = total - (hours * 60 + minutes) * 60;
  char text[48];
  char hourText[16] = "";
  char minuteText[16] = "";
  if (hours >= 1) std::snprintf(hourText, sizeof hourText, "%d:", hours);
  if (hours >= 1 || minutes >= 1) std::snprintf(minuteText, sizeof minuteText, "%02d:", minutes);
  std::snprintf(text, sizeof text, "%s%s%02d", hourText, minuteText, rest);
  return text;
}

}  // namespace obf2::app
