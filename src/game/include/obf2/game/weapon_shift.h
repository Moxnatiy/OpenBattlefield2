#pragma once
// The bolt-action cycle: after a round a weapon with `animation.useShiftAnimation`
// plays its `shift` clip (message 32) and refuses to fire again until the cycle
// ends (docs/functions/weapon-zoom.md, "The bolt action").
//
// The server's `DefaultAnimationComp` (Linux 0x60a710..0x60a8a0) and its template
// (0x60a8e0); the animation system hears the message from `shift()` (0x60a860).
#include <cstdint>
#include <optional>

#include "obf2/game/object_template.h"

namespace obf2::game {

struct ShiftTemplate {
  bool useShiftAnimation = false;  // `animation.useShiftAnimation`, +0x18
  float shiftDelay = 0.0f;         // `animation.shiftDelay`, +0x1c; ticks +0x20 (×30)
  int shiftDelayTicks() const { return static_cast<int>(shiftDelay * 30.0f); }
};

ShiftTemplate shiftOf(const ObjectTemplate& weapon);

class WeaponShift {
 public:
  void setWeapon(const ShiftTemplate& weapon);

  // `DefaultAnimationComp::requestFire` (0x60a710): no while a shift is waiting or
  // under way.
  bool canFire() const { return !pending_ && startedAt_ == 0; }

  // A round has completed (`fireComplete`, 0x60a840): with the shift on and rounds
  // left (`setAmmoLeft`, +0x54, above 0) one is asked for.
  void onRoundComplete(int roundsLeft);

  // One tick (`updateComponent`, 0x60a740): a shift asked for starts once the weapon
  // is not busy — `weaponBusy` is the other components' busy test — and raises
  // message 32 once (`shift()`, 0x60a860); it ends `shiftDelay` ticks after it
  // started. True when the message was raised in this tick.
  bool update(std::uint32_t tick, bool weaponBusy);

  int shifts() const { return shifts_; }

 private:
  ShiftTemplate template_;
  bool pending_ = false;            // +0x48
  std::uint32_t startedAt_ = 0;     // +0x4c: 0 when no shift is under way
  int shifts_ = 0;
};

}  // namespace obf2::game
