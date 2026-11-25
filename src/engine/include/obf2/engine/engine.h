#pragma once
// The engine's state from startup to the menu.
//
// The order is taken from the original: first the settings are read, then the
// intro movies play, then the main menu is shown. We reproduce the logic 1:1,
// but **not the technologies**:
//
//   * BF2's intros are Bink (`Movies/*.bik`, 138 MB for the intro alone),
//     played through binkw32.dll;
//   * the menu is Macromedia Flash, which the engine runs with its FSMoviePlayer
//     (`dice.hfe.geom.FSMoviePlayer` in the string table).
//
// The movies are decoded through FFmpeg (`src/video`); the menu is played by our
// own player over gameswf (`src/swf`, docs/research/12-native-flash.md). The states, the transitions,
// the settings and the order files are read in stay the same.
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "obf2/engine/console.h"
#include "obf2/engine/control_map.h"
#include "obf2/engine/movie_queue.h"
#include "obf2/engine/settings.h"
#include "obf2/loc/lexicon.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::engine {

enum class State {
  Boot,      // reading the settings
  Intro,     // the movies Init.con queues: EA, dice, legal, intro, welcome
  MainMenu,  // the main menu
  Loading,   // loading a level
  InGame,
};

std::string_view stateName(State state);

// A level in the menu's list. The name comes from Info/<name>.desc, as in the game.
struct LevelEntry {
  std::string directory;   // the directory's name: Dalian_plant
  std::string displayName; // <name> from the .desc
  std::string loadImage;   // Info/loadmap.png, if present
  std::string briefingKey; // the locid from <briefing> — the loading screen's description
  // <music> from the .desc: the level's own music, a path in the archives
  // (common/sound/menu/music/load_CH_music.ogg). The client's reader keeps it at
  // +0x70 of its level description (BF2.exe 0x4802fe); who plays it, and when,
  // is not established.
  std::string music;
};

// <music> from the mod's own `mod.desc`: the mod's music, a path in the archives
// (common/sound/menu/music/menu_music.ogg in bf2). The client builds the path
// from the mod's folder and "mod.desc" (BF2.exe 0x47ddab) and keeps the tag at
// +0x8c of its mod description (0x47df11); who plays it, and when, is not
// established. Empty when the file or the tag is missing.
std::string readModMusic(FileSystem& files);

class Engine {
 public:
  // Runs the startup chain of .con files in the same order as the game
  // (docs/functions/startup.md). `homeDir` is `@HOME@`, the folder holding
  // `Profiles/`; empty skips the profile.
  bool boot(FileSystem& files, const std::filesystem::path& modDir,
            const std::filesystem::path& homeDir = {});

  // The dictionary only, without the rest of the loading. Needed in a battle
  // too: the HUD's labels are localisation keys, and without the dictionary the
  // keys themselves show on screen instead of text.
  void loadLexicon(FileSystem& files);

  // One frame. `escapePressed` is IDKey_Escape going down this frame: the only key
  // that skips a movie, and only one whose flag is not 1 (movie_queue.h).
  void update(float deltaSeconds, bool escapePressed = false);

  // The transition to loading a level — like picking a map in the menu.
  bool startLoading(std::string_view levelDirectory);
  void finishLoading();

  // Drop every movie at once (needed for deterministic screenshots). Not an
  // engine path: the original's own way is the `skipMovies` setting.
  void skipAllMovies();
  // Log a local profile in: `loadCurrentProfile` (`BF2.exe` 0x4ab1d0) for a named
  // profile, its folder under `@HOME@/Profiles/` ("0001"). False when its
  // Profile.con does not open.
  bool login(const std::string& profile);
  // `GlobalSettings.setDefaultUser` from Profiles/Global.con: the profile the
  // menu's login page offers first.
  const std::string& defaultUser() const { return defaultUser_; }
  // `GlobalSettings.setNamePrefix` from Profiles/Global.con.
  const std::string& namePrefix() const { return namePrefix_; }

