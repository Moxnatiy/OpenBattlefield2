#include "obf2/engine/engine.h"

#include <algorithm>
#include <system_error>

namespace obf2::engine {
namespace {

// The order is as in the original: first the default video settings, then the
// ones saved by the user, then the profile, sound and controls. The files may be
// absent — the game then simply keeps the defaults, and so do we.
constexpr const char* kBootFiles[] = {
    "Settings/VideoDefault.con",
    "Settings/Video.con",
    "Settings/GeneralOptions.con",
    "Settings/Sound.con",
    "Settings/Controls.con",
    // The console's 79 aliases: `fps`, `hud`, `lp`, `suicide` and the other
    // short names the console is played with. Without this file they are simply
    // unknown commands.
    "Settings/AliasedCommands.con",
};

// The intro movies in the order they are shown. They are Bink videos; we do not
// decode them, but the order and the fact of showing them stay as in the game.
constexpr const char* kMovieNames[] = {
    "Movies/EA.bik",
    "Movies/Dice.bik",
    "Movies/Legal.bik",
    "Movies/Intro.bik",
};

// How long to hold a movie we cannot play. The real durations are known only
// from the Bink itself, so for now this is a nominal pause.
constexpr float kPlaceholderMovieSeconds = 1.5f;

// A minimal extraction of a tag's content. A .desc is XML, but all we need from
// it is <name>, and dragging in a full parser for one tag is pointless.
std::string tagValue(std::string_view xml, std::string_view tag) {
  const std::string open = "<" + std::string(tag) + ">";
  const std::string close = "</" + std::string(tag) + ">";
  const std::size_t start = xml.find(open);
  if (start == std::string_view::npos) return {};
  const std::size_t from = start + open.size();
  const std::size_t end = xml.find(close, from);
  if (end == std::string_view::npos) return {};

  std::string_view value = xml.substr(from, end - from);
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  return std::string(value);
}

// An attribute's value: <briefing locid="LOADINGSCREEN_MAPDESCRIPTION_dalianplant">
std::string attributeValue(std::string_view xml, std::string_view tag, std::string_view attribute) {
  const std::size_t tagStart = xml.find("<" + std::string(tag));
  if (tagStart == std::string_view::npos) return {};
  const std::size_t tagEnd = xml.find('>', tagStart);
  if (tagEnd == std::string_view::npos) return {};

  const std::string_view inside = xml.substr(tagStart, tagEnd - tagStart);
  const std::string needle = std::string(attribute) + "=\"";
  const std::size_t at = inside.find(needle);
  if (at == std::string_view::npos) return {};

  const std::size_t from = at + needle.size();
  const std::size_t quote = inside.find('"', from);
  if (quote == std::string_view::npos) return {};
  return std::string(inside.substr(from, quote - from));
}

}  // namespace

std::string_view stateName(State state) {
  switch (state) {
    case State::Boot: return "Boot";
    case State::Intro: return "Intro";
    case State::MainMenu: return "MainMenu";
    case State::Loading: return "Loading";
    case State::InGame: return "InGame";
  }
  return "?";
}

bool Engine::boot(FileSystem& files, const std::filesystem::path& modDir) {
  settings_.bind(console_);
  controls_.bind(console_);
  console_.registerAliases();

  // The settings are read with the same interpreter as everything else: in
  // Refractor 2 the console is the single entry point for any .con.
  con::Interpreter interpreter(
      files, [this](const con::Command& command) { console_.execute(command); });

  for (const char* name : kBootFiles) {
    if (!files.exists(name)) continue;
    bootFiles_.emplace_back(name);
    interpreter.runFile(name);
  }

  // The movies lie not in an archive but next to the game, so we look on disk.
  for (const char* name : kMovieNames) {
    const std::filesystem::path path = modDir / name;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) continue;
    movies_.push_back(Movie{path.string(), static_cast<std::size_t>(size)});
  }

  loadLexicon(files);
  scanLevels(files, modDir);

  enter(settings_.general.viewIntroMovie && !movies_.empty() ? State::Intro : State::MainMenu);
  return true;
}

void Engine::loadLexicon(FileSystem& files) {
  // The game reads several files per language: the main one, patches, add-ons.
  // Later ones override earlier, so the order matters.
  for (const char* name : {"Localization/English/english.utxt",
                           "Localization/English/English_Patch.utxt",
                           "Localization/English/XPEnglish.utxt"}) {
    const auto bytes = files.read(name);
    if (bytes) lexicon_.addUtxt(*bytes);
  }
}

void Engine::scanLevels(FileSystem& files, const std::filesystem::path& modDir) {
  std::error_code ec;
  const std::filesystem::path levelsDir = modDir / "Levels";
  if (!std::filesystem::is_directory(levelsDir, ec)) return;

  for (const auto& entry : std::filesystem::directory_iterator(levelsDir, ec)) {
    if (ec) break;
    if (!entry.is_directory()) continue;

    LevelEntry level;
    level.directory = entry.path().filename().string();
    level.displayName = level.directory;

    // The name lies in Info/<name>.desc — the same place as the loading picture.
    const std::string base = "Levels/" + level.directory + "/Info/";
    if (const auto desc = files.read(base + level.directory + ".desc")) {
      const std::string xml(reinterpret_cast<const char*>(desc->data()), desc->size());
      const std::string name = tagValue(xml, "name");
      if (!name.empty()) level.displayName = name;
      level.briefingKey = attributeValue(xml, "briefing", "locid");
    }
    if (files.exists(base + "loadmap.png")) level.loadImage = base + "loadmap.png";

    levels_.push_back(std::move(level));
  }

  std::sort(levels_.begin(), levels_.end(),
            [](const LevelEntry& a, const LevelEntry& b) { return a.displayName < b.displayName; });
}

bool Engine::startLoading(std::string_view levelDirectory) {
  for (std::size_t i = 0; i < levels_.size(); ++i) {
    if (levels_[i].directory == levelDirectory) {
      loadingIndex_ = static_cast<int>(i);
      enter(State::Loading);
      return true;
    }
  }
  return false;
}

void Engine::finishLoading() {
  if (state_ == State::Loading) enter(State::InGame);
}

const LevelEntry* Engine::loadingLevel() const {
  if (loadingIndex_ < 0 || loadingIndex_ >= static_cast<int>(levels_.size())) return nullptr;
  return &levels_[static_cast<std::size_t>(loadingIndex_)];
}

void Engine::enter(State next) {
  state_ = next;
  if (next == State::Intro) {
    currentMovie_ = 0;
    movieElapsed_ = 0.0f;
  } else {
    currentMovie_ = -1;
  }
}

void Engine::skipMovie() {
  if (state_ != State::Intro) return;
  movieElapsed_ = kPlaceholderMovieSeconds;
}

void Engine::skipAllMovies() {
  if (state_ == State::Intro) enter(State::MainMenu);
}

void Engine::update(float deltaSeconds) {
  if (state_ != State::Intro) return;

  movieElapsed_ += deltaSeconds;
  if (movieElapsed_ < kPlaceholderMovieSeconds) return;

  movieElapsed_ = 0.0f;
  ++currentMovie_;
  if (currentMovie_ >= static_cast<int>(movies_.size())) enter(State::MainMenu);
}

}  // namespace obf2::engine
