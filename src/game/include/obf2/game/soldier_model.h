#pragma once
// What a soldier is drawn from, as the game's data assembles it.
//
//   the soldier   `ObjectTemplate.create Soldier us_light_soldier` — its geometry
//                 (a SkinnedMesh), `skeleton3P`, `animationSystem3P`;
//   the kit       `ObjectTemplate.create Kit US_Specops` — `geometry US_Kits` and
//                 `geometry.kit <n>`, the piece of that mesh this kit wears;
//   the weapons   the kit's children (`GenericFireArm`), each with its own
//                 geometry and `animationSystem3P` for the upper body.
//
// Which kit a player wears arrives with the spawn: `CreateKitEvent` and
// `HandlePickupEvent` (bf2_events.h).
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/game/object_template.h"

namespace obf2::game {

// One skinned or bundled mesh to draw, named as the template names it.
struct SoldierPart {
  std::string templateName;
  std::string templateFile;  // the geometry lies next to it, under `meshes/`
  std::string geometryName;
  int geometry = 0;          // the sub-geometry in the mesh file
};

struct SoldierModel {
  std::string skeleton3p;         // `ObjectTemplate.skeleton3P`
  std::string animationSystem3p;  // `ObjectTemplate.animationSystem3P`, the legs
  SoldierPart body;
  std::optional<SoldierPart> kit;
  // The kit's weapon in `itemIndex` slot 3, and its animation system for the
  // upper body. Which weapon is out is in the soldier state's 0x1000; until that is
  // read for other players, slot 3 stands in.
  std::optional<SoldierPart> weapon;
  std::string weaponAnimationSystem3p;
};

// The sub-geometry a soldier's body is drawn with from outside. The soldier meshes
// carry two: 0 is textured `1p_*` (the first-person arms, one LOD) and 1 is
// textured `*_3p_*` plus the head (three LODs) — read from
// `soldiers/mec/meshes/mec_light_soldier.skinnedmesh`. The code in `RendDX9.dll`
// that picks it is not found.
inline constexpr int kSoldierThirdPersonGeometry = 1;

std::optional<SoldierModel> soldierModel(const Registry& registry, std::string_view soldier,
                                         std::string_view kit);

}  // namespace obf2::game
