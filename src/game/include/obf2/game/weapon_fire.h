#pragma once
// When a hand weapon fires a round: its rate of fire, fire mode and trigger, as
// the weapon's `SingleFireComp` and
// `GenericFireArm::handleUpdate` decide it. A round fired is what the weapon's
// animation hears as message 1 (`DefaultAnimationComp::postFire`, Linux
// 0x60a7d0). docs/functions/soldier-model.md, "Who raises the messages".
#include <cstdint>
#include <vector>

#include "obf2/game/object_template.h"

namespace obf2::game {

struct FireTemplate {
  // `fire.roundsPerMinute`; 600 from `FireCompTemplate`'s constructor (`BF2.exe`
  // 0x621310, `0x258` into +0x3c; Linux 0x5f5d70), which no M4 file overrides.
  int roundsPerMinute = 600;
  // `fire.addFireRate`, in the order the data adds them; the first is the one a
  // weapon starts with. Empty means none was added.
  std::vector<int> fireRates;
  // `fire.burstSize`: rounds in a burst (`getBurstSize`, template +0x4c), 3 by
  // default (upstream note; the flare launcher and two artillery pieces set 5).
  unsigned burstSize = 3;

  // Ticks between two rounds: `1800 / rpm`, kept beside the rate in the template
  // (+0x40 in the client, `setRoundsPerMinute` Linux 0x5f5d60).
  int ticksBetweenRounds() const;
};

FireTemplate fireOf(const ObjectTemplate& weapon);

// The fire rate that keeps firing while the trigger is held. `FireComp::requestFire`
// (Linux 0x5f5110) treats 2 apart: for it the trigger held from before is no
// reason to refuse. Retail templates use 0 for single, 1 for burst and 2 for
// automatic.
inline constexpr int kFireRateBurst = 1;
inline constexpr int kFireRateAutomatic = 2;

class WeaponFire {
 public:
  void setWeapon(const FireTemplate& weapon);

  // One tick: `held` is the fire button in this tick's action. True when a round
  // is fired in it.
  //
  //   a round sets the next ready tick to now + ticksBetweenRounds
  //     (`SingleFireComp::fire`, Linux 0x5fcf6e..0x5fcf77, +0x164);
  //   from that tick on the component is ready again (`updateComponent`,
  //     0x5fda0c → `fireComplete` 0x5fce40) and the same tick may fire;
  //   held from the tick before only fires on the automatic rate (0x5f5158).
  //
  // Not taken: the start delay (`fireStartDelay`, template +0x14, 0 for the M4),
  // the ammunition and reloading, the heat, `busy` of the other components.
  bool tick(std::uint32_t tick, bool held, bool available = true);

  // `switchFireRate` (Linux 0x5f62f0): the next rate in the template's list,
  // round the list.
  bool switchFireRate();
  void setFireRate(int mode);

  int fireRate() const;
  bool hasFireRate() const { return !weapon_.fireRates.empty(); }
  std::uint32_t roundsFired() const { return rounds_; }
  // The fire component is busy from a round until its next ready tick — the window
  // the other components wait out (`SingleFireComp::busy`).
  bool busy(std::uint32_t tick) const { return armed_ && tick < readyAt_; }

 private:
  FireTemplate weapon_;
  std::size_t rate_ = 0;
  bool heldBefore_ = false;
  unsigned burstRemaining_ = 0;
  bool armed_ = false;
  std::uint32_t readyAt_ = 0;
  std::uint32_t rounds_ = 0;
};

}  // namespace obf2::game
