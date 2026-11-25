#pragma once
// A level into a scene: the terrain, the roads, the water, the sky dome and every
// placed object with its baked light map.
//
// The engine builds this while loading too — `ObjectManager` creates the objects
// of the placement and the terrain is its own geometry
// (`Code/BF2/Scene/Object/ObjectManager.cpp`).
#include <functional>
#include <vector>

#include "obf2/app/scene.h"
#include "obf2/game/object_template.h"
#include "obf2/level/level.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// What the command line may override about the geometry; the scene itself knows
// nothing about the command line.
struct SceneOptions {
  int geometryIndex = -1;  // -1 = choose by the number of lods
  int lodIndex = 0;
  // `lodIndex` is the quality's skip (`skippedLods`) and the LODs left switch
  // by distance; false: every mesh draws `lodIndex` as asked.
  bool clampLod = false;
  bool noLightmaps = false;
};

// How many leading LODs a mesh drops at load, by the geometry quality (1–3).
// `RendDX9.dll` 0x10119a80 (StaticMeshTemplate, the LOD load): per geometry,
// skip = renderer+0x7e4 + max(0, renderer+0x7ec + template+0x2b8), capped by
// the template's +0xc0 for geometry 0 and +0xc4 for geometry 1 (0 for any
// other), and by lodCount - 1; the skipped LODs are read and thrown away.
// 0x1000fb50 sets +0x7e4/+0x7ec to 1/0 at low, 0/0 at medium, 0/-1 at high.
// +0xc0/+0xc4 are maxSkip3pLods/maxSkipWreckLods (GeometryTemplate ctor
// 0x10192960 sets both to 1); template+0x2b8 is `GeometryTemplate.meshDetailLevel`
// (get/set 0x100b37f8), 0 from the StaticMeshTemplate ctor 0x1011a430 and set
// by no game file, so the +0x7ec term is never above 0. The bundled
// (0x100ea5f0) and skinned (0x10110f40) loads take +0x7e4 alone. The caps, per
// template and per kind, are applied in `game::loadMesh`.
inline int skippedLods(int geometryQuality) { return geometryQuality <= 1 ? 1 : 0; }

// The terrain, the roads, the water and the sky. The level is not const: a
// road's geometry is moved into the scene rather than copied, the way the
// terrain's patches are.
LevelScene buildLevelScene(FileSystem& files, level::Level& level, Scene& scene);

// Every object of the placement: one mesh per template, one instance per copy.
// `keepAlive` is called while it goes on — the placement is the longest part of
// loading, and a server we are connected to has to hear that we are alive.
void placeObjects(FileSystem& files, const game::Registry& registry,
                  const std::vector<level::StaticObject>& placement, const SceneOptions& options,
                  LevelScene& levelScene, Scene& scene,
                  const std::function<void()>& keepAlive = {});

}  // namespace obf2::app
