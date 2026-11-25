#include "obf2/game/weapon_fire.h"

#include <algorithm>

namespace obf2::game {

int FireTemplate::ticksBetweenRounds() const {
  return roundsPerMinute > 0 ? 1800 / roundsPerMinute : 0;
}

FireTemplate fireOf(const ObjectTemplate& weapon) {
  FireTemplate out;
  // The properties are `ObjectTemplate.fire.*` whatever the component is called
  // (`SingleFireComp`), so the registry files them under "fire".
  const Component* fire = weapon.component("fire");
  if (fire == nullptr) return out;
  if (const auto found = fire->properties.find("roundsperminute");
      found != fire->properties.end() && !found->second.empty()) {
    if (const auto rpm = found->second.back().asInt(0)) out.roundsPerMinute = *rpm;
  }
  if (const auto found = fire->properties.find("addfirerate"); found != fire->properties.end()) {
    for (const Property& call : found->second) {
      if (const auto rate = call.asInt(0)) out.fireRates.push_back(*rate);
    }
  }
  if (const auto found = fire->properties.find("burstsize");
      found != fire->properties.end() && !found->second.empty()) {
    if (const auto size = found->second.back().asInt(0); size && *size > 0) {
      out.burstSize = static_cast<unsigned>(*size);
    }
  }
  return out;
}

void WeaponFire::setWeapon(const FireTemplate& weapon) {
  weapon_ = weapon;
  rate_ = 0;
  heldBefore_ = false;
  burstRemaining_ = 0;
  armed_ = false;
  readyAt_ = 0;
  rounds_ = 0;
}

int WeaponFire::fireRate() const {
  return rate_ < weapon_.fireRates.size() ? weapon_.fireRates[rate_] : 0;
}

bool WeaponFire::switchFireRate() {
  if (weapon_.fireRates.size() < 2) return false;
  rate_ = (rate_ + 1) % weapon_.fireRates.size();
  burstRemaining_ = 0;
  return true;
}

void WeaponFire::setFireRate(int mode) {
  if (fireRate() == mode) return;
  const auto found = std::ranges::find(weapon_.fireRates, mode);
  if (found == weapon_.fireRates.end()) return;
  rate_ = static_cast<std::size_t>(found - weapon_.fireRates.begin());
  burstRemaining_ = 0;
}

bool WeaponFire::tick(std::uint32_t tick, bool held, bool available) {
  const bool pressed = held && !heldBefore_;
  heldBefore_ = held;
  if (!available) {
    burstRemaining_ = 0;
    return false;
  }
  if (armed_ && tick < readyAt_) return false;
  if (fireRate() == kFireRateBurst) {
    if (burstRemaining_ == 0 && pressed) burstRemaining_ = weapon_.burstSize;
    if (burstRemaining_ == 0) return false;
    --burstRemaining_;
  } else if (!held || (!pressed && fireRate() != kFireRateAutomatic)) {
    return false;
  }
  armed_ = true;
  readyAt_ = tick + static_cast<std::uint32_t>(weapon_.ticksBetweenRounds());
  ++rounds_;
  return true;
}

}  // namespace obf2::game
