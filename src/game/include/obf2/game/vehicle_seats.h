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
// offset from the named object of the tree (the hull, or a turret base). Its
// ancestors with replicated rotations move the seat away from its rest pose.
// The soldier plays the seat's `seatAnimationSystem` there.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/game/object_template.h"

namespace obf2::game {

struct VehicleSeatJoint {
  unsigned seat;
  std::size_t bundle;
  Mat4 rest;
  Mat4 parentRest = Mat4::identity();
  bool stabilizeYaw = false;
  bool aimYaw = false, aimPitch = false;
};

struct VehicleSeatCamera {
  Mat4 transform;
  std::vector<VehicleSeatJoint> joints;
  // docs/functions/vehicle-camera-modes.md.
  // Defaults: Linux CameraTemplate::CameraTemplate (0x577ab0). chaseDistance
  // -1 means the vehicle's bounding radius (at least 2) times g_cam_dist
  // (getTransformationCVMChase 0x57ab80); g_cam_dist is not measured.
  float chaseDistance = -1.0f;   // +0x2b4
  float chaseAngle = 0.3f;       // +0x2b8, 0x3e99999a
  Vec3f chaseOffset;             // +0x2bc..+0x2c4, a point in the hull's frame
  float followStiffness = 1.0f;  // +0x2b0: the chase direction's slerp per second
  // Which view modes this camera allows (setViewMode 0x579bd0); defaults
  // from the constructor. Chase, FrontChase and FlyBy also need the server's
  // sv.allowExternalViews.
  bool allowInside = true;       // +0x298, CVMInside (1)
  bool allowChase = true;        // +0x299, CVMChase (3)
  bool allowFlyBy = true;        // +0x29a, CVMFlyBy (5); not drawn yet
  bool allowFrontChase = true;   // +0x29d, CVMFrontChase (4)
  float fieldOfView = 1.05f;
  float insideFieldOfView = 1.05f;
};

struct VehicleSeat {
  int index = 0;              // the network id is the vehicle's + this
  std::string templateName;   // the seat's control object
  std::string seatObject;     // `seatInformation`'s object
  Mat4 transform = Mat4::identity();  // the soldier's place, from the vehicle's origin
  std::vector<VehicleSeatJoint> joints;
  bool hasSeatInformation = false;
  std::optional<int> cockpitGeometry;
  std::string animationSystem;  // `seatAnimationSystem`
  std::optional<VehicleSeatCamera> camera;
  // The seat's GenericFireArm templates in tree order, not counting a nested
  // seat's. VehicleHudComp::getGuiIndex (Linux 0x5bf4c0) takes the seat's
  // weapon 0, then weapon 1; that the engine's weapon order is this tree
  // order is inferred (USTNK_M1A2: barrel, then coaxial).
  std::vector<std::string> weapons;
};

// The seats of a vehicle template, in network order; empty when the template is
// unknown or has none.
std::vector<VehicleSeat> vehicleSeats(const Registry& registry, std::string_view vehicle);

}  // namespace obf2::game
