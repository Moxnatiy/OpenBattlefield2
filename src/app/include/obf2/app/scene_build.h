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
  bool noLightmaps = false;
};

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
