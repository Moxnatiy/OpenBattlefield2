#include "obf2/app/render_context.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>

#include "obf2/core/parallel.h"
#include "obf2/core/path.h"

namespace obf2::app {

const std::optional<texture::Texture>* TextureCache::cache(const std::string& path) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (const auto cached = textures_.find(path); cached != textures_.end()) {
      return &cached->second;
    }
  }

  auto bytes = files_.read(path);
  // In the game the paths point at `.tga` while the archives hold `.dds` — that is
  // how it is with the level's map, for instance: BF2.exe asks for
  // `Levels/%s/Hud/Minimap/ingameMap.tga` while client.zip has only
  // `ingameMap.dds`. So we try the compressed variant by the same path.
  std::string swapped;
  if (path.size() > 4 && path.compare(path.size() - 4, 4, ".tga") == 0) {
    swapped = path.substr(0, path.size() - 4) + ".dds";
    if (!bytes) bytes = files_.read(swapped);
  }
  if (!bytes) bytes = files_.read(joinAssetPath("objects", path));
  if (!bytes && !swapped.empty()) bytes = files_.read(joinAssetPath("objects", swapped));
  // The HUD's paths are counted from the interface texture directory — the same as
  // is visible in `nametags.setTexture Menu/HUD/Texture/...`.
  if (!bytes) bytes = files_.read(joinAssetPath("menu/hud/texture", path));
  if (!bytes && !swapped.empty()) bytes = files_.read(joinAssetPath("menu/hud/texture", swapped));

  std::optional<texture::Texture> decoded;
  std::string textureError;
  if (bytes) decoded = texture::loadImage(*bytes, &textureError);

  const std::lock_guard<std::mutex> lock(mutex_);
  // Two threads may have asked for the same texture at once; the first one in
  // wins and the second's copy is dropped.
  const auto [where, inserted] = textures_.emplace(path, std::move(decoded));
  if (inserted) {
    if (where->second) {
      ++loaded_;
    } else {
      ++missing_;
      if (bytes && missing_ <= 6) {
        std::printf("    the texture does not read: %s (%s)\n", path.c_str(), textureError.c_str());
      }
    }
  }
  return &where->second;
}

RenderContext createRenderContext(int width, int height, std::string* error) {
  RenderContext out;
  gfx::WindowDesc desc;
  desc.title = "OpenBattlefield2";
  desc.width = width;
  desc.height = height;
  out.device = gfx::Device::create(desc, error);
  if (!out.device) return out;
  {
    // In pixels, not in points: on a Retina display the two differ by two, and
    // what we draw into is the pixels.
    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSizeInPixels(out.device->window(), &windowWidth, &windowHeight);
    std::printf("GPU backend: %s | window %dx%d (asked for %dx%d)\n",
                std::string(out.device->driver()).c_str(), windowWidth, windowHeight, width,
                height);
  }
  out.renderer = gfx::MeshRenderer::create(*out.device, error);
  if (!out.renderer) out.device.reset();
  return out;
}

