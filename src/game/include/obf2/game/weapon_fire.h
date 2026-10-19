#pragma once
// When a hand weapon fires a round: its rate of fire, its fire rates (single or
// automatic) and the trigger, as the weapon's `SingleFireComp` and
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

  // Ticks between two rounds: `1800 / rpm`, kept beside the rate in the template
  // (+0x40 in the client, `setRoundsPerMinute` Linux 0x5f5d60).
  int ticksBetweenRounds() const;
};

FireTemplate fireOf(const ObjectTemplate& weapon);

// The fire rate that keeps firing while the trigger is held. `FireComp::requestFire`
// (Linux 0x5f5110) treats 2 apart: for it the trigger held from before is no
// reason to refuse; for any other rate a round needs a new press. That 2 is
// automatic is read; what separates 0 from 1 (single from burst,
// `getBurstSize`, template +0x4c = 3) is not.
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
  bool tick(std::uint32_t tick, bool held);

  // `switchFireRate` (Linux 0x5f62f0): the next rate in the template's list,
  // round the list.
  void switchFireRate();

  int fireRate() const;
  std::uint32_t roundsFired() const { return rounds_; }

 private:
  FireTemplate weapon_;
  std::size_t rate_ = 0;
  bool heldBefore_ = false;
  bool armed_ = false;
  std::uint32_t readyAt_ = 0;
  std::uint32_t rounds_ = 0;
};

}  // namespace obf2::game
