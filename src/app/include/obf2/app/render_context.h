#pragma once
// The graphics side of a session: the window, the renderer, the textures and the
// upload of a scene. The engine keeps the same split — `BF2Render` sets the
// device and the world's lighting up and hands the scene to the renderer
// (`Code/BF2/Game/Main/BF2Render.cpp`).
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "obf2/app/scene.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/level.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// What a name in the data resolves to. The `#` names are not files but pictures
// we make ourselves (a colour, the menu's Flash frame, the combat-area hatch), so
// the resolver is handed in rather than built here.
using TextureResolver = std::function<std::optional<texture::Texture>(const std::string&)>;

// Every texture the world and the interface ask for, decoded once.
//
// The engine's paths do not always match the archives: the level's map is asked
// for as `.tga` while `client.zip` holds only the `.dds`, and the HUD's paths are
// counted from the interface texture directory. Each of those is tried in turn.
class TextureCache {
 public:
  explicit TextureCache(FileSystem& files) : files_(files) {}

  // Safe to call from several threads: the lock is held around the map alone,
  // never around the unpacking and the decoding, which is the part worth
  // spreading over the cores. The pointer stays valid however much the cache
  // grows, so a texture that is only being cached is never copied.
  const std::optional<texture::Texture>* cache(const std::string& path);

  int loaded() const { return loaded_; }
  int missing() const { return missing_; }

 private:
  FileSystem& files_;
  std::unordered_map<std::string, std::optional<texture::Texture>> textures_;
  std::mutex mutex_;
  int loaded_ = 0;
  int missing_ = 0;
};

// The window and the renderer. Empty on failure, with the reason in `error`.
struct RenderContext {
  std::unique_ptr<gfx::Device> device;
  std::unique_ptr<gfx::MeshRenderer> renderer;
};
RenderContext createRenderContext(int width, int height, std::string* error);

// The level's own fog, lighting and view: everything the renderer is told once,
// before the first frame. `topDown` takes the fog off — from above it eats the
// whole level.
void applyLevelLighting(gfx::MeshRenderer& renderer, const level::Level& level, bool topDown,
                        int textureFilteringQuality);

// The scene on the card: every mesh uploaded, the light map atlas pages, the
// terrain's detail and materials, and the sky dome.
struct UploadedScene {
  std::vector<gfx::GpuMesh> meshes;
  std::vector<bool> ok;  // per mesh: whether it reached the card
  long long triangles = 0;
  std::vector<SDL_GPUTexture*> lightmapPages;
  gfx::GpuMesh sky;
  bool skyReady = false;
};

UploadedScene uploadScene(gfx::MeshRenderer& renderer, TextureCache& textures,
                          const TextureResolver& resolve, const Scene& scene,
                          const LevelScene& levelScene, const level::Level* level,
                          FileSystem& files);

}  // namespace obf2::app
