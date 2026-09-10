#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/gfx/device.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/texture/dds.h"

namespace obf2::gfx {

// The off-screen buffer the ground's light is drawn into before anything is
// lit by it. Declared here, defined in `obf2/gfx/terrain_light.h`.
class TerrainLightBuffer;

// Geometry uploaded into video memory.
struct GpuMesh {
  // One draw call: a range of indices plus its texture.
  struct Range {
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    SDL_GPUTexture* texture = nullptr;   // base colour; nullptr -> placeholder
    SDL_GPUTexture* lightmap = nullptr;  // baked lighting; nullptr -> white
    SDL_GPUTexture* detail = nullptr;    // fine structure, tiled
    // Whether the detail is multiplied into the base colour. True for a mesh
    // material with a `Detail` channel, false for the terrain, whose slot 2 is
    // a weight map and not a colour.
    bool detailMultiply = false;
    // Cut the surface out by the base map's alpha. The material's `alphaMode`
    // says so: 0 or 2 are the only values in the game's static meshes, and 2 is
    // what leaves, fences and grates carry.
    bool alphaTest = false;
    // Drawn as leaves: its own shader in the original, and its own two colours
    // (`obf2::mesh::markVegetationLeaves`).
    bool leaf = false;
    // The material's normal map, and whether it is read with the tiling UV set
    // (`NDetail`) or the base map's (`NBase`). With one bound the surface is
    // lit per pixel, which is the path the original takes for exactly these
    // materials.
    SDL_GPUTexture* normalMap = nullptr;
    bool normalOnDetailUv = true;
    // The grime the base colour is multiplied by, and the cracks lerped over
    // it. 1368 of the game's materials name a Dirt channel, 368 a Crack.
    SDL_GPUTexture* dirt = nullptr;
    SDL_GPUTexture* crack = nullptr;
    // A terrain patch's two chart maps — `Detailmaps/txCCxRR_1.dds` and
    // `_2.dds`. Three channels each, six in all, and they say which of the
    // level's six terrain materials owns which texel of this patch.
    SDL_GPUTexture* chartA = nullptr;
    SDL_GPUTexture* chartB = nullptr;
  };

  // Whether the geometry carried the light map's own UV set. Without it a baked
  // light map cannot be sampled at all, whatever the level's atlas says.
  bool hasLightmapUv = false;

  SDL_GPUBuffer* vertices = nullptr;
  SDL_GPUBuffer* indices = nullptr;
  std::vector<Range> ranges;
  std::vector<SDL_GPUTexture*> ownedTextures;

  // The bounding sphere in the mesh's local coordinates — for culling what is
  // not visible. A sphere rather than a box: the test is four times cheaper and
  // it lets only a handful of extra objects through.
  Vec3f boundsCenter;
  float boundsRadius = 0.0f;
};

// Mesh rendering. Which slot of a BF2 material holds what is named by its own
// technique, and `obf2::mesh::materialLayout` reads it. The base colour and the
// detail are drawn (base * detail, as the game's shader does); the dirt, the
// crack and the normal maps are not — the last need a tangent frame we do not
// build yet.
class MeshRenderer {
 public:
  // How to find and read a texture by the name in a material. Made a callback
  // so that gfx knows nothing about the VFS and the game's archives.
  using TextureResolver =
      std::function<std::optional<texture::Texture>(const std::string& mapName)>;

  ~MeshRenderer();
  MeshRenderer(const MeshRenderer&) = delete;
  MeshRenderer& operator=(const MeshRenderer&) = delete;

  static std::unique_ptr<MeshRenderer> create(Device& device, std::string* error = nullptr);

  std::optional<GpuMesh> upload(const mesh::RenderMesh& source, const TextureResolver& resolve,
                                std::string* error = nullptr);
  void release(GpuMesh& gpuMesh);

