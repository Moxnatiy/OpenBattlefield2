#pragma once
// The menu's movie behind one face: our native player (obf2::swf, the
// default) or Ruffle (obf2::flash, `--ruffle`, kept only to compare against
// until the native one has replaced it everywhere). Both give the same calls
// the menu uses: open, the stage size, the mouse, the orders, a step, a frame.
#include <cstdint>
#include <string>
#include <vector>

#include "obf2/engine/engine.h"
#include "obf2/swf/player.h"
#include "obf2/vfs/filesystem.h"
#if OBF2_HAVE_FLASH
#include "obf2/flash/movie.h"
#endif

namespace obf2::app {

class MenuMovie {
 public:
  // Ruffle instead of ours — before open.
  void useRuffle(bool ruffle) { ruffle_ = ruffle; }
  bool usingRuffle() const { return ruffle_; }

  // Ours needs the engine's side (the bridge's data); Ruffle reads the files.
  bool open(const swf::Host& host, const std::string& path);
  bool isOpen() const;
  std::uint32_t width() const;
  std::uint32_t height() const;

  // Window points to stage pixels: the movie is stretched over the window.
  void toStage(int windowWidth, int windowHeight, double x, double y, double* sx, double* sy) const;
  void mouseMove(double x, double y);
  void mouseButton(double x, double y, bool down);

  std::string takeCommand();
  // One step: ours by the frame's time, Ruffle by one of its frames.
  void advance(double seconds);
  // RGBA, `width × height × 4`.
  const std::vector<std::uint8_t>& render();

 private:
  bool ruffle_ = false;
  swf::Player native_;
  double mouseX_ = 0, mouseY_ = 0;
  bool down_ = false;
#if OBF2_HAVE_FLASH
  flash::Movie flash_;
#endif
};

// What a menu movie's bridge answers from the engine: the dictionary, the
// profiles, the levels, the settings objects, the credits — the same for the
// main menu and the loading screen. The engine and the file system must
// outlive the host (its functions keep references to them).
swf::Host makeMenuHost(FileSystem& files, engine::Engine& engine);

}  // namespace obf2::app
