#include "obf2/game/vehicle_seats.h"

#include <cctype>

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
  for (const PartPlacement& part : instance->parts) {
    if (!sameText(part.className, "PlayerControlObject")) continue;
    const ObjectTemplate* seat = registry.find(part.templateName);
    // A control object without a networkable has no descriptor and takes no id.
    if (seat == nullptr || seat->property("setNetworkableInfo") == nullptr) continue;
    VehicleSeat out;
    out.index = static_cast<int>(seats.size());
    out.templateName = part.templateName;
    out.transform = part.transform;
    if (const Property* info = seat->property("seatInformation")) {
      out.seatObject = std::string(info->value(0));
      const auto position = vector3(info->value(1));
      const auto rotation = vector3(info->value(2));
      // The named object's place in the tree; the seat itself when none matches.
      Mat4 base = part.transform;
      for (const PartPlacement& other : instance->parts) {
        if (sameText(other.templateName, out.seatObject)) {
          base = other.transform;
          break;
        }
      }
      Mat4 offset = Mat4::identity();
      if (rotation) offset = rotationYawPitchRoll(rotation->x, rotation->y, rotation->z);
      if (position) offset = translation(*position) * offset;
      out.transform = base * offset;
      out.hasSeatInformation = position.has_value();
    }
    out.animationSystem = std::string(seat->text("seatAnimationSystem"));
    seats.push_back(std::move(out));
  }
  return seats;
}

}  // namespace obf2::game