  // A texture that belongs to no one mesh — a page of the level's light map
  // atlas, which many placements share. The renderer keeps it and frees it with
  // itself; nullptr when the upload failed.
  SDL_GPUTexture* uploadSharedTexture(const texture::Texture& source);

  // A full pass: clear the colour and the depth, then draw every range.
  void render(const Frame& frame, const GpuMesh& gpuMesh, const Mat4& modelViewProjection,
              Color clearColor);

  // One mesh placed into the world by its own matrix. Identical geometry (and on
  // a level that is hundreds of identical buildings) is uploaded once and drawn
  // as many times as it was placed.
  struct DrawItem {
    const GpuMesh* mesh = nullptr;
    Mat4 transform;
    // The overlay's tint. In the HUD this is `setNodeColor`: the game multiplies
    // a node's texture by it, and without it the yellow captions, the tabs'
    // highlight and the coloured bars all come out plain white.
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // A road is a skin on the terrain, not geometry of its own: it is lifted a
    // centimetre, blended by its texture's alpha and does not write depth.
    // Drawn like everything else it fights the terrain for the same pixels.
    bool road = false;
    // How hard a road's markings sit over the tiling surface under them
    // (`RoadTemplate.SetBlendFactor`). Only read when `road`.
    float roadBlendFactor = 1.0f;
    // The sky dome. Drawn first, unlit, unfogged and with no depth at all: it
    // is the background everything else is painted over. The caller places it
    // around the camera.
    bool sky = false;
    // The object's baked light map: the level's atlas page, and the window into
    // it as `LightMapOffset` — xy scale, zw offset. Per placement, not per
    // mesh: the same building has a different window in every copy. A zero
    // scale means this object has none, which is normal for vegetation.
    SDL_GPUTexture* lightmap = nullptr;
    float lightmapOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  };

  // The fog comes from the level's data (Sky.con). fogEnd == 0 disables it.
  struct Fog {
    Color color;
    float start = 0.0f;
    float end = 0.0f;
    // `Renderer.fogStartEndAndBase`'s third and fourth numbers: the slope of
    // the near ramp, and the floor it cannot fall below.
    float base = 0.0f;
    float floorVisibility = 1.0f;
  };
  void setFog(const Fog& fog) { fog_ = fog; }

  // The texture-filtering setting, 1 to 3. What each level means is the
  // preamble the engine writes for the shader compiler — see the definition.
  void setTextureFiltering(int quality);

  // How many times the detail texture repeats over a terrain patch.
  void setDetailTiling(float tiles) { detailTiling_ = tiles; }

  // The ground's own structure. The colour map has about two texels to the
  // metre, so the game tiles one texture per level over the terrain from three
  // directions — `Levels/<name>/lowdetailtexture.dds`
  // (`Shaders_client.zip:TerrainShader_Shared.fx:244`). Every patch also
  // carries a map of how much of it shows where, and that one rides in the
  // range's third slot.
  //
  // `sideTiling` and `topTiling` are `terrain.farSideTiling` and
  // `farTopTilingHi` from Terrain.con, `yOffset` is `terrain.farYOffset`, and
  // `componentSize` is `terrain.lowDetailmapSize` — the per-patch map's own
  // size, which the half-texel correction needs. A null texture leaves the
  // ground as the colour map alone.
  void setTerrainDetail(SDL_GPUTexture* lowDetail, const float sideTiling[2], float topTiling,
                        float yOffset, int componentSize);

