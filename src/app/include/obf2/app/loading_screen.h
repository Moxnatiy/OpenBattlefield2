#pragma once
// The screen a level loads behind: the game's own `endOfRound.swf`.
//
// When a level starts loading the client sets SwiffHost's loading flag (+0xD0,
// BF2.exe 0x4ec08c) and posts the menu event that brings the end-of-round
// movie up; the movie reads the flag (`Logic.getIsLoading`), the level's name,
// briefing and `Info/loadMap.png`, and the progress (`Client.getLoadProgress`)
// on every frame, and tells the host it is done with `Logic.readyToPlay`
// (docs/research/12-native-flash.md, "The loading screen").
//
// Ours draws it between the phases of our load: `frame` steps the movie by the
// time since the last frame, draws it over the window and presents. The load
// itself is still one thread, so the screen moves only at those points — the
// engine repaints from its load callback up to thirty times a second.
#include <chrono>
#include <string>

#include "obf2/app/menu_movie.h"
#include "obf2/engine/engine.h"
#include "obf2/engine/load_stat.h"
#include "obf2/gfx/device.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/swf/player.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

class LoadingScreen {
 public:
  // The level by directory, the mode (gpm_cq, sp1 …) and its size. The bridge
  // answers from the engine's settings and dictionary like the menu's does.
  bool open(FileSystem& files, engine::Engine& engine, const std::string& level, const std::string& mode,
            int size);
  bool isOpen() const { return movie_.isOpen(); }

  engine::LoadStat& stat() { return stat_; }

  // One frame: pump the window, step the movie, draw it, present.
  void frame(gfx::Device& device, gfx::MeshRenderer& renderer);

  // The load is over: the flag goes off and the movie is stepped until it says
  // `readyToPlay` (its JOIN GAME button), Escape is pressed, the window is
  // closed — or `maxSeconds` pass (0: no limit).
  void finish(gfx::Device& device, gfx::MeshRenderer& renderer, double maxSeconds);
  bool closed() const { return closed_; }

  void release(gfx::MeshRenderer& renderer);

 private:
  MenuMovie movie_;
  swf::Host host_;
  engine::LoadStat stat_;
  bool loading_ = true;
  bool ready_ = false;
  bool closed_ = false;
  bool pressed_ = false;
  double pressX_ = 0.0, pressY_ = 0.0;
  texture::Texture frame_;
  gfx::GpuMesh mesh_;
  bool uploaded_ = false;
  std::chrono::steady_clock::time_point last_{};
};

}  // namespace obf2::app
