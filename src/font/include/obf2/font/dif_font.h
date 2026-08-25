#pragma once
// Шрифти BF2: пара файлів `.dif` + `.dds`.
//
// `.dif` — **текстовий** формат метрик (реверс не знадобився), `.dds` поруч
// із ним — атлас гліфів. Обидва лежать у `Fonts_client.zip`, з окремими
// теками під роздільність (`800/`) і мову (`Chinese/800/`).
//
// Розкладка `.dif`, знята з реальних файлів:
//
//   header
//   2                          версія
//   scoreboardFont_8           ім'я
//   128                        ширина атласа
//   128                        висота атласа
//   8.000000                   кегль / висота рядка
//   glyphs
//   328                        скільки
//   65<TAB>0.066667<TAB>4.600000<TAB>1.266667<TAB>0<TAB>35<TAB>7<TAB>40<TAB>14
//   ...
//   kerning
//   216
//   65<TAB>84<TAB>-0.466667    пара символів і поправка
//
// Поля гліфа: код, лівий винос, ширина, правий винос, зсув по вертикалі,
// далі прямокутник в атласі (left, top, right, bottom). Що це саме
// прямокутник, видно з даних: `!` дає 1x7, `"` — 2x3, `A` — 5x7.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace obf2::font {

struct Glyph {
  std::uint32_t code = 0;
  float bearingLeft = 0.0f;
  float width = 0.0f;   // ширина у пікселях, дробова
  float bearingRight = 0.0f;
  int offsetY = 0;      // зсув від верху рядка: кома й крапка сидять нижче

  // Прямокутник в атласі, у пікселях.
  int left = 0, top = 0, right = 0, bottom = 0;

  int pixelWidth() const { return right - left; }
  int pixelHeight() const { return bottom - top; }
  // Крок до наступного символу без урахування кернінгу.
  float advance() const { return bearingLeft + width + bearingRight; }
};

class Font {
 public:
  std::string name;
  int atlasWidth = 0;
  int atlasHeight = 0;
  float size = 0.0f;  // кегль, він же висота рядка

  const Glyph* glyph(std::uint32_t code) const;
  float kerning(std::uint32_t first, std::uint32_t second) const;

  // Ширина рядка в пікселях з урахуванням кернінгу.
  float measure(std::string_view text) const;

  std::size_t glyphCount() const { return glyphs_.size(); }
  std::size_t kerningCount() const { return kerning_.size(); }

  friend std::optional<Font> parseDif(std::string_view text, std::string* error);

 private:
  static std::uint64_t pairKey(std::uint32_t first, std::uint32_t second) {
    return (static_cast<std::uint64_t>(first) << 32) | second;
  }

  std::unordered_map<std::uint32_t, Glyph> glyphs_;
  std::unordered_map<std::uint64_t, float> kerning_;
};

// Читає один символ UTF-8 і посуває позицію. Рядки локалізації саме в UTF-8.
std::uint32_t nextCodepoint(std::string_view text, std::size_t& position);

// nullopt + пояснення: зіпсований шрифт не має валити рушій.
std::optional<Font> parseDif(std::string_view text, std::string* error = nullptr);

}  // namespace obf2::font
