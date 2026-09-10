#pragma once
// The Flash player for the game's menu.
//
// Battlefield 2's menu is `mainMenu.swf` (proven by running the original,
// docs/functions/menu-bridge.md), and it is played by **Ruffle** through the
// thin library `obf2_flash` (docs/research/11-ruffle-menu.md). Here there is
// only a C++ wrapper over its C interface.
//
// A frame arrives as an ordinary RGBA image, and we put it on screen ourselves:
// all graphics in the port go through `obf2::gfx`, and Flash is no exception
// here.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace obf2::flash {

class Movie {
 public:
  Movie() = default;
  ~Movie();
  Movie(const Movie&) = delete;
  Movie& operator=(const Movie&) = delete;

  // Zero dimensions mean taking the stage's size from the movie itself.
  bool open(const std::string& path, std::uint32_t width = 0, std::uint32_t height = 0);

  // The same, for a movie that lives in the game's archives rather than on
  // disk — which is where the game keeps its own: `mainMenu.swf` is inside
  // `Menu_client.zip`, and nothing is unpacked (CLAUDE.md, rule 5). `path` is
  // the name the file manager knows it by, `Menu/External/FlashMenu/mainMenu.swf`,
  // and everything the movie loads next to itself is asked for by that path
  // too — so `setFileReader` has to be set first, or the movie opens without
  // any of its pictures.
  bool openFromMemory(const std::vector<std::byte>& data, const std::string& path,
                      std::uint32_t width = 0, std::uint32_t height = 0);

  // How the player reads a file out of the game's archives. Set once, before
  // opening; `flash` knows nothing about the VFS itself, the same way `gfx`
  // does not.
  using FileReader = std::function<std::optional<std::vector<std::byte>>(const std::string& path)>;
  static void setFileReader(FileReader reader);

  void close();
  bool isOpen() const { return handle_ != nullptr; }

  std::uint32_t width() const { return width_; }
  std::uint32_t height() const { return height_; }

  // One step of the movie — as long as its frame rate says a frame is.
  void advance();

  // Draws a frame and returns RGBA. Empty when it did not work.
  const std::vector<std::uint8_t>& render();

  // Input. The coordinates are in the movie's stage pixels, so before the call
  // they have to be converted from the window (see `Movie::toStage`).
  void mouseMove(double x, double y);
  void mouseButton(double x, double y, bool down);
  void text(std::uint32_t codepoint);

  // The control keys an input field understands apart from characters.
  enum class Key { Backspace = 1, Delete = 2, Left = 3, Right = 4, Enter = 5, Escape = 6 };
  void key(Key which, bool down);

  // Orders the menu gives the host: `level <path> <mode> <size>`, `quit`.
  // Empty string when there is nothing queued. Poll once per frame.
  std::string takeCommand();

  // The window may be a different size from the movie's stage. We convert so
  // that clicks land where the player sees them.
  void toStage(int windowWidth, int windowHeight, float windowX, float windowY, double* stageX,
               double* stageY) const;

 private:
  void* handle_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::vector<std::uint8_t> pixels_;
};

}  // namespace obf2::flash