  // The settings objects `GameServerSettings` and `GeneralSettings`: what the
  // profile's ServerSettings.con / General.con write
  // (`GameServerSettings.setTicketRatio 200`) and the menu's ServerSettings /
  // GeneralSettings get*/set* reach (SwiffPlayer.dll looks the object up by
  // name: setTeamRatio 0x10063b50, setMaxBots 0x10031f40). Kept as text by
  // object and the name after `set`/`get`, without case; "" when never set.
  // The engine's own fields behind them (BF2.exe 0x987354, 0x987390) are not
  // read.
  // The menu's credits, as the host keeps them: `credits.con` (Menu_client,
  // External/FlashMenu) sets `swiffHost.setScrollFadeSpeed 200`,
  // `setScrollSpeed 1200`, `setScrollerRect 0 118 1024 668` and one
  // `swiffHost.addCredit "<text or localisation key>" <size>` per line (size 2
  // a title, 1 a subtitle, 0 a name — the file's own comment). The host
  // scrolls them itself while the menu has `Logic.enableScroller(true)`.
  struct Credits {
    int fadeSpeed = 0;
    int scrollSpeed = 0;
    int rect[4] = {0, 0, 0, 0};
    std::vector<std::pair<std::string, int>> lines;
  };
  const Credits& credits() const { return credits_; }
  // Logic.cacheCredits (SwiffPlayer.dll 0x1004c950 → the host) runs
  // `menu/external/flashmenu/credits.con` (BF2.exe names that path, file
  // offset 0x49fe8c), the earlier lines cleared first.
  void cacheCredits(FileSystem& files);

  std::string setting(const std::string& object, const std::string& name) const;
  void setSetting(const std::string& object, const std::string& name, const std::string& value);

  // The local profiles: the numbered folders under `@HOME@/Profiles/`, each
  // with the names its Profile.con sets — what the menu's login page lists.
  struct LocalProfile {
    std::string id;           // the folder: "0001"
    std::string name;         // LocalProfile.setName
    std::string nick;         // LocalProfile.setNick
    std::string gamespyNick;  // LocalProfile.setGamespyNick
  };
  std::vector<LocalProfile> localProfiles() const;
  const std::string& currentProfile() const { return currentProfile_; }

  // The setting `skipMovies`, before `boot`.
  void setSkipMovies(bool skip) { movies_.setSkipMovies(skip); }

  State state() const { return state_; }
  const Settings& settings() const { return settings_; }
  const ControlMap& controls() const { return controls_; }
  Console& console() { return console_; }
  const Console& console() const { return console_; }

  const MovieQueue& movies() const { return movies_; }
  // Where a queued movie's name is resolved from (the mod's folder).
  const std::filesystem::path& movieDir() const { return modDir_; }
  const std::vector<LevelEntry>& levels() const { return levels_; }
  const loc::Lexicon& lexicon() const { return lexicon_; }
  const LevelEntry* loadingLevel() const;
  const std::vector<std::string>& bootFiles() const { return bootFiles_; }

  // A movie's length in seconds from its Bink header, nullopt when it does not open.
  std::optional<double> openMovie(const std::string& name) const;

 private:
  void enter(State next);
  void loadDefaultProfile(const std::filesystem::path& homeDir);
  bool loadProfile(const std::string& profile);
  void scanLevels(FileSystem& files, const std::filesystem::path& modDir);

  Console console_;
  Settings settings_;
  ControlMap controls_;
  State state_ = State::Boot;

  MovieQueue movies_;
  std::filesystem::path modDir_;
  std::filesystem::path homeDir_;
  std::string defaultUser_;
  std::string namePrefix_;
  std::map<std::string, std::string> objectSettings_;  // "object.name", lower case → value
  Credits credits_;
  std::string currentProfile_;
  std::vector<LevelEntry> levels_;
  loc::Lexicon lexicon_;
  int loadingIndex_ = -1;
  std::vector<std::string> bootFiles_;  // which .con files actually were read
};

}  // namespace obf2::engine