void applyLevelLighting(gfx::MeshRenderer& renderer, const level::Level& level, bool topDown,
                        int textureFilteringQuality) {
  // In map mode the fog only gets in the way: from above it eats the whole level.
  const float fogEnd = topDown ? 0.0f : level.terrain.fogEnd;
  renderer.setFog(gfx::MeshRenderer::Fog{
      gfx::Color{level.terrain.fogColor.x, level.terrain.fogColor.y, level.terrain.fogColor.z,
                 1.0f},
      level.terrain.fogStart, fogEnd, level.terrain.fogBase, level.terrain.fogFloor});
  renderer.setTerrainLighting(
      gfx::Color{level.terrain.terrainSunColor.x, level.terrain.terrainSunColor.y,
                 level.terrain.terrainSunColor.z, 1.0f},
      gfx::Color{level.terrain.terrainSkyColor.x, level.terrain.terrainSkyColor.y,
                 level.terrain.terrainSkyColor.z, 1.0f});
  const level::Lighting& lighting = level.lighting;
  // The world's samplers follow the profile's texture-filtering level.
  renderer.setTextureFiltering(textureFilteringQuality);
  renderer.setStaticSpecular(
      gfx::Color{lighting.staticSpecularColor.x, lighting.staticSpecularColor.y,
                 lighting.staticSpecularColor.z, 1.0f},
      // `StaticGloss` as the engine gives it, measured in a frame dump of the
      // original (`psc c2` = 0.2 on 678 draws of one Karkand frame). No level
      // sets it; a material can, and we do not read that yet.
      0.2f);
  renderer.setVegetationLighting(
      gfx::Color{lighting.treeSunColor.x, lighting.treeSunColor.y, lighting.treeSunColor.z, 1.0f},
      gfx::Color{lighting.treeAmbientColor.x, lighting.treeAmbientColor.y,
                 lighting.treeAmbientColor.z, 1.0f});
  renderer.setStaticLighting(
      gfx::Color{lighting.staticSunColor.x, lighting.staticSunColor.y, lighting.staticSunColor.z,
                 1.0f},
      gfx::Color{lighting.staticSkyColor.x, lighting.staticSkyColor.y, lighting.staticSkyColor.z,
                 1.0f},
      lighting.sunDirection,
      gfx::Color{lighting.singlePointColor.x, lighting.singlePointColor.y,
                 lighting.singlePointColor.z, 1.0f});
  std::printf("  static lighting: sun %.2f/%.2f/%.2f, sky %.2f/%.2f/%.2f, from %.2f/%.2f/%.2f\n",
              lighting.staticSunColor.x, lighting.staticSunColor.y, lighting.staticSunColor.z,
              lighting.staticSkyColor.x, lighting.staticSkyColor.y, lighting.staticSkyColor.z,
              lighting.sunDirection.x, lighting.sunDirection.y, lighting.sunDirection.z);
}

