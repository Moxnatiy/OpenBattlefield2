#pragma once
// A hand weapon's magazines and its reload: the template's `ObjectTemplate.ammo.*`
// and the state machine `AmmoComp` runs on it (docs/functions/weapon-ammo.md).
//
// All of it is the server's code (Linux `AmmoComp`, 0x5ea150..0x5ec340): the client
// runs the same component for its own weapon and `GenericFireArm::handleUpdate`
// (0x5e7640..) glues it to the other components, which is why the client's HUD
// and animation can follow it without waiting for the server.
#include <cstdint>
#include <vector>

#include "obf2/game/object_template.h"

namespace obf2::game {

// `AmmoCompTemplate` (Linux): the defaults are its constructor's (0x5eafa0), the
// fields the script commands' setters write (`makeScript` 0x5eb940 names each
// command against its offset). Times are seconds in the data and ticks (×30,
// `0xb2efc4`) in the component.
struct AmmoTemplate {
  int magSize = 30;                  // `ammo.magSize`, +0x40 (default 0x1e)
  int nrOfMags = 5;                  // `ammo.nrOfMags`, +0x3c; -1 never runs out
  int reloadAmount = 0;              // `ammo.reloadAmount`, +0x44; 0 reloads a whole magazine
  float reloadTime = 2.0f;           // `ammo.reloadTime`, +0x14 (0x40000000)
  float changeMagAt = 0.7f;          // `ammo.changeMagAt`, +0x2c (0x3f333333), clamped to 0..1
  bool autoReload = false;           // `ammo.autoReload`, +0x30
  bool instantReloadOnEnable = false;  // `ammo.instantReloadOnEnable`, +0x31 — not used yet
  bool toggleWhenNoAmmo = false;     // `ammo.toggleWhenNoAmmo`, +0x48 — not used yet
  bool reloadWithoutPlayer = true;   // `ammo.reloadWithoutPlayer`, +0x49 — not used yet
  float firstShotExtraTime = 0.0f;   // `ammo.firstShotExtraTime`, +0x1c / ticks +0x20
  float lastShotExtraTime = 0.0f;    // `ammo.lastShotExtraTime`, +0x24 / ticks +0x28
  float minimumTimeUntilReload = 0.0f;  // `ammo.minimumTimeUntilReload`, +0x34 / ticks +0x38

  // `setReloadTime` (0x5ea5c0): truncated, not rounded.
  int reloadTicks() const { return static_cast<int>(reloadTime * 30.0f); }
  int firstExtraTicks() const { return static_cast<int>(firstShotExtraTime * 30.0f); }
  int lastExtraTicks() const { return static_cast<int>(lastShotExtraTime * 30.0f); }
  int minimumTicks() const { return static_cast<int>(minimumTimeUntilReload * 30.0f); }
  // `doReload`: ticks into the reload at which the magazine changes.
  int changeMagTicks() const { return static_cast<int>(static_cast<float>(reloadTicks()) * changeMagAt); }
};

AmmoTemplate ammoOf(const ObjectTemplate& weapon);

// What the weapon's other components hear when the reload moves, as
// `DefaultAnimationComp::reload(state)` (Linux 0x60aad0, jump table 0xb78b58)
// turns it into a message on the animation system: state 3 raises message 2 once,
// state 4 holds it, states 0 and 6 let it go; 1, 2 and 5 say nothing.
enum class ReloadMessage { None, Once, Hold, Clear };

class WeaponAmmo {
 public:
  void setWeapon(const AmmoTemplate& weapon);
  void synchronize(int rounds, int magazines);

  // `AmmoComp::requestFire` (0x5ea150) with `busy` (0x5ec230): a round may leave
  // when the magazine is not empty, the component is not asking for a reload
  // (state 2) and not in the middle of one (state above 2).
  bool canFire() const;

  // A round left the barrel at `tick`: `postFire` (0x5ead60) takes it out of the
  // magazine, `fireComplete` (0x5ea7a0) starts the wait and arms an auto reload.
  void onRound(std::uint32_t tick);

  // The reload key. `GenericFireArm::requestReload` (0x5e2c10) asks every
  // component and then `AmmoComp::requestReload` (0x5eab70): refused while
  // reloading or when `canReload` (0x5eb520) says no — the magazine is full or
  // no spare is left. On success the reload is queued (+0x60/+0x64) and the state
  // is 2 at once; the update of the same tick starts it (message 10 in
  // `GenericFireArm::internalHandleMessage`, 0x5e758c..0x5e75ba).
  bool requestReload(std::uint32_t tick);

  // One tick of `updateComponent` (0x5ea680) followed by the glue in
  // `GenericFireArm::handleUpdate` that turns the component's two signals into
  // `doReload` (0x5e2ce0). Returns what the animation should hear.
  ReloadMessage update(std::uint32_t tick);

  int roundsInMag() const { return mags_.empty() ? 0 : mags_[0]; }
  // The magazines beyond the one in the weapon (`getMagVectorSize() - 1`, the
  // client's `PrimaryClips`, 0x7a5800).
  int spareMags() const { return mags_.empty() ? 0 : static_cast<int>(mags_.size()) - 1; }
  int state() const { return state_; }
  bool reloading() const { return state_ > 1; }
  // `getReloadTimeLeftInPercent` (0x5ea380): 0 when not reloading.
  float reloadLeft(std::uint32_t tick) const;
  const AmmoTemplate& tmpl() const { return template_; }

 private:
  bool needsReload() const { return roundsInMag() == 0; }
  bool canReload() const;
  bool busy() const { return state_ > 2 || signalReload_; }
  void doReload(std::uint32_t tick);
  void reloadMag();
  void reloadComplete();
  // `getSignalReload` (0x5eab20): reads and clears the flag, held back until the
  // wait after the last round is over.
  bool takeSignalReload(std::uint32_t tick);

  AmmoTemplate template_;
  std::vector<int> mags_{30, 30, 30, 30, 30};  // +0x28: [0] in the weapon, the rest spare
  int state_ = 0;                  // +0x68
  int queued_ = 0;                 // +0x60
  int requested_ = 0;              // +0x64
  std::uint32_t waitUntil_ = 0;    // +0x44: after a round, the wait before a reload
  std::uint32_t changeMagAt_ = 0;  // +0x48
  std::uint32_t completeAt_ = 0;   // +0x4c
  bool magChanged_ = false;        // +0x6c
  bool emptyAfterFire_ = false;    // +0x6d
  bool signalComplete_ = false;    // +0x6e
  bool signalReload_ = false;      // +0x6f
  bool autoReloadPending_ = false;  // +0x70
};

}  // namespace obf2::game