  // The ground close up. Six materials per level, each a texture tiled far
  // harder than the low-detail one, and a patch's chart maps weight them: the
  // game draws the terrain once per material with `vComponentsel` picking a
  // channel, and the six weights sum to one
  // (`Shaders_client.zip:TerrainShader_Hi.fx:87`, and measured over Karkand's
  // maps — docs/formats/terraindata.md). We take the six in one pass instead,
  // which is the same sum.
  //
  // Where they come from and what the four tilings mean: `terraindata.raw`,
  // the material table of docs/formats/terraindata.md. A material marked
  // tri-planar is drawn from three directions like the low detail, and the
  // rest only from above.
  struct TerrainMaterial {
    SDL_GPUTexture* texture = nullptr;
    float sideTiling[2] = {2.0f, 2.0f};
    float topTiling = 32.0f;
    float yOffset = 0.0f;
    bool triPlanar = false;
  };
  //
  // `chartSize` is the chart maps' own size in texels, for the half-texel
  // correction. It is not `terrain.detailmapSize` from the level's `.con`: the
  // engine asks the loaded texture (`RendDX9.dll`, 0x100d9c30, the DETAILTEX
  // push walks the patches until one has a map and takes its width), and on
  // Karkand the two disagree — the `.con` says 512 and the files are 256.
  static constexpr int kTerrainMaterials = 6;
  void setTerrainMaterials(const TerrainMaterial materials[kTerrainMaterials], int chartSize);

  // The two colours the terrain's baked light map is multiplied by, as the
  // level's Sky.con writes them: `terrain.sunColor` and `terrain.GIColor`.
  //
  // The engine does not hand them to the shader whole. `Terrain::setSunColor`
  // (`RendDX9.dll`, 0x100db420) keeps `saturate(colour * 0.25)` and
  // `Terrain::setGIColor` (0x100db520) keeps `saturate(colour * 0.5)`; the
  // matching getters multiply back by 4 and by 2 (0x100db620, 0x100e2fa0), so
  // those are storage scales, not tints. What is stored is what is uploaded —
  // `Terrain`'s per-frame constant push reads exactly those fields into the
  // SUNCOLOR and GICOLOR handles (0x100d9c30, fields +0x2e4 and +0x2f0).
  //
  // The shader's own factors then undo them: `4 * accum.a * vSunColor` and
  // `2 * accum.rgb` over a buffer that holds `saturate(2 * lightmap.b *
  // vGIColor) * 0.5`. So the ground is lit by one times the level's numbers,
  // and the quarter is what keeps a level like Highway Tampa — whose sun is
  // 2.34/1.72/0.56 — inside a constant register at all.
  //
  // We store what the engine uploads, because that is what the shader reads.
  // The clamp is the engine's too, and it is per component.
  void setTerrainLighting(Color sun, Color sky) {
    auto clamp01 = [](float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); };
    terrainSun_ = Color{clamp01(sun.r * 0.25f), clamp01(sun.g * 0.25f), clamp01(sun.b * 0.25f),
                        1.0f};
    terrainSky_ = Color{clamp01(sky.r * 0.5f), clamp01(sky.g * 0.5f), clamp01(sky.b * 0.5f), 1.0f};
  }

  // Where the eye is, in world coordinates. Only the specular needs it, and
  // only because a highlight depends on who is looking.
  void setCameraPosition(Vec3f position) { cameraPosition_ = position; }

  // The specular highlight: `Lightmanager.staticSpecularColor` as the level
  // writes it, and `StaticGloss`. Both were measured in a frame dump of the
  // original — the colour reaches the pixel stage whole and the gloss is the
  // engine's 0.2 unless a material overrides it, which we do not read yet
  // (`GeometryTemplate.setSpecularStaticGloss`).
  void setStaticSpecular(Color color, float gloss) {
    staticSpecular_ = color;
    staticGloss_ = gloss;
  }

  // What a leaf is lit by: `Lightmanager.treeSunColor` and `treeAmbientColor`
  // as the level's Sky.con writes them. The engine halves the sun on the way
  // into the shader and the shader doubles it back — measured in a frame dump
  // of the original, docs/formats/shaders.md — so these go in whole.
  void setVegetationLighting(Color sun, Color ambient) {
    treeSun_ = sun;
    treeAmbient_ = ambient;
  }

