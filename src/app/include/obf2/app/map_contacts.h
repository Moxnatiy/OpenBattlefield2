#pragma once
// The map's live icons from the session's world (docs/functions/hud-map.md):
// our team's players, the server's vehicles, enemies under a mark or a spot,
// our orders, the commander's assets and the spots. `hud::Manager` draws them
// (`setMinimapContacts`).

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "obf2/hud/render.h"
#include "obf2/server/soldier_move.h"
#include "obf2/session/remote_world.h"

namespace obf2::app {

inline constexpr float kMapDegrees = 3.14159265f / 180.0f;

// A template property's last value, a texture path with forward slashes.
inline std::string mapIconOf(const game::ObjectTemplate& definition, const char* component,
                             const char* key) {
  const auto* part = definition.component(component);
  if (part == nullptr) return {};
  const auto found = part->properties.find(key);
  if (found == part->properties.end() || found->second.empty()) return {};
  std::string path(found->second.back().value());
  for (char& c : path) {
    if (c == '\\') c = '/';
  }
  return path;
}

// A radar object's radius, `ObjectTemplate.ability.radarRadius` (0 by default,
// Linux AbilityGLCompTemplate 0x5ae736); 0 for none.
inline int radarRadiusOf(const game::ObjectTemplate& definition) {
  const auto* ability = definition.component("ability");
  if (ability == nullptr) return 0;
  const auto found = ability->properties.find("radarradius");
  return found == ability->properties.end() || found->second.empty()
             ? 0
             : found->second.back().asInt().value_or(0);
}

// A kind-2 mark (RemoteWorld::stepMap: the satellite line 0x782580, the UAV
// beam 0x782710) on an enemy: drawn red at 0x77a9a0's fade, with the mark's
// texture when it has one. False: no such mark, and the enemy is not drawn.
inline bool applySweptMark(const session::RemoteWorld& remote, std::uint16_t object,
                           hud::MapContact& contact) {
  const auto* mark = remote.mapMarkOf(object);
  if (mark == nullptr || mark->kind != 2) return false;
  contact.object = object;
  contact.swept = true;
  contact.alpha = remote.mapMarkFade(*mark);
  if (!mark->texture.empty()) {
    contact.texture = mark->texture;
    contact.size = 16.0f;  // the mark's icon, as the kind-4 one below (0x783bc4)
  }
  return true;
}

// A kind-1 mark blinks the icon yellow; a kind-4 mark puts the specialist's
// icon in its place, 16 × 16. A soldier whose icon is mini_Medic, mini_Repair
// or mini_Ammo also gets +0x9b8, mini_MedicArrow, 16 × 16, turned by his
// heading (0x783bc4); the heading is the soldier's body yaw (the track's yaw
// less the aim, world_view.cpp), atan2(forward x, forward z) of rotationY.
inline void applyMapMark(const session::RemoteWorld& remote, std::uint16_t object,
                         hud::MapContact& contact, bool soldier = false) {
  contact.object = object;
  const auto* mark = remote.mapMarkOf(object);
  if (mark == nullptr) return;
  if (mark->kind == 1) contact.radioBlink = true;
  if (mark->kind == 4 && !mark->texture.empty()) {
    contact.texture = mark->texture;
    contact.size = 16.0f;
    const bool arrow = mark->texture.ends_with("mini_Medic.tga") ||
                       mark->texture.ends_with("mini_Repair.tga") ||
                       mark->texture.ends_with("mini_Ammo.tga");
    if (soldier && arrow) {
      if (const auto pose = remote.world.poseOf(object)) {
        contact.overlayTexture = "Ingame/Player/Icons/Minimap/mini_MedicArrow.tga";
        contact.overlayHeading = (pose->bodyYaw - pose->aimYaw) * kMapDegrees;
      }
    }
  }
}

// Every live icon, with a name for each (the diagnostics print it).
template <class Visitor>
void visitMapContacts(const session::RemoteWorld& remote, Visitor visit) {
  const auto& world = remote.world;
  const int team = world.ownTeam();
  if (team < 1 || team > 2) return;
  const auto own = static_cast<std::uint32_t>(world.ownPlayer());
  const auto self = world.players().find(own);
  const int squad = self == world.players().end() ? 0 : self->second.squad.value_or(0);
  using Kind = hud::MapContact::Kind;
  // Live objects, not ObjectSpawner positions. Create/DestroyObject and the
  // vehicle's ghost/control state own existence and position (network-events.md).
  for (const auto& [id, object] : world.objects()) {
    const auto* name = remote.templateNumbers.nameOf(object.templateId);
    const auto* definition = name ? remote.registry.find(*name) : nullptr;
    if (definition == nullptr) continue;
    const bool destroyed = object.vehicleHealth && *object.vehicleHealth <= 0;
    // The UAV layer's per-object circles (BF2.exe 0x77fc30, the +0xb38 map):
    // 0x5ac9f0 → 0x77bfe0 enters an object of the local team whose ability
    // component (Object +0x64; Linux `setComponent` puts IID_IAbilityGLComp at
    // +0xc8, 0x6891c8) has a radar radius above 0 —
    // `ObjectTemplate.ability.radarRadius`, 0 by default (Linux
    // AbilityGLCompTemplate 0x5ae736); no stock template sets it. The circle is
    // the UAV's texture, 2 × that radius wide, at the object, at the node alpha
    // with no fade, turning with the shared spin +0xabc. It goes when the object
    // is destroyed or turns out to be another team's (0x77aca0).
    {
      const int radius = radarRadiusOf(*definition);
      if (radius > 0 && !destroyed && (object.team == 0 || object.team == team)) {
        hud::MapContact circle{object.position.x, object.position.z, Kind::CommanderAsset};
        circle.texture = "Ingame/Minimap/map_UAV.tga";
        circle.worldSize = 2.0f * static_cast<float>(radius);
        circle.angle = remote.mapSpin;  // the shared spin +0xabc
        visit(std::string_view(*name), circle);
      }
    }
    if (definition->className != "PlayerControlObject") continue;
    // A strategic object's intact or destroyed icon at 19, else the vehicle's
    // own at 16 (hud-map.md, the sizes measured on the original's spawn screen).
    auto texture =
        mapIconOf(*definition, "StrategicObject", destroyed ? "destroyedicon" : "intacticon");
    const bool strategic = !texture.empty();
    if (!strategic && destroyed) continue;
    if (texture.empty()) texture = mapIconOf(*definition, "VehicleHud", "minimapicon");
    if (texture.empty()) continue;
    // The vehicle's colour is its occupants': our squad's, our team's, or
    // white while empty. With an enemy aboard it is the enemy's.
    Kind kind = Kind::Neutral;
    bool enemy = false;
    for (const auto& [playerId, occupant] : world.players()) {
      if (occupant.manDown.value_or(false) || !occupant.alive.value_or(occupant.object != 0)) continue;
      if (!occupant.object || occupant.object == remote.infantryObject ||
          remote.vehicleRootOf(occupant.object) != id)
        continue;
      if (occupant.team != team) {
        enemy = true;
        continue;
      }
      if (kind != Kind::Squad) kind = squad && occupant.squad == squad ? Kind::Squad : Kind::Team;
    }
    auto position = object.position;
    if (const auto pose = id == remote.vehicleRootOf(remote.ourSoldier)
                              ? remote.occupiedVehiclePose()
                              : world.poseOf(id, remote.tick.pending / server::kTickTime))
      position = pose->position;
    hud::MapContact contact{position.x, position.z, kind};
    contact.texture = std::move(texture);
    contact.size = strategic ? 19.0f : 16.0f;
    // A vehicle turns with itself; a strategic icon stays as the level's.
    if (!strategic) contact.angle = -object.yaw.value_or(0.0f) * kMapDegrees;
    if (enemy) {
      // An enemy vehicle shows only with a kind-2 mark; other enemy routes are
      // not measured.
      if (!applySweptMark(remote, id, contact)) continue;
    } else {
      applyMapMark(remote, id, contact);
    }
    visit(std::string_view(*name), contact);
  }
  for (const auto& [id, player] : world.players()) {
    if ((player.team != 1 && player.team != 2) || player.manDown.value_or(false) ||
        !player.alive.value_or(player.object != 0))
      continue;
    const auto kind = id == own                             ? Kind::Self
                      : squad && player.squad == squad ? Kind::Squad
                                                            : Kind::Team;
    if (kind == Kind::Self) {
      if (const auto position = remote.drawnSoldierPosition()) {
        visit(std::string_view(player.name), hud::MapContact{position->x, position->z, kind});
        continue;
      }
    }
    const auto seat = remote.vehicleSeatOf(player.object);
    if (remote.vehicleRootOf(player.object)) continue;  // represented once by the vehicle icon
    const auto object = world.objects().find(seat ? seat->parent : player.object);
    if (object == world.objects().end() ||
        !(object->second.fromGhostStream || (seat && object->second.hasCreationPosition)))
      continue;
    const auto& position = object->second.position;
    if (player.team != team) {
      // An enemy soldier with a live position, only under a kind-2 mark.
      const auto* latest = object->second.track.newest();
      const float serverTime =
          static_cast<float>(world.newestPacketTick()) * net::bf2::kGhostTickMs;
      if (!object->second.fromGhostStream || !latest || serverTime - latest->timeMs > 1500.0f)
        continue;  // 1.5 s: not measured
      hud::MapContact contact{position.x, position.z, Kind::Spotted};
      if (applySweptMark(remote, object->first, contact)) visit("Detected enemy", contact);
      continue;
    }
    // `populatePlayerMapIcon` (OpenBF2 Menu/Bf2MenuMapNode.cpp) with the role
    // icons on: a squad leader's squad number and the commander's own mark,
    // both over `mini_headingArrow` turned by his yaw; `colorMapIcon` draws the
    // commander white and the arrow black.
    hud::MapContact contact{position.x, position.z, kind};
    const bool commander = player.commander.value_or(false);
    const bool leader = player.squadLeader && player.squad.value_or(0) > 0;
    if (leader || commander) {
      hud::MapContact arrow{position.x, position.z, kind};
      arrow.texture = "Ingame/Player/Icons/Minimap/mini_headingArrow.tga";
      arrow.size = 32.0f;
      arrow.angle = -object->second.yaw.value_or(0.0f) * kMapDegrees;
      arrow.tint = hud::Color{0.0f, 0.0f, 0.0f, 1.0f};
      visit(std::string_view(player.name), arrow);
    }
    if (leader) {
      contact.texture = "Ingame/Player/Icons/Minimap/mini_Squad_" +
                        std::to_string(player.squad.value_or(0)) + "_small.tga";
    } else if (commander) {
      contact.texture = "Ingame/Player/Icons/Minimap/mini_commander2.tga";
      contact.size = 32.0f;
      contact.tint = hud::Color{};
    }
    applyMapMark(remote, seat ? seat->parent : player.object, contact, !seat);
    visit(std::string_view(player.name), contact);
  }
  for (const auto& order : world.ownOrders()) {
    if (order.route == net::bf2::OrderRoute::Request) continue;
    if (const auto point = world.orderPosition(order)) {
      const auto kind = order.accepted ? Kind::Order : Kind::PendingOrder;
      const auto name = order.id == 4   ? "Move"
                        : order.id == 5 ? "Attack"
                        : order.id == 6 ? "Defend"
                        : order.id == 7 ? "Repair"
                        : order.id == 8 ? "Destroy"
                                        : "Squad order";
      hud::MapContact contact{point->x, point->z, kind};
      contact.order = order.id;
      contact.commander = order.route == net::bf2::OrderRoute::Commander;
      visit(std::string_view(name), contact);
    }
  }
  for (const auto& effect : remote.commanderEffects) {
    if (effect.expires <= world.gameTick()) continue;
    using Tool = net::bf2::CommanderTool;
    using Asset = hud::MapContact::Asset;
    const auto [name, asset] = effect.tool == Tool::UAV         ? std::pair{"UAV", Asset::UAV}
                               : effect.tool == Tool::Artillery ? std::pair{"Artillery", Asset::Artillery}
                               : effect.tool == Tool::Supplies  ? std::pair{"Supply drop", Asset::Supplies}
                                                                : std::pair{"Vehicle drop", Asset::Vehicle};
    hud::MapContact contact{effect.position.x, effect.position.z, Kind::CommanderAsset};
    contact.asset = asset;
    if (effect.tool == Tool::UAV) {
      // The map's UAV circle (layer 0x20, BF2.exe 0x77fc30), set up by 0x77a0d0:
      // `Ingame/Minimap/map_UAV.tga` (0x781928), 2 × `GameLogic::getUAVRadius`
      // wide (100 m, Linux GameLogic::GameLogic 0x477813), turning at
      // +0xac0 = 4 rad/s (0x77a0d0) and wrapping at 2π (0x77b2b3). Its time t
      // runs over the UAV's 30 s (`getUAVActiveTime`, Linux 0x4777fb); with
      // f = t / 30 the alpha is 10f below 0.1, 1 − (f − 0.9)·10 above 0.9
      // (0x77fc30), times the node's icon alpha.
      constexpr float kUavRadius = 100.0f;
      constexpr float kUavActiveTime = 30.0f;
      const float t = static_cast<float>(world.gameTick() - effect.start) * server::kTickTime;
      const float f = t / kUavActiveTime;
      contact.texture = "Ingame/Minimap/map_UAV.tga";
      contact.worldSize = 2.0f * kUavRadius;
      contact.angle = remote.mapSpin;  // +0xabc, zeroed at the UAV start (0x77a0d0)
      contact.alpha = f > 0.9f   ? 1.0f - (f - 0.9f) * 10.0f
                      : f < 0.1f ? 1.0f - (0.1f - f) * 10.0f
                                 : 1.0f;
    }
    visit(std::string_view(name), contact);
  }
  for (const auto& spot : world.visibleSpots()) {
    hud::MapContact contact{spot.x, spot.z, Kind::Spotted};
    contact.blinks = !spot.followsObject;
    if (spot.followsObject) {
      // A spot on an enemy the client holds is a kind-0 entry (BF2.exe 0x782feb,
      // 12 s): it only lets the enemy's own icon draw — the vehicle's minimap
      // icon or the soldier's — coloured by 0x7788b0 for another team: the
      // table 0xa1ccc8 at index 1, (0.87, 0.094, 0.094) from 0x97dea0.
      contact.enemy = true;
      const auto object = world.objects().find(spot.target);
      const auto root = object == world.objects().end() ? 0 : remote.vehicleRootOf(spot.target);
      const auto vehicle = world.objects().find(root ? root : spot.target);
      if (vehicle != world.objects().end()) {
        const auto* name = remote.templateNumbers.nameOf(vehicle->second.templateId);
        const auto* definition = name ? remote.registry.find(*name) : nullptr;
        if (definition != nullptr) {
          contact.texture = mapIconOf(*definition, "VehicleHud", "minimapicon");
        }
      }
    }
    visit(std::string_view(spot.target ? "Spotted enemy" : "Spotted position"), contact);
  }
}

inline std::vector<hud::MapContact> minimapContacts(const session::RemoteWorld& remote) {
  std::vector<hud::MapContact> contacts;
  visitMapContacts(remote, [&](std::string_view, const hud::MapContact& contact) {
    if (contact.kind != hud::MapContact::Kind::Self) contacts.push_back(contact);
  });
  return contacts;
}

}  // namespace obf2::app
