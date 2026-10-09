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
  // The kit's weapon in the asked `itemIndex` slot, and its animation system for
  // the upper body. Which one is out is the soldier state's 0x1000.
  std::optional<SoldierPart> weapon;
  std::string weaponAnimationSystem3p;

  // First person: the player's own arms and weapon. The body's and the weapon's
  // sub-geometry is `kSoldierFirstPersonGeometry`, and both are posed on the
  // first-person skeleton, driven by the soldier's and the weapon's
  // `animationSystem1P` the way the third-person pair drives the other skeleton.
  // The kit has no first-person piece: it is a third-person mesh on the other
  // skeleton's bones.
  std::string skeleton1p;         // `ObjectTemplate.skeleton1P`
  std::string animationSystem1p;  // `ObjectTemplate.animationSystem1P` (only swimming)
  SoldierPart body1p;
  std::optional<SoldierPart> weapon1p;
  std::string weaponAnimationSystem1p;
};

// The sub-geometry anything held or worn is drawn with from outside, and it is
// the engine's own number: `Camera::changeSubGeometry` (Linux server 0x5788b0)
// walks the object and its children and, for the third-person message, calls the
// geometry's setter (`IGeometry` vtable +0xe0) with a literal **1** and its
// second one (+0xf8) with -1 (0x578b4a). The first-person branch asks the object
// instead (`IObject` vtable +0x1e0 and +0x1d8). Objects carrying a single
// sub-geometry are skipped, which is why a vehicle needs none of this.
//
// The data agrees on both counts: a soldier's 1 is textured `*_3p_*` plus the
// head where 0 is `1p_*`, and a weapon's 1 is the low-detail model (the M4's is
// 998 triangles against 4253, and it is the one textured `usrif_m4_mini_c.dds`)
// where 0 carries the scope blur — `mesh_info`.
inline constexpr int kSoldierThirdPersonGeometry = 1;

// The client says the same from its side: the soldier's animation update
// (`BF2.exe` 0x61f630) hands the soldier's geometry its view number, +0x2b8 —
// 0 in first person, 1 in third — through the geometry's +0x6c (0x61f680), and
// the weapon's the same (0x61f8e9). So 0 is the first-person one.
inline constexpr int kSoldierFirstPersonGeometry = 0;

// `weaponItem` is the `itemIndex` of the weapon he has out — the soldier state's
// 0x1000 (soldier_state.h). Slot 3, the kit's main weapon, stands in where that is
// not known yet: a soldier whose full record has not been read.
std::optional<SoldierModel> soldierModel(const Registry& registry, std::string_view soldier,
                                         std::string_view kit, int weaponItem = 3);

}  // namespace obf2::game
