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

// Geometry uploaded into video memory.
struct GpuMesh {
  // One draw call: a range of indices plus its texture.
  struct Range {
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    SDL_GPUTexture* texture = nullptr;   // base colour; nullptr -> placeholder
    SDL_GPUTexture* lightmap = nullptr;  // baked lighting; nullptr -> white
    SDL_GPUTexture* detail = nullptr;    // fine structure, tiled
  };

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

// Mesh rendering with a base texture. BF2's materials have up to four slots
// (`_c` base colour, `_de` detail, `_deb` detail normal, `_di`/`_cr` dirt and
// cracks) — only the zeroth is used here; the rest await a proper material
// pipeline.
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
  };

  // The fog comes from the level's data (Sky.con). fogEnd == 0 disables it.
  struct Fog {
    Color color;
    float start = 0.0f;
    float end = 0.0f;
  };
  void setFog(const Fog& fog) { fog_ = fog; }

  // The colours the terrain's baked light map is multiplied by (the level's LightSettings.*).
  // How many times the detail texture repeats over a terrain patch.
  void setDetailTiling(float tiles) { detailTiling_ = tiles; }

  void setTerrainLighting(Color sun, Color sky) {
    terrainSun_ = sun;
    terrainSky_ = sky;
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
  SDL_GPUGraphicsPipeline* overlayPipeline_ = nullptr;
  SDL_GPUSampler* sampler_ = nullptr;
  // A separate sampler for the interface: there a texture is never tiled, and
  // repeating at a quad's edge drags in the opposite edge and leaves a
  // one-pixel dark line.
  SDL_GPUSampler* overlaySampler_ = nullptr;
  SDL_GPUTexture* placeholder_ = nullptr;  // a white 1x1 for materials with no texture
  Fog fog_;
  Color terrainSun_{1.0f, 1.0f, 1.0f, 1.0f};
  Color terrainSky_{0.6f, 0.7f, 0.9f, 1.0f};
  float detailTiling_ = 16.0f;
  int drawn_ = 0;
  int culled_ = 0;
};

}  // namespace obf2::gfx
