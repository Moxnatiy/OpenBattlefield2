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
#include "obf2/engine/settings.h"
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

class Engine {
 public:
  // Виконує стартовий ланцюжок .con у тому ж порядку, що й гра.
  bool boot(FileSystem& files, const std::filesystem::path& modDir);

  void update(float deltaSeconds);

  // Пропустити поточну заставку — як пробіл у грі.
  void skipMovie();

  State state() const { return state_; }
  const Settings& settings() const { return settings_; }
  Console& console() { return console_; }
  const Console& console() const { return console_; }

  const std::vector<Movie>& movies() const { return movies_; }
  int currentMovie() const { return currentMovie_; }
  const std::vector<std::string>& bootFiles() const { return bootFiles_; }

 private:
  void enter(State next);

  Console console_;
  Settings settings_;
  State state_ = State::Boot;

  std::vector<Movie> movies_;
  std::vector<std::string> bootFiles_;  // які .con реально прочиталися
  int currentMovie_ = -1;
  float movieElapsed_ = 0.0f;
};

}  // namespace obf2::engine
