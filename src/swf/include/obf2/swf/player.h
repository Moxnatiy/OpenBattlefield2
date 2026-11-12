#pragma once
// The menu's Flash player, native: our gameswf (third_party/gameswf, tu-testbed
// r777 grown with SwiffPlayer's changes — docs/research/12-native-flash.md),
// the bridge the movie talks to the engine through (`dice.bf2.*`,
// docs/functions/menu-bridge.md), and a renderer that paints each frame into an
// RGBA image the application shows as a texture.
//
// It replaces the Ruffle-based `obf2::flash::Movie` and keeps its shape: open a
// movie, step it, render, feed the mouse, take the orders it gives.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace obf2::swf {

// What the bridge needs from the engine. Everything the menu shows comes from
// here — the bridge reads no file and keeps no copy of the engine's state.
struct Host {
  // A file through the engine's file system: the movie, its pictures, the
  // levels' `Info/<level>.desc`, `mod.desc`.
  std::function<std::optional<std::vector<std::byte>>(const std::string& path)> readFile;
  // A localisation key into text; the key itself when it is missing.
  std::function<std::string(const std::string& key)> localize;

  struct Profile {
    std::string id;
    std::string name;
    std::string gamespyNick;
  };
  std::vector<Profile> profiles;
  std::string defaultUser;  // GlobalSettings.setDefaultUser
  std::string namePrefix;   // GlobalSettings.setNamePrefix
  // The level folders (`Dalian_plant`); their `.desc` gives the rest.
  std::vector<std::string> levelDirectories;
  std::string hostVersion;  // what Logic.getModVersion answers
  std::string language = "english";
  // The mod's folder on disk: `loadMovie("?menu.bik")` plays `<mod>/Movies/menu.bik`
  // (RendDX9.dll 0x101ba46f).
  std::string modDirectory;
  // The engine's settings objects (`GameServerSettings`, `GeneralSettings`) by
  // the name after get/set (`TeamRatio`), as text — what the menu's
  // ServerSettings/GeneralSettings get* read (SwiffPlayer.dll 0x10066760,
  // 0x10033b50).
  std::function<std::string(const std::string& object, const std::string& name)> setting;
  // A console line run by the engine — what their set* become, so the
  // engine's own handlers of those commands see them too.
  std::function<void(const std::string& line)> console;
  // Logic.isGameRunning (0x1004cc30: the host's +0xb8): a round is on.
  std::function<bool()> gameRunning;
  // Logic.isCapsLockPressed (0x1004d450: GetKeyState(VK_CAPITAL) & 1 — the
  // lock's state, not the key held).
  std::function<bool()> capsLock;
  // Logic.cacheCredits (0x1004c950): the host reads the credits' script;
  // Multiplay.getScrollFadeSpeed (0x10052450: the host's +0x314) answers the
  // fade speed it set.
  std::function<void()> cacheCredits;
  std::function<int()> scrollFadeSpeed;

  // The level being loaded, for endOfRound.swf — the loading screen (the host
  // turns its loading flag on, +0xD0, BF2.exe 0x4ec08c, and the movie reads it
  // with Logic.getIsLoading). Empty `mapPath` while no level is named.
  struct Loading {
    std::string mapPath;  // the level's directory in lower case (EndOfRound.getMapPath)
    std::string gameMode; // gpm_cq, sp1 … (getMapGameMode)
    int mapSize = 0;      // 16, 32, 64 (getMapSize)
    bool firstLoad = true;                     // Logic.isFirstTimeLoading
    std::function<bool()> active;              // Logic.getIsLoading
    std::function<float()> progress;           // Client.getLoadProgress, 0..1
    std::function<std::string()> currentFile;  // Client.getCurrentLoadFile
    std::function<void()> readyToPlay;         // Logic.readyToPlay
  } loading;
};

class Player {
 public:
  Player();
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  // Installs the bridge and opens the movie at `path` (through Host::readFile).
  bool open(const Host& host, const std::string& path);
  bool isOpen() const;

  std::uint32_t width() const;   // the stage, pixels
  std::uint32_t height() const;

  // One step of `seconds`, then the deferred captions handed out.
  void advance(double seconds);
  // The frame, `width × height × 4`, RGBA, straight alpha.
  const std::vector<std::uint8_t>& render();

  // The mouse in stage pixels; `down` is the left button.
  void mouse(double x, double y, bool down);

  // An order the menu gave: "login 0001", "level dalian_plant gpm_cq 16",
  // "quit". Empty when there is none.
  std::string takeCommand();

  // The library's complaints so far, by kind — for the end-of-run report.
  const std::vector<std::pair<std::string, int>>& complaints() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace obf2::swf
