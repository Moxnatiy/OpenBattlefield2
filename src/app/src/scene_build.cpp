#include "obf2/app/scene_build.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "obf2/core/parallel.h"
#include "obf2/game/object_mesh.h"

namespace obf2::app {
namespace {

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

LevelScene buildLevelScene(FileSystem& files, level::Level& level, Scene& scene) {
  LevelScene out;
  // The level's baked light maps for placed objects. Read once here; every
  // placement below asks it for its own window.
  out.objectLightmaps = level::ObjectLightmaps::load(files, level.name);

  auto patches = level::buildTerrainPatches(level, files);
  std::printf("  terrain patches: %zu of %d (the rest under water, no colour map)\n", patches.size(),
              ((level.primary.size - 1) / level.terrain.patchSize) *
                  ((level.primary.size - 1) / level.terrain.patchSize));
  out.terrainPatches = patches.size();
  for (auto& patch : patches) {
    if (out.firstChartMap.empty()) out.firstChartMap = patch.detailmap;
    scene.add(std::move(patch.geometry), Mat4::identity());
  }

  // Roads: their vertices lie relative to the start point, so we place them by
  // the absolute position from the .con.
  int roadsPlaced = 0;
  for (auto& road : level.roads) {
    if (road.geometry.indices.empty()) continue;
    scene.add(std::move(road.geometry), translation(road.position), true);
    scene.instances.back().roadBlendFactor = road.blendFactor;
    ++roadsPlaced;
  }
  std::printf("  roads in the scene: %d\n", roadsPlaced);
  scene.add(level::buildWaterPlane(level), Mat4::identity());

  // The sky dome. Its place is the camera's, so it is not a scene instance:
  // it is uploaded on its own and put into the draw list every frame.
  if (auto dome = level::buildSkyDome(level, files)) {
    out.skyDome = std::move(*dome);
    std::printf("  sky: %s, texture %s, rotation %.0f\n", level.sky.domeTemplate.c_str(),
                level.sky.texture.c_str(), static_cast<double>(level.sky.domeRotation));
  } else if (!level.sky.domeTemplate.empty()) {
    std::printf("  sky: the dome mesh for `%s` was not found\n", level.sky.domeTemplate.c_str());
  }
  return out;
}

void placeObjects(FileSystem& files, const game::Registry& registry,
                  const std::vector<level::StaticObject>& placement, const SceneOptions& options,
                  LevelScene& levelScene, Scene& scene, const std::function<void()>& keepAlive) {
  std::unordered_map<std::string, int> meshIndexByTemplate;
  std::map<std::string, int> missing;
  int placed = 0;
  int lightmapped = 0;
  const auto placementStarted = std::chrono::steady_clock::now();

  // The geometry first, and on every core. A level places two thousand objects
  // out of three hundred templates, and assembling one — unpacking its meshes
  // from the archives, flattening its tree, moving its parts — is the same work
  // whichever thread does it and looks at nothing another thread writes. The
  // order of the built meshes is the order the names come in, so the scene is the
  // same as when this was a loop.
  {
    std::vector<std::string> names;
    for (const auto& object : placement) {
      if (meshIndexByTemplate.emplace(object.templateName, -1).second) {
        names.push_back(object.templateName);
      }
    }
    std::vector<std::optional<mesh::RenderMesh>> built(names.size());
    parallelFor(names.size(), [&](std::size_t i) {
      built[i] = game::buildObjectMesh(files, registry, names[i], options.geometryIndex,
                                       options.lodIndex, false);
    });
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (!built[i]) continue;
      scene.meshes.push_back(std::move(*built[i]));
      meshIndexByTemplate[names[i]] = static_cast<int>(scene.meshes.size()) - 1;
    }
    if (keepAlive) keepAlive();
  }

  for (const auto& object : placement) {
    // The placement is the longest part of loading. While it goes on, the server
    // has to hear that we are alive.
    if (keepAlive) keepAlive();
    const auto found = meshIndexByTemplate.find(object.templateName);
    if (found == meshIndexByTemplate.end()) continue;
    if (found->second < 0) {
      ++missing[object.templateName];
      continue;
    }

    // Vegetation arrives as a ready matrix, the rest as a position with angles.
    Mat4 transform = object.transform;
    if (!object.hasTransform) {
      transform = translation(object.position);
      if (object.hasRotation) {
        transform = transform *
                    rotationYawPitchRoll(object.rotation.x, object.rotation.y, object.rotation.z);
      }
    }
    Scene::Instance instance;
    instance.mesh = found->second;
    instance.transform = transform;
    // The baked light map is keyed by the template's name and the placement's own
    // position, so it is looked up here rather than with the geometry: the mesh is
    // shared between copies and the light map is not.
    const auto* baked = options.noLightmaps
                            ? nullptr
                            : levelScene.objectLightmaps.find(object.templateName, object.position);
    if (baked != nullptr) {
      instance.lightmapAtlas = baked->atlas;
      instance.lightmapOffset[0] = baked->scaleU;
      instance.lightmapOffset[1] = baked->scaleV;
      instance.lightmapOffset[2] = baked->offsetU;
      instance.lightmapOffset[3] = baked->offsetV;
      ++lightmapped;
    }
    scene.instances.push_back(instance);
    ++placed;
  }

  std::printf("  unique geometry: %zu, placed: %d, without geometry: %zu templates (%.2f s)\n",
              scene.meshes.size() - levelScene.terrainPatches, placed, missing.size(),
              secondsSince(placementStarted));
  std::printf("  baked light maps: %d of %d objects, %d atlas pages\n", lightmapped, placed,
              levelScene.objectLightmaps.atlasCount());
  int shown = 0;
  for (const auto& [name, count] : missing) {
    if (shown++ >= 5) break;
    std::printf("    without geometry: %s (x%d)\n", name.c_str(), count);
  }
}

}  // namespace obf2::app