  // How the level lights everything that is not terrain: the `Lightmanager.*`
  // block of its Sky.con. `direction` is the way the sun's light travels, as
  // the data gives it — negative Y on every level.
  void setStaticLighting(Color sun, Color sky, Vec3f direction, Color point) {
    staticSun_ = sun;
    staticSky_ = sky;
    sunDirection_ = direction;
    pointColor_ = point;
  }

  void renderScene(const Frame& frame, const std::vector<DrawItem>& items,
                   const Mat4& viewProjection, Color clearColor);

  // How many instances were drawn and how many culled in the last frame.
  int drawnLastFrame() const { return drawn_; }
  int culledLastFrame() const { return culled_; }

  // The interface pass: no depth, with alpha blending and without lighting.
  // The vertex coordinates are already in NDC, so no matrix is needed.
  // clear = false keeps what is already drawn: that is how the HUD lands over the scene.
  void renderOverlay(const Frame& frame, const std::vector<DrawItem>& items, Color clearColor,
                     bool clear = true);

 private:
  MeshRenderer() = default;
  SDL_GPUTexture* uploadTexture(const texture::Texture& source);

  Device* device_ = nullptr;
  SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
  SDL_GPUGraphicsPipeline* roadPipeline_ = nullptr;
  SDL_GPUGraphicsPipeline* skyPipeline_ = nullptr;
  SDL_GPUGraphicsPipeline* overlayPipeline_ = nullptr;
  SDL_GPUSampler* sampler_ = nullptr;
  // A separate sampler for the interface: there a texture is never tiled, and
  // repeating at a quad's edge drags in the opposite edge and leaves a
  // one-pixel dark line.
  SDL_GPUSampler* overlaySampler_ = nullptr;
  // The normal map's own: the lowest filtering level takes its mips point-wise.
  SDL_GPUSampler* normalSampler_ = nullptr;
  SDL_GPUTexture* placeholder_ = nullptr;  // a white 1x1 for materials with no texture
  // The ground's light, filled once per frame before the scene is drawn. Null
  // when the device would not give us the pass; then the terrain falls back to
  // reading its light map directly and the roads to the static-mesh formula,
  // which is where they were before.
  std::unique_ptr<TerrainLightBuffer> terrainLight_;
  std::vector<SDL_GPUTexture*> sharedTextures_;
  Fog fog_;
  // Already scaled the way the engine stores them — see setTerrainLighting.
  Color terrainSun_{0.25f, 0.25f, 0.25f, 1.0f};
  Color terrainSky_{0.3f, 0.35f, 0.45f, 1.0f};
  Color staticSun_{0.8f, 0.8f, 0.8f, 1.0f};
  Color staticSky_{0.3f, 0.35f, 0.4f, 1.0f};
  Vec3f sunDirection_{-0.26f, -0.80f, -0.54f};
  Color pointColor_{0.0f, 0.0f, 0.0f, 1.0f};
  Vec3f cameraPosition_{0.0f, 0.0f, 0.0f};
  Color staticSpecular_{0.65f, 0.60f, 0.52f, 1.0f};
  float staticGloss_ = 0.2f;
  Color treeSun_{0.7f, 0.6f, 0.5f, 1.0f};
  Color treeAmbient_{0.2f, 0.25f, 0.3f, 1.0f};
  float detailTiling_ = 16.0f;
  SDL_GPUTexture* terrainDetail_ = nullptr;  // owned by the caller's upload, not by us
  float terrainTiling_[4]{5.0f, 5.0f, 24.0f, 0.0f};
  float terrainDetailUv_[2]{1.0f, 0.0f};
  // The sampler for everything that covers its surface exactly once — see
  // `setTextureFiltering`, where it is built beside the repeating one.
  SDL_GPUSampler* clampSampler_ = nullptr;
  TerrainMaterial terrainMaterials_[kTerrainMaterials];
  bool terrainMaterialsReady_ = false;
  float terrainChartUv_[2]{1.0f, 0.0f};
  int drawn_ = 0;
  int culled_ = 0;
};

}  // namespace obf2::gfx
