#pragma once
// A vehicle's seats, and the network id each one has.
//
// Every seat is a `PlayerControlObject` of the vehicle's template tree, and every
// control object with a networkable is a descriptor of its own:
// `NetworkManager::analyseObject` (OpenBF2 IO/Network/NetworkManager.cpp) walks
// `getAllPlayerControlObjects` — the object itself, then each child subtree
// depth-first in tree order — and gives the first the vehicle's id and each next
// one with properties the id after it (`requestedId = assigned + 1`). So the
// seat `EnterVehicleEvent` names is the vehicle's id plus the seat's index here.
// Checked live: a T-90 created as 1830, a bot entering 1831 — its gunner.
//
// A seat's place is its `seatInformation <object> <position> <rotation>`: an
// offset from the named object of the tree (the hull, or a turret base), taken
// in that object's rest pose — the turrets' live rotation
// (`RotationalBundleNetworkable`) is not applied. Its pose is the
// `seatAnimationSystem` the soldier plays there.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/game/object_template.h"

namespace obf2::game {

struct VehicleSeat {
  int index = 0;              // the network id is the vehicle's + this
  std::string templateName;   // the seat's control object
  std::string seatObject;     // `seatInformation`'s object
  Mat4 transform = Mat4::identity();  // the soldier's place, from the vehicle's origin
  bool hasSeatInformation = false;
  std::string animationSystem;  // `seatAnimationSystem`
};

// The seats of a vehicle template, in network order; empty when the template is
// unknown or has none.
std::vector<VehicleSeat> vehicleSeats(const Registry& registry, std::string_view vehicle);

}  // namespace obf2::game
