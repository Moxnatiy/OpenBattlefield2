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
// We are not going to reproduce Bink; the menu is played by Ruffle
// (`src/flash`, docs/research/11-ruffle-menu.md). The states, the transitions,
// the settings and the order files are read in stay the same.
#include <filesystem>
#include <string>
#include <vector>

#include "obf2/engine/console.h"
#include "obf2/engine/control_map.h"
#include "obf2/engine/settings.h"
#include "obf2/loc/lexicon.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::engine {

enum class State {
  Boot,      // reading the settings
  Intro,     // the EA / DICE / Intro movies
  MainMenu,  // the main menu
  Loading,   // loading a level
  InGame,
};

std::string_view stateName(State state);

// One movie from the startup chain.
struct Movie {
  std::string path;
  std::size_t sizeBytes = 0;
};

// A level in the menu's list. The name comes from Info/<name>.desc, as in the game.
struct LevelEntry {
  std::string directory;   // the directory's name: Dalian_plant
  std::string displayName; // <name> from the .desc
  std::string loadImage;   // Info/loadmap.png, if present
  std::string briefingKey; // the locid from <briefing> — the loading screen's description
};

class Engine {
 public:
  // Runs the startup chain of .con files in the same order as the game.
  bool boot(FileSystem& files, const std::filesystem::path& modDir);

  // The dictionary only, without the rest of the loading. Needed in a battle
  // too: the HUD's labels are localisation keys, and without the dictionary the
  // keys themselves show on screen instead of text.
  void loadLexicon(FileSystem& files);

  void update(float deltaSeconds);

  // The transition to loading a level — like picking a map in the menu.
  bool startLoading(std::string_view levelDirectory);
  void finishLoading();

  // Skip the current movie — like the space bar in the game.
  void skipMovie();
  // Skip every movie at once (needed for deterministic screenshots).
  void skipAllMovies();

  State state() const { return state_; }
  const Settings& settings() const { return settings_; }
  const ControlMap& controls() const { return controls_; }
  Console& console() { return console_; }
  const Console& console() const { return console_; }

  const std::vector<Movie>& movies() const { return movies_; }
  const std::vector<LevelEntry>& levels() const { return levels_; }
  const loc::Lexicon& lexicon() const { return lexicon_; }
  const LevelEntry* loadingLevel() const;
  int currentMovie() const { return currentMovie_; }
  const std::vector<std::string>& bootFiles() const { return bootFiles_; }

 private:
  void enter(State next);
  void scanLevels(FileSystem& files, const std::filesystem::path& modDir);

  Console console_;
  Settings settings_;
  ControlMap controls_;
  State state_ = State::Boot;

  std::vector<Movie> movies_;
  std::vector<LevelEntry> levels_;
  loc::Lexicon lexicon_;
  int loadingIndex_ = -1;
  std::vector<std::string> bootFiles_;  // which .con files actually were read
  int currentMovie_ = -1;
  float movieElapsed_ = 0.0f;
};

}  // namespace obf2::engine
