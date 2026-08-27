#pragma once
// Стан рушія від запуску до меню.
//
// Порядок узятий з оригіналу: спершу читаються налаштування, потім
// програються заставки, потім показується головне меню. Логіку повторюємо
// 1:1, а от **технології — ні**:
//
//   * заставки BF2 — це Bink (`Movies/*.bik`, 138 МБ на саме інтро),
//     програються через binkw32.dll;
//   * меню — Macromedia Flash, який рушій крутить своїм FSMoviePlayer
//     (`dice.hfe.geom.FSMoviePlayer` у таблиці рядків).
//
// Відтворювати ні Bink, ні Flash ми не будемо: це велика робота заради
// технологій 2005 року. Стани, переходи, налаштування й порядок читання
// файлів лишаються ті самі, а показ — наш власний.
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
  Boot,      // читання налаштувань
  Intro,     // заставки EA / DICE / Intro
  MainMenu,  // головне меню
  Loading,   // завантаження рівня
  InGame,
};

std::string_view stateName(State state);

// Одна заставка зі стартового ланцюжка.
struct Movie {
  std::string path;
  std::size_t sizeBytes = 0;
};

// Рівень у списку меню. Назву беремо з Info/<ім'я>.desc, як і гра.
struct LevelEntry {
  std::string directory;   // ім'я теки: Dalian_plant
  std::string displayName; // <name> з .desc
  std::string loadImage;   // Info/loadmap.png, якщо є
  std::string briefingKey; // locid із <briefing> — опис на екрані завантаження
};

class Engine {
 public:
  // Виконує стартовий ланцюжок .con у тому ж порядку, що й гра.
  bool boot(FileSystem& files, const std::filesystem::path& modDir);

  // Тільки словник, без решти завантаження. Потрібен і в бою: підписи
  // HUD — це ключі локалізації, і без словника на екрані видно самі
  // ключі замість тексту.
  void loadLexicon(FileSystem& files);

  void update(float deltaSeconds);

  // Перехід до завантаження рівня — як вибір карти в меню.
  bool startLoading(std::string_view levelDirectory);
  void finishLoading();

  // Пропустити поточну заставку — як пробіл у грі.
  void skipMovie();
  // Пропустити всі заставки одразу (потрібно для детермінованих знімків).
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
  std::vector<std::string> bootFiles_;  // які .con реально прочиталися
  int currentMovie_ = -1;
  float movieElapsed_ = 0.0f;
};

}  // namespace obf2::engine
