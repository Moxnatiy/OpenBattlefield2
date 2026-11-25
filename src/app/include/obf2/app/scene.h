#pragma once
// The world as the renderer takes it: unique geometry plus where each copy of
// it stands.
//
// On a level the same building occurs dozens of times, so the mesh is uploaded
// once and the placement is a list of matrices — the split the engine makes too,
// between a `GeometryTemplate` and the objects that carry it
// (`Code/BF2/Scene/Object/ObjectManager.cpp`).
#include <optional>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/level/level.h"
#include "obf2/level/lightmap_atlas.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::app {

struct Scene {
  // One placement of a piece of geometry. `road` says the renderer must draw it
  // as a skin on the terrain rather than as geometry of its own — what that
  // means is `obf2::gfx`'s business, not ours.
  struct Instance {
    int mesh = -1;
    Mat4 transform;
    bool road = false;
    float roadBlendFactor = 1.0f;
    // Which page of the level's light map atlas this placement is baked into,
    // and its window in it. -1 means the object has no baked light map.
    int lightmapAtlas = -1;
    float lightmapOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Index into `lodChains`, or -1 when the template draws one LOD only.
    int lodChain = -1;
    // With a chain: the light map of each of its LODs, in the chain's order
    // (every LOD has its own, `level::ObjectLightmaps`).
    struct LodLightmap {
      int atlas = -1;
      float offset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };
    std::vector<LodLightmap> lodLightmaps;
  };

  // The LODs of one template left after the quality skip, nearest first, and
  // what picks among them each frame (`game::pickLod`, `RendDX9.dll`
  // 0x100fe5d0). `meshes[0]` is the instance's own `mesh`.
  struct LodChain {
    std::vector<int> meshes;
    std::vector<float> switchDistances;  // meshes.size() - 1 values
    float baseRadius = 0.0f;
  };

  std::vector<mesh::RenderMesh> meshes;
  std::vector<Instance> instances;
  std::vector<LodChain> lodChains;
  Vec3f center;
  float radius = 1.0f;

  void add(mesh::RenderMesh&& geometry, const Mat4& transform, bool road = false) {
    meshes.push_back(std::move(geometry));
    Instance instance;
    instance.mesh = static_cast<int>(meshes.size()) - 1;
    instance.transform = transform;
    instance.road = road;
    instances.push_back(instance);
  }
};

// What a level contributes besides the scene itself.
struct LevelScene {
  // The sky dome is kept apart: everything in the scene has a fixed place, and
  // the dome's place is wherever the camera is.
  std::optional<mesh::RenderMesh> skyDome;
  // The level's baked object light maps, keyed by template name and position.
  level::ObjectLightmaps objectLightmaps;
  // One of the terrain's chart maps, kept for its size alone: the near detail's
  // half-texel correction needs it.
  std::string firstChartMap;
  std::size_t terrainPatches = 0;
};

}  // namespace obf2::app
