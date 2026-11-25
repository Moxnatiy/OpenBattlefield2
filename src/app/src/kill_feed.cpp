#include "obf2/app/kill_feed.h"

#include <cctype>

namespace obf2::app {

namespace {

bool sameText(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

const game::ObjectTemplate* templateOf(std::uint32_t number, const game::TemplateNumbers& numbers,
                                       const game::Registry& registry) {
  const std::string* name = numbers.nameOf(number);
  return name != nullptr ? registry.find(*name) : nullptr;
}

// A component's `hudName`, the quotes the `.tweak` writes it in taken off.
std::optional<std::string> hudName(const game::ObjectTemplate& object, std::string_view component) {
  const game::Component* found = object.component(component);
  if (found == nullptr) return std::nullopt;
  const auto at = found->properties.find("hudname");
  if (at == found->properties.end() || at->second.empty()) return std::string{};
  std::string value(at->second.back().value());
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

}  // namespace

WeaponName killWeaponName(const net::bf2::Event::KilledBy& kill,
                          const game::TemplateNumbers& numbers, const game::Registry& registry) {
  WeaponName out;
  const game::ObjectTemplate* vehicle = templateOf(kill.vehicleTemplate, numbers, registry);
  if (vehicle == nullptr) return out;  // the killer's current vehicle: not ours (see the header)
  const game::ObjectTemplate* weapon = templateOf(kill.weaponTemplate, numbers, registry);
  if (weapon != nullptr &&
      (sameText(vehicle->className, "Soldier") || sameText(vehicle->className, "LadderContainer") ||
       sameText(vehicle->className, "Parachute"))) {
    if (const auto name = hudName(*weapon, "WeaponHud")) {
      out.key = *name;
    } else {
      out.missingWeapon = true;
    }
  }
  if (out.key.empty() && !out.missingWeapon) {
    if (const auto name = hudName(*vehicle, "VehicleHud")) {
      out.key = *name;
    } else if (weapon != nullptr) {
      if (const auto weaponName = hudName(*weapon, "WeaponHud")) out.key = *weaponName;
    }
  }
  return out;
}

hud::Manager::Kill killForHud(const net::bf2::Event::KilledBy& kill,
                              const std::map<std::uint32_t, net::bf2::RemotePlayer>& players,
                              int ownPlayer, const game::TemplateNumbers& numbers,
                              const game::Registry& registry) {
  hud::Manager::Kill out;
  out.killer = static_cast<int>(kill.killer);
  out.victim = static_cast<int>(kill.victim);
  out.killerAlive = kill.killerAlive;
  if (const auto found = players.find(kill.killer); found != players.end()) {
    out.killerKnown = true;
    out.killerName = found->second.name;
    out.killerTeam = found->second.team;
    out.killerSpeaker.commander = found->second.commander.value_or(false);
    // `selectPlayerColors`: a squadmate of ours on our side is the squad's colour.
    if (const auto self = players.find(static_cast<std::uint32_t>(ownPlayer)); self != players.end()) {
      const auto squad = found->second.squad.value_or(0);
      out.killerSpeaker.squadColour = squad != 0 && self->second.team == found->second.team &&
                                      self->second.squad.value_or(0) == squad;
    }
  }
  if (const auto found = players.find(kill.victim); found != players.end()) {
    out.victimKnown = true;
    out.victimName = found->second.name;
    out.victimTeam = found->second.team;
  }
  const WeaponName weapon = killWeaponName(kill, numbers, registry);
  out.weaponKey = weapon.key;
  out.missingWeapon = weapon.missingWeapon;
  return out;
}

hud::Manager::Local localForHud(const std::map<std::uint32_t, net::bf2::RemotePlayer>& players,
                                int ownPlayer, int ownTeam) {
  hud::Manager::Local local;
  local.id = ownPlayer;
  local.team = ownTeam;
  if (const auto self = players.find(static_cast<std::uint32_t>(ownPlayer)); self != players.end()) {
    local.commander = self->second.commander.value_or(false);
  }
  return local;
}

}  // namespace obf2::app