UploadedScene uploadScene(gfx::MeshRenderer& renderer, TextureCache& textures,
                          const TextureResolver& resolve, const Scene& scene,
                          const LevelScene& levelScene, const level::Level* level,
                          FileSystem& files) {
  UploadedScene out;
  out.meshes.resize(scene.meshes.size());
  out.ok.assign(scene.meshes.size(), false);

  // Every texture the scene names, unpacked and decoded before the upload
  // begins. The upload itself has to stay on this thread — SDL's GPU device is
  // not shared — but the work in front of it is inflate and a DXT header per
  // file, and that is what the other cores are for. Afterwards the loop below
  // finds all of them in the cache.
  {
    std::vector<std::string> wanted;
    std::unordered_set<std::string> seen;
    for (const mesh::RenderMesh& piece : scene.meshes) {
      for (const mesh::DrawRange& range : piece.ranges) {
        for (const std::string& map : range.maps) {
          // The `#` names are colours rather than files, and the cache is not
          // where they come from.
          if (map.empty() || map.front() == '#') continue;
          if (seen.insert(map).second) wanted.push_back(normalizeAssetPath(map));
        }
      }
    }
    parallelFor(wanted.size(), [&](std::size_t i) { textures.cache(wanted[i]); });
  }

  std::string error;
  for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
    auto uploaded = renderer.upload(scene.meshes[i], resolve, &error);
    if (!uploaded) continue;
    out.meshes[i] = *uploaded;
    out.ok[i] = true;
    out.triangles += static_cast<long long>(scene.meshes[i].indices.size() / 3);
  }

  // The atlas pages, uploaded once. Only the pages some object actually points
  // at are loaded; a level has up to 21 and a small map uses few of them.
  out.lightmapPages.assign(
      static_cast<std::size_t>(std::max(levelScene.objectLightmaps.atlasCount(), 0)), nullptr);
  {
    std::vector<bool> wanted(out.lightmapPages.size(), false);
    for (const Scene::Instance& instance : scene.instances) {
      if (instance.lightmapAtlas >= 0 &&
          static_cast<std::size_t>(instance.lightmapAtlas) < wanted.size()) {
        wanted[static_cast<std::size_t>(instance.lightmapAtlas)] = true;
      }
    }
    int loaded = 0;
    for (std::size_t i = 0; i < out.lightmapPages.size(); ++i) {
      if (!wanted[i]) continue;
      if (auto decoded = resolve(levelScene.objectLightmaps.atlasPath(static_cast<int>(i)))) {
        out.lightmapPages[i] = renderer.uploadSharedTexture(*decoded);
        if (out.lightmapPages[i] != nullptr) ++loaded;
      }
    }
    if (!out.lightmapPages.empty()) {
      std::printf("  light map atlas pages loaded: %d of %zu\n", loaded, out.lightmapPages.size());
    }
  }

  // The ground's own structure: one texture for the whole level, tiled over the
  // terrain from three directions. Where it shows is a map per patch, and that
  // one travels with the patch's geometry.
  if (level != nullptr) {
    const std::string lowDetail = level::lowDetailTexturePath(*level, files);
    SDL_GPUTexture* uploaded = nullptr;
    if (!lowDetail.empty()) {
      if (auto decoded = resolve(lowDetail)) uploaded = renderer.uploadSharedTexture(*decoded);
    }
    renderer.setTerrainDetail(uploaded, level->terrain.farSideTiling,
                              level->terrain.farTopTilingHi, level->terrain.farYOffset,
                              level->terrain.lowDetailmapSize);
    std::printf("  terrain detail: %s, tiling %.0f/%.0f side, %.0f top\n",
                uploaded != nullptr ? lowDetail.c_str() : "none",
                static_cast<double>(level->terrain.farSideTiling[0]),
                static_cast<double>(level->terrain.farSideTiling[1]),
                static_cast<double>(level->terrain.farTopTilingHi));
    // And the near detail: the six materials out of the compiled terrain, and
    // the chart maps that say which of them owns which texel of a patch.
    gfx::MeshRenderer::TerrainMaterial materials[gfx::MeshRenderer::kTerrainMaterials];
    const std::size_t count =
        std::min(level->terrain.materials.size(),
                 static_cast<std::size_t>(gfx::MeshRenderer::kTerrainMaterials));
    for (std::size_t i = 0; i < count; ++i) {
      const level::TerrainMaterial& material = level->terrain.materials[i];
      // The file names the texture without an extension, and the archives hold
      // it as `.dds` like every other texture in the game.
      if (auto decoded = resolve(material.texture + ".dds")) {
        materials[i].texture = renderer.uploadSharedTexture(*decoded);
      }
      materials[i].sideTiling[0] = material.sideTilingX;
      materials[i].sideTiling[1] = material.sideTilingY;
      materials[i].topTiling = material.topTiling;
      materials[i].yOffset = material.yOffset;
      materials[i].triPlanar = material.triPlanar;
      std::printf("  terrain material %zu: %-44s top %g, side %g/%g%s%s\n", i,
                  material.texture.c_str(), static_cast<double>(material.topTiling),
                  static_cast<double>(material.sideTilingX),
                  static_cast<double>(material.sideTilingY),
                  material.triPlanar ? ", tri-planar" : "",
                  materials[i].texture != nullptr ? "" : ", not loaded");
    }
    // The chart maps' size for the half-texel correction, taken from the first
    // patch that has one — the engine takes it the same way (`RendDX9.dll`,
    // 0x100d9c30) rather than from `terrain.detailmapSize`, which on Karkand says
    // 512 where the files are 256.
    int chartSize = 0;
    if (!levelScene.firstChartMap.empty()) {
      if (auto decoded = resolve(levelScene.firstChartMap)) {
        chartSize = static_cast<int>(decoded->width);
      }
    }
    renderer.setTerrainMaterials(materials, chartSize);
  }

  if (levelScene.skyDome) {
    if (auto uploaded = renderer.upload(*levelScene.skyDome, resolve, &error)) {
      out.sky = *uploaded;
      out.skyReady = true;
    }
  }
  return out;
}

}  // namespace obf2::app
