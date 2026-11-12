#pragma once
// The bottom-left HUD: the player's health and stamina bars, and whether he is
// dead — what `BF2.exe` writes into the information layer each frame
// (docs/functions/hud-player.md).
//
//   PlayerHealth        +0x1a8  hit points ÷ max hit points           (0x78aa14..0x78aa2a)
//   PlayerHealthString  +0x270  ceil(that × 100), "%i" (0x89e620)     (0x78aa30..0x78aa82)
//   PlayerHealthColor*  +0x1f4  `hudColor(PlayerHealth, 0.3)`          (0x78ab10)
//   PlayerStamina       +0x1ac  the soldier's `getSprintStamina`       (0x78acb1..0x78acbf)
//   PlayerStaminaShow   +0x245  sprinting, or the stamina under 1      (0x78acc9..0x78acf1)
//   PlayerSprintColor*  +0x214  `hudColor(PlayerStamina, 0.05)` (B2BF's reading; the
//                                call follows 0x78ab10 in the same function)
//   KilledState         +0x253  not alive (player vfn +0x68), or man down (+0xfc)
//                                                                       (0x7af439..0x7af465)
//
// Not taken: the blinkers (`PlayerHealthBlink`, `PlayerStaminaBlink`, built by
// 0x7718a0 with 0.3, 0, 0.7, 0.15, 1 — their behaviour is not read), the injury and
// panting sounds, night vision, the commander's squad badge.
#include <array>
#include <optional>

#include "obf2/app/ammo_hud.h"

namespace obf2::app {

// The effect object's colour pick (vtable +0x8c, called with the layer's
// number and a threshold): the cream at 0x97ddd0 (0.8275, 0.8235, 0.702, 1) unless
// the number is under the threshold, then the red at 0x97dde0 (242, 47, 47 / 255, 1).
// An unordered compare picks the cream, as `!(value < threshold)` does.
std::array<float, 4> hudColor(float value, float threshold);

struct PlayerHudInput {
  std::optional<float> health;  // 0..1; none until a state of our soldier has said
  float stamina = 1.0f;
  bool sprinting = false;
  bool alive = true;
  bool manDown = false;
};

AmmoHud playerHud(const PlayerHudInput& input);

}  // namespace obf2::app
