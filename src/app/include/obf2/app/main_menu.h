#pragma once
// The screens before a round: the intro fill, the loading screen and the game's
// own Flash menu.
//
// In the original the menu is `mainMenu.swf` and the engine hosts it
// (`Code/BF2/Game/SwiffHost/SwiffHost.cpp`), while the intro and the loading
// screen are drawn by the engine itself out of the level's `Info/<level>.desc`
// (`Game/MenuLogic/LoadGameLogic.cpp`). The movie cannot start a level by
// itself: it asks, and the asking goes through the console, the same path our own
// menu used (docs/functions/menu-bridge.md, docs/research/11-ruffle-menu.md).
#include <optional>
#include <string>
#include <vector>

#include "obf2/app/render_context.h"
#include "obf2/app/scene.h"
#include "obf2/engine/engine.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

#if OBF2_HAVE_FLASH
#include "obf2/flash/movie.h"
#endif

namespace obf2::app {

class MainMenu {
 public:
  struct Options {
    // `--screen menu|loading`: skip straight to a screen, for a deterministic
    // screenshot.
    std::string screen;
    // `--flash <file.swf>`: show another movie instead of the game's own.
    std::string flashSwf;
    // The synthetic mouse: `--mouse`, `--click` and `--click-at <frame>:<x>:<y>`.
    // The menu leads the player through several steps, and one click will not get
    // through it, so the clicks are a schedule.
    float mouseX = -1.0f, mouseY = -1.0f;
    bool click = false;
    struct Click {
      int frame;
      float x, y;
    };
    std::vector<Click> clicks;
    int width = 800, height = 600;
  };

  // Boots the engine (the profile, the levels, the localisation), binds the
  // console commands the menu's buttons run, and puts the intro fill and the
  // loading screen into the scene.
  void boot(FileSystem& files, engine::Engine& engine, const std::filesystem::path& modDir,
            Scene& scene, const Options& options);

  // Opens the movie the original opens. `--flash` only overrides which one.
  void openMovie(FileSystem& files, const Options& options);

  // The movie's current frame, handed to the texture resolver under `#flash` —
  // like the combat-area hatch, a picture we make rather than a file.
  const std::optional<texture::Texture>& flashFrame() const { return flash_; }

  // One frame of the menu: the engine's own screens, the movie's input and its
  // orders. Gives back what to draw over the frame.
  std::vector<gfx::MeshRenderer::DrawItem> frame(engine::Engine& engine, gfx::Device& device,
                                                 gfx::MeshRenderer& renderer,
                                                 const TextureResolver& resolve,
                                                 const gfx::Device::InputState& input,
                                                 int frameNumber, const Options& options,
                                                 const std::vector<gfx::GpuMesh>& sceneMeshes,
                                                 const std::vector<bool>& sceneOk);

  // What the menu asked the game for.
  const std::string& requestedLevel() const { return requestedLevel_; }
  bool quit() const { return quit_; }

  void release(gfx::MeshRenderer& renderer);

 private:
  int introQuad_ = -1;
  int loadingQuad_ = -1;
  std::vector<int> loadingTextQuads_;
  std::string requestedLevel_;
  bool quit_ = false;

  // The background under the movie. `mainMenu.swf` holds no reference to
  // `images/background/` — the engine draws it, and Flash lands on top with a
  // transparent stage.
  std::string background_;
  std::optional<texture::Texture> flash_;
  gfx::GpuMesh backgroundMesh_;
  bool backgroundReady_ = false;
  gfx::GpuMesh flashMesh_;
  bool flashUploaded_ = false;
  // A click is sent as two events: down first, and the release on the next frame,
  // **in the same place**. Flash does not separate them when both come at once,
  // and by the next frame the cursor is elsewhere — which Flash counts as
  // "released outside the button", so the button never fires.
  bool pressed_ = false;
  double pressX_ = 0.0, pressY_ = 0.0;
#if OBF2_HAVE_FLASH
  flash::Movie movie_;
#endif
};

}  // namespace obf2::app
