#include "obf2/engine/engine.h"

#include <system_error>

namespace obf2::engine {
namespace {

// Порядок як в оригіналі: спершу типові налаштування відео, потім збережені
// користувачем, далі профіль, звук і керування. Файлів може не бути — гра в
// такому разі просто лишає значення за замовчуванням, і ми теж.
constexpr const char* kBootFiles[] = {
    "Settings/VideoDefault.con",
    "Settings/Video.con",
    "Settings/GeneralOptions.con",
    "Settings/Sound.con",
    "Settings/Controls.con",
};

// Заставки в порядку показу. Це Bink-відео; ми їх не декодуємо, але порядок
// і сам факт показу лишаються ті самі, що в грі.
constexpr const char* kMovieNames[] = {
    "Movies/EA.bik",
    "Movies/Dice.bik",
    "Movies/Legal.bik",
    "Movies/Intro.bik",
};

// Скільки тримати заставку, якої ми не вміємо програти. Реальні тривалості
// відомі лише з самого Bink, тому поки що умовна пауза.
constexpr float kPlaceholderMovieSeconds = 1.5f;

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

  // Налаштування читаються тим самим інтерпретатором, що й усе інше:
  // у Refractor 2 консоль — єдина точка входу для будь-якого .con.
  con::Interpreter interpreter(
      files, [this](const con::Command& command) { console_.execute(command); });

  for (const char* name : kBootFiles) {
    if (!files.exists(name)) continue;
    bootFiles_.emplace_back(name);
    interpreter.runFile(name);
  }

  // Заставки лежать не в архіві, а поруч із грою, тому дивимося на диск.
  for (const char* name : kMovieNames) {
    const std::filesystem::path path = modDir / name;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) continue;
    movies_.push_back(Movie{path.string(), static_cast<std::size_t>(size)});
  }

  enter(settings_.general.viewIntroMovie && !movies_.empty() ? State::Intro : State::MainMenu);
  return true;
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

void Engine::update(float deltaSeconds) {
  if (state_ != State::Intro) return;

  movieElapsed_ += deltaSeconds;
  if (movieElapsed_ < kPlaceholderMovieSeconds) return;

  movieElapsed_ = 0.0f;
  ++currentMovie_;
  if (currentMovie_ >= static_cast<int>(movies_.size())) enter(State::MainMenu);
}

}  // namespace obf2::engine
