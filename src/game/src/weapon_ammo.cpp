#include "obf2/game/weapon_ammo.h"

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>

namespace obf2::game {
namespace {

std::optional<float> ammoFloat(const Component& ammo, const char* property) {
  const auto found = ammo.properties.find(property);
  if (found == ammo.properties.end() || found->second.empty()) return std::nullopt;
  return found->second.back().asFloat(0);
}

}  // namespace

AmmoTemplate ammoOf(const ObjectTemplate& weapon) {
  AmmoTemplate out;
  // `ObjectTemplate.ammo.*` whatever the component is called (`DefaultAmmoComp`),
  // so the registry files them under "ammo".
  const Component* ammo = weapon.component("ammo");
  if (ammo == nullptr) return out;
  const auto number = [&](const char* name, auto& field) {
    if (const auto value = ammoFloat(*ammo, name)) field = static_cast<std::decay_t<decltype(field)>>(*value);
  };
  number("magsize", out.magSize);
  number("nrofmags", out.nrOfMags);
  number("reloadamount", out.reloadAmount);
  number("reloadtime", out.reloadTime);
  number("changemagat", out.changeMagAt);
  number("firstshotextratime", out.firstShotExtraTime);
  number("lastshotextratime", out.lastShotExtraTime);
  number("minimumtimeuntilreload", out.minimumTimeUntilReload);
  const auto flag = [&](const char* name, bool& field) {
    if (const auto value = ammoFloat(*ammo, name)) field = *value != 0.0f;
  };
  flag("autoreload", out.autoReload);
  flag("instantreloadonenable", out.instantReloadOnEnable);
  flag("togglewhennoammo", out.toggleWhenNoAmmo);
  flag("reloadwithoutplayer", out.reloadWithoutPlayer);
  // `setChangeMagAt` (Linux 0x5ea5a0) keeps it between 0 and the constant at
  // 0xb2f3bc; the data uses 0 to 1, and 1 is the bound read here.
  out.changeMagAt = std::clamp(out.changeMagAt, 0.0f, 1.0f);
  return out;
}

void WeaponAmmo::setWeapon(const AmmoTemplate& weapon) {
  *this = WeaponAmmo{};
  template_ = weapon;
  // `AmmoComp`'s constructor (Linux 0x5eb620): `nrOfMags` magazines of `magSize`
  // rounds; -1 mags is one that is never counted down (`getMaxNrOfMags`).
  mags_.clear();
  if (weapon.nrOfMags == -1) {
    mags_.push_back(weapon.magSize);
  } else if (weapon.nrOfMags > 0) {
    mags_.assign(static_cast<std::size_t>(weapon.nrOfMags), weapon.magSize);
  } else {
    mags_.push_back(-1);
  }
}

void WeaponAmmo::synchronize(int rounds, int magazines) {
  if (rounds < (template_.magSize < 0 ? -1 : 0) || rounds > template_.magSize ||
      magazines < (template_.nrOfMags < 0 ? -1 : 0) || magazines > template_.nrOfMags) {
    return;
  }
  mags_.resize(static_cast<std::size_t>(std::max(1, magazines)), template_.magSize);
  mags_.front() = rounds;
}

bool WeaponAmmo::canReload() const {
  // 0x5eb520: not when the magazine in the weapon is full, then when a spare is
  // left or the supply is endless.
  if (!mags_.empty() && mags_[0] == template_.magSize) return false;
  return mags_.size() > 1 || template_.nrOfMags == -1;
}

bool WeaponAmmo::canFire() const {
  return !needsReload() && state_ != 2 && !busy();
}

void WeaponAmmo::onRound(std::uint32_t tick) {
  // postFire (0x5ead60)
  if (mags_[0] > 0) mags_[0] -= 1;
  if (mags_[0] == 0) emptyAfterFire_ = true;
  autoReloadPending_ = template_.autoReload && mags_[0] == 0;
  // fireComplete (0x5ea7a0)
  waitUntil_ = tick + static_cast<std::uint32_t>(template_.minimumTicks());
  state_ = 1;
  if (autoReloadPending_ && needsReload() && canReload()) {
    // `waitUntil_` is not before now, so the signal path (0x5ea827) and not the
    // immediate `doReload`.
    signalReload_ = true;
  }
}

bool WeaponAmmo::requestReload(std::uint32_t tick) {
  (void)tick;
  // The reload message (`GenericFireArm::internalHandleMessage`, Linux 0x5e758c..
  // 0x5e75ba): nothing when the reload state is already 2; then
  // `requestReload(true)` — and, when it accepts, `doReload()` at once, whose
  // `ammo->reload()` makes the state 2 (0x5ea350). The next `updateComponent`
  // raises the signal that starts the reload (state 2, 0x5ea756), so the reload
  // begins in this very tick's update.
  if (state_ == 2) return false;
  // `AmmoComp::requestReload(true)` (0x5eab70): refused with `autoReload` (0x5eab77..
  // 0x5eab84) and while reloading; otherwise the queue is set and the answer is
  // `canReload` (0x5eabb4).
  if (template_.autoReload || state_ > 1) return false;
  if (template_.reloadAmount == 0) {
    queued_ = 1;
    requested_ = 1;
  } else if (queued_ == 0) {
    queued_ = template_.magSize - mags_[0];
    requested_ = queued_;
  }
  if (!canReload()) return false;
  state_ = 2;
  return true;
}

void WeaponAmmo::doReload(std::uint32_t tick) {
  const auto fraction = static_cast<std::uint32_t>(template_.changeMagTicks());
  const auto length = static_cast<std::uint32_t>(template_.reloadTicks());
  const auto first = static_cast<std::uint32_t>(template_.firstExtraTicks());
  const auto last = static_cast<std::uint32_t>(template_.lastExtraTicks());
  const int left = std::max(queued_ - 1, 0);
  if (template_.reloadAmount == 0) {
    state_ = 3;
    magChanged_ = false;
    changeMagAt_ = tick + fraction;
    completeAt_ = tick + length;
  } else if (queued_ == requested_) {
    state_ = 4;
    magChanged_ = false;
    changeMagAt_ = tick + first + fraction;
    completeAt_ = tick + first + length;
  } else if (queued_ != 0) {
    state_ = 5;
    magChanged_ = false;
    changeMagAt_ = tick + fraction;
    completeAt_ = tick + length;
  } else {
    state_ = 6;
    magChanged_ = true;
    completeAt_ = tick + last;
  }
  queued_ = left;
  autoReloadPending_ = false;
}

void WeaponAmmo::reloadMag() {
  // 0x5eb560
  if (template_.nrOfMags != -1) {
    if (mags_.size() <= 1) {
      magChanged_ = true;
      return;
    }
    mags_.pop_back();
  }
  if (template_.reloadAmount == 0) {
    mags_[0] = template_.magSize;
  } else {
    mags_[0] += 1;
  }
  magChanged_ = true;
  if (mags_[0] == template_.magSize) emptyAfterFire_ = false;
}

void WeaponAmmo::reloadComplete() {
  // 0x5ea650
  signalComplete_ = true;
  if (queued_ > 0 || state_ == 4 || state_ == 5) signalReload_ = true;
  state_ = 0;
}

bool WeaponAmmo::takeSignalReload(std::uint32_t tick) {
  // 0x5eab20
  const bool raised = signalReload_;
  signalReload_ = false;
  if (!raised) return false;
  if (waitUntil_ == 0) return true;
  if (tick < waitUntil_) {
    signalReload_ = true;
    return false;
  }
  waitUntil_ = 0;
  return true;
}

ReloadMessage WeaponAmmo::update(std::uint32_t tick) {
  // updateComponent (0x5ea680)
  if (state_ > 2) {
    if (!magChanged_ && tick >= changeMagAt_) reloadMag();
    if (completeAt_ <= tick) reloadComplete();
  }
  if (state_ == 2) signalReload_ = true;  // nothing else of the weapon is busy
  if (waitUntil_ < tick) {
    if (template_.autoReload && state_ <= 1) {
      if (needsReload() && canReload()) signalReload_ = true;
    }
    if (state_ == 1) state_ = 0;
  }
  // GenericFireArm::handleUpdate (0x5e7a24..0x5e7b44)
  signalComplete_ = false;
  if (!takeSignalReload(tick)) return ReloadMessage::None;
  if (state_ != 2) {
    state_ = 2;
  } else {
    doReload(tick);
  }
  switch (state_) {
    case 3: return ReloadMessage::Once;
    case 4: return ReloadMessage::Hold;
    case 0:
    case 6: return ReloadMessage::Clear;
    default: return ReloadMessage::None;
  }
}

float WeaponAmmo::reloadLeft(std::uint32_t tick) const {
  if (!reloading() || template_.reloadTicks() <= 0) return 0.0f;
  return static_cast<float>(static_cast<std::int64_t>(completeAt_) - static_cast<std::int64_t>(tick)) /
         static_cast<float>(template_.reloadTicks());
}

}  // namespace obf2::game
