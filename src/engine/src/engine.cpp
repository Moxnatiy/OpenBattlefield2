#include "obf2/engine/engine.h"

#include <algorithm>
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
    // 79 псевдонімів консолі: `fps`, `hud`, `lp`, `suicide` і решта
    // коротких імен, якими грають у консоль. Без цього файлу вони просто
    // невідомі команди.
    "Settings/AliasedCommands.con",
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

// Мінімальний витяг вмісту тега. .desc — це XML, але нам звідти потрібне
// лише <name>, і тягти повноцінний парсер заради одного тега ні до чого.
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

// Значення атрибута: <briefing locid="LOADINGSCREEN_MAPDESCRIPTION_dalianplant">
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

  loadLexicon(files);
  scanLevels(files, modDir);

  enter(settings_.general.viewIntroMovie && !movies_.empty() ? State::Intro : State::MainMenu);
  return true;
}

void Engine::loadLexicon(FileSystem& files) {
  // Гра читає кілька файлів на мову: основний, патчі, додатки. Пізніші
  // перекривають раніші, тому порядок має значення.
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

    // Назва лежить у Info/<ім'я>.desc — там же, де й картинка завантаження.
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
