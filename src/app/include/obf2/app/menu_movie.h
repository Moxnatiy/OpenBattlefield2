#pragma once
// The menu's movie: our player (obf2::swf, gameswf underneath — the library the
// original's SwiffPlayer.dll is built on) behind the calls the menu uses: open,
// the stage size, the mouse, the orders, a step, a frame.
#include <cstdint>
#include <string>
#include <vector>

#include "obf2/engine/engine.h"
#include "obf2/swf/player.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

class MenuMovie {
 public:
  // The host carries the engine's side the movie's bridge answers from.
  bool open(const swf::Host& host, const std::string& path);
  bool isOpen() const;
  std::uint32_t width() const;
  std::uint32_t height() const;

  // Window points to stage pixels: the movie is stretched over the window.
  void toStage(int windowWidth, int windowHeight, double x, double y, double* sx, double* sy) const;
  void mouseMove(double x, double y);
  void mouseButton(double x, double y, bool down);

  std::string takeCommand();
  // One step by the frame's time.
  void advance(double seconds);
  // RGBA, `width × height × 4`.
  const std::vector<std::uint8_t>& render();

 private:
  swf::Player native_;
  double mouseX_ = 0, mouseY_ = 0;
  bool down_ = false;
};

// What a menu movie's bridge answers from the engine: the dictionary, the
// profiles, the levels, the settings objects, the credits — the same for the
// main menu and the loading screen. The engine and the file system must
// outlive the host (its functions keep references to them).
swf::Host makeMenuHost(FileSystem& files, engine::Engine& engine);

}  // namespace obf2::app
