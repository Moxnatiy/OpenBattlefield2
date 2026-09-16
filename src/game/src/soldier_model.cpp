#include "obf2/game/soldier_model.h"

namespace obf2::game {
namespace {

std::optional<SoldierPart> partOf(const ObjectTemplate& object, int geometry) {
  const std::string_view geometryName = object.text("geometry");
  if (geometryName.empty()) return std::nullopt;
  return SoldierPart{object.name, object.file, std::string(geometryName), geometry};
}

}  // namespace

std::optional<SoldierModel> soldierModel(const Registry& registry, std::string_view soldier,
                                         std::string_view kit) {
  const ObjectTemplate* body = registry.find(soldier);
  if (body == nullptr) return std::nullopt;
  auto bodyPart = partOf(*body, kSoldierThirdPersonGeometry);
  if (!bodyPart) return std::nullopt;

  SoldierModel model;
  model.body = std::move(*bodyPart);
  model.skeleton3p = std::string(body->text("skeleton3p"));
  model.animationSystem3p = std::string(body->text("animationsystem3p"));

  const ObjectTemplate* kitTemplate = kit.empty() ? nullptr : registry.find(kit);
  if (kitTemplate != nullptr) {
    // `ObjectTemplate.geometry.kit` — which piece of the kits' mesh this kit is.
    // The registry files a dotted property under its component, `geometry`.
    if (const Component* geometry = kitTemplate->component("geometry")) {
      const auto found = geometry->properties.find("kit");
      if (found != geometry->properties.end() && !found->second.empty()) {
        if (const auto piece = found->second.back().asInt(0)) {
          model.kit = partOf(*kitTemplate, *piece);
        }
      }
    }

    for (const ChildTemplate& child : kitTemplate->children) {
      const ObjectTemplate* item = registry.find(child.name);
      if (item == nullptr || item->number("itemindex") != 3.0f) continue;
      // The same sub-geometry as the body: the camera sets 1 on the object **and
      // its children** for the third-person view (`Camera::changeSubGeometry`).
      model.weapon = partOf(*item, kSoldierThirdPersonGeometry);
      model.weaponAnimationSystem3p = std::string(item->text("animationsystem3p"));
      break;
    }
  }
  return model;
}

}  // namespace obf2::game
