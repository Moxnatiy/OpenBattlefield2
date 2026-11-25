#include "obf2/game/vehicle_seats.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "obf2/game/scene.h"

namespace obf2::game {

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

// "x/y/z" as the `.con` writes a vector.
std::optional<Vec3f> vector3(std::string_view text) {
  float values[3] = {0.0f, 0.0f, 0.0f};
  std::size_t at = 0;
  for (int i = 0; i < 3; ++i) {
    const std::size_t end = text.find('/', at);
    const std::string part(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
    if (part.empty()) return std::nullopt;
    char* stop = nullptr;
    values[i] = std::strtof(part.c_str(), &stop);
    if (stop == part.c_str()) return std::nullopt;
    if (end == std::string_view::npos) {
      if (i != 2) return std::nullopt;
      break;
    }
    at = end + 1;
  }
  return Vec3f{values[0], values[1], values[2]};
}

}  // namespace

std::vector<VehicleSeat> vehicleSeats(const Registry& registry, std::string_view vehicle) {
  std::vector<VehicleSeat> seats;
  const auto instance = flattenObject(registry, vehicle);
  if (!instance) return seats;
  struct Ancestor {
    std::size_t partIndex;
    int seat;
  };
  std::vector<Ancestor> path;
  std::vector<std::size_t> anchorParts;
  std::vector<std::vector<VehicleSeatJoint>> jointsAt(instance->parts.size());
  for (const PartPlacement& part : instance->parts) {
    const auto partIndex = static_cast<std::size_t>(&part - instance->parts.data());
    path.resize(static_cast<std::size_t>(part.depth));
    const int parentSeat = path.empty() ? -1 : path.back().seat;
    auto& joints = jointsAt[partIndex];
    if (!path.empty()) joints = jointsAt[path.back().partIndex];
    path.push_back({partIndex, parentSeat});
    const ObjectTemplate* object = registry.find(part.templateName);
    if (!object) continue;
    if (parentSeat >= 0 && sameText(part.className, "GenericFireArm") &&
        static_cast<std::size_t>(parentSeat) < seats.size()) {
      seats[static_cast<std::size_t>(parentSeat)].weapons.push_back(part.templateName);
    }
    if (parentSeat >= 0 && sameText(part.className, "RotationalBundle")) {
      const auto seat = static_cast<unsigned>(parentSeat);
      const auto bundles = vehicleTurretBundles(registry, vehicle, seat);
      const auto bundle = std::ranges::find_if(bundles, [&](const auto& name) { return sameText(name, part.templateName); });
      if (bundle != bundles.end()) {
        const auto parent = path.size()>1 ? instance->parts[path[path.size()-2].partIndex].transform
                                         : Mat4::identity();
        joints.push_back({seat, static_cast<std::size_t>(bundle - bundles.begin()), part.transform,
                          parent, object->number("automaticYawStabilization").value_or(0)!=0,
                          sameText(object->text("setInputToYaw"),"PIMouseLookX"),
                          sameText(object->text("setInputToPitch"),"PIMouseLookY")});
      }
    }
    if (sameText(part.className, "Camera") && parentSeat >= 0 &&
        object->number("cameraId").value_or(0.0f) == 0.0f) {
      auto& seat = seats[static_cast<std::size_t>(parentSeat)];
      if (seat.camera) continue;
      VehicleSeatCamera camera;
      camera.transform = part.transform;
      camera.chaseDistance = object->number("chaseDistance").value_or(camera.chaseDistance);
      camera.chaseAngle = object->number("chaseAngle").value_or(camera.chaseAngle);
      camera.chaseOffset = vector3(object->text("chaseOffset")).value_or(Vec3f{});
      camera.followStiffness = object->number("followStiffness").value_or(camera.followStiffness);
      // ObjectTemplate.CVMChase / CVMFrontChase / CVMFlyBy 0|1 (vehicle .tweak files).
      camera.allowChase = object->number("CVMChase").value_or(1.0f) != 0.0f;
      camera.allowFrontChase = object->number("CVMFrontChase").value_or(1.0f) != 0.0f;
      camera.allowFlyBy = object->number("CVMFlyBy").value_or(1.0f) != 0.0f;
      camera.fieldOfView = std::clamp(object->number("worldFOV").value_or(1.05f), 0.2f, 2.5f);
      const auto insideFov = object->number("insideFOV").value_or(0.0f);
      camera.insideFieldOfView = insideFov > 0.0f ? std::clamp(insideFov, 0.2f, 2.5f)
                                                 : camera.fieldOfView;
      camera.joints = joints;
      seat.camera = std::move(camera);
      continue;
    }
    // A control object without a networkable has no descriptor and takes no id.
    if (!sameText(part.className, "PlayerControlObject") || !object->property("setNetworkableInfo")) continue;
    VehicleSeat out;
    out.index = static_cast<int>(seats.size());
    path.back().seat = out.index;
    out.templateName = part.templateName;
    out.transform = part.transform;
    auto anchorPart = partIndex;
    if (const Property* info = object->property("seatInformation")) {
      out.seatObject = std::string(info->value(0));
      const auto position = vector3(info->value(1));
      const auto rotation = vector3(info->value(2));
      // The named object's place in the tree; the seat itself when none matches.
      Mat4 base = part.transform;
      for (const PartPlacement& other : instance->parts) {
        if (sameText(other.templateName, out.seatObject)) {
          base = other.transform;
          anchorPart = static_cast<std::size_t>(&other - instance->parts.data());
          break;
        }
      }
      Mat4 offset = Mat4::identity();
      if (rotation) offset = rotationYawPitchRoll(rotation->x, rotation->y, rotation->z);
      if (position) offset = translation(*position) * offset;
      out.transform = base * offset;
      out.hasSeatInformation = position.has_value();
      // The Vodnik places the gunner at the fixed cupola base. The soldier's
      // seated yaw follows the tower, while gun elevation moves only the barrel.
      if (sameText(out.seatObject, "JEP_VODNIK_CupolaBase")) {
        const auto tower = std::ranges::find_if(instance->parts, [](const PartPlacement& part) {
          return sameText(part.templateName, "JEP_VODNIK_CupolaTower");
        });
        if (tower != instance->parts.end()) {
          anchorPart = static_cast<std::size_t>(tower - instance->parts.begin());
        }
      }
    }
    out.animationSystem = std::string(object->text("seatAnimationSystem"));
    if (const auto* cockpit = object->property("cockpitSubGeom")) {
      // A zero-based mesh index can be 0: ahe_havoc.bundledmesh geometry 0
      // contains the authored CockpitNoHemiLight interior material.
      if (const auto geometry = cockpit->asInt(0); geometry && *geometry >= 0) {
        out.cockpitGeometry = *geometry;
      }
    }
    anchorParts.push_back(anchorPart);
    seats.push_back(std::move(out));
  }
  for (std::size_t index = 0; index < seats.size(); ++index) {
    seats[index].joints = jointsAt[anchorParts[index]];
    if (!seats[index].joints.empty()) continue;
    const auto anchor = anchorParts[index];
    for (std::size_t i = anchor + 1; i < instance->parts.size() && instance->parts[i].depth > instance->parts[anchor].depth; ++i) {
      const auto* object = registry.find(instance->parts[i].templateName);
      if (object && !object->text("setInputToYaw").empty() && !jointsAt[i].empty() &&
          jointsAt[i].back().seat == index) {
        seats[index].joints = jointsAt[i];
        break;
      }
    }
  }
  return seats;
}

}  // namespace obf2::game
