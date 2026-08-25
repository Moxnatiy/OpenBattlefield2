#include "obf2/font/dif_font.h"

#include <charconv>
#include <cstdlib>

namespace obf2::font {
namespace {

// Читач рядків, стійкий до CRLF: файли шрифтів прийшли з Windows.
class LineReader {
 public:
  explicit LineReader(std::string_view text) : text_(text) {}

  bool next(std::string_view& line) {
    if (position_ >= text_.size()) return false;
    const std::size_t end = text_.find('\n', position_);
    line = text_.substr(position_,
                        end == std::string_view::npos ? std::string_view::npos : end - position_);
    position_ = (end == std::string_view::npos) ? text_.size() : end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    ++lineNumber_;
    return true;
  }

  int lineNumber() const { return lineNumber_; }

 private:
  std::string_view text_;
  std::size_t position_ = 0;
  int lineNumber_ = 0;
};

// Поля розділені табуляціями.
std::vector<std::string_view> split(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= line.size(); ++i) {
    if (i != line.size() && line[i] != '\t') continue;
    fields.push_back(line.substr(start, i - start));
    start = i + 1;
  }
  return fields;
}

bool parseInt(std::string_view text, int& out) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
  return result.ec == std::errc{};
}

bool parseFloat(std::string_view text, float& out) {
  // Числа у файлі завжди у форматі C; from_chars для float є не всюди,
  // тому там, де його нема, лишається strtof із локаллю "C".
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
  return result.ec == std::errc{};
#else
  const std::string copy(text);
  char* end = nullptr;
  out = std::strtof(copy.c_str(), &end);
  return end != copy.c_str();
#endif
}

}  // namespace

const Glyph* Font::glyph(std::uint32_t code) const {
  const auto found = glyphs_.find(code);
  return found == glyphs_.end() ? nullptr : &found->second;
}

float Font::kerning(std::uint32_t first, std::uint32_t second) const {
  const auto found = kerning_.find(pairKey(first, second));
  return found == kerning_.end() ? 0.0f : found->second;
}

float Font::measure(std::string_view text) const {
  float width = 0.0f;
  std::uint32_t previous = 0;
  std::size_t position = 0;
  while (position < text.size()) {
    const std::uint32_t code = nextCodepoint(text, position);
    const Glyph* found = glyph(code);
    if (found == nullptr) {
      previous = 0;
      continue;
    }
    if (previous != 0) width += kerning(previous, code);
    width += found->advance();
    previous = code;
  }
  return width;
}

std::uint32_t nextCodepoint(std::string_view text, std::size_t& position) {
  // Рядки локалізації — UTF-8, і в них трапляються не-ASCII символи
  // (типографський апостроф у "People’s"). Читати їх побайтово не можна:
  // у шрифті гліф лежить під справжнім кодом, а не під першим байтом.
  if (position >= text.size()) return 0;

  const auto first = static_cast<unsigned char>(text[position]);
  std::size_t extra = 0;
  std::uint32_t code = first;

  if ((first & 0x80u) == 0) {
    extra = 0;
  } else if ((first & 0xE0u) == 0xC0u) {
    extra = 1;
    code = first & 0x1Fu;
  } else if ((first & 0xF0u) == 0xE0u) {
    extra = 2;
    code = first & 0x0Fu;
  } else if ((first & 0xF8u) == 0xF0u) {
    extra = 3;
    code = first & 0x07u;
  } else {
    ++position;  // зіпсований байт — пропускаємо
    return 0xFFFDu;
  }

  if (position + extra >= text.size()) {
    position = text.size();
    return 0xFFFDu;
  }
  for (std::size_t i = 1; i <= extra; ++i) {
    const auto continuation = static_cast<unsigned char>(text[position + i]);
    if ((continuation & 0xC0u) != 0x80u) {
      position += i;
      return 0xFFFDu;
    }
    code = (code << 6) | (continuation & 0x3Fu);
  }
  position += extra + 1;
  return code;
}

std::optional<Font> parseDif(std::string_view text, std::string* error) {
  LineReader reader(text);
  std::string_view line;

  auto fail = [&](const std::string& why) -> std::optional<Font> {
    if (error) *error = why + " (рядок " + std::to_string(reader.lineNumber()) + ")";
    return std::nullopt;
  };

  if (!reader.next(line) || line != "header") return fail("немає секції header");

  Font font;
  int version = 0;
  if (!reader.next(line) || !parseInt(line, version)) return fail("немає версії");
  if (version != 2) return fail("непідтримувана версія: " + std::to_string(version));

  if (!reader.next(line)) return fail("немає імені шрифту");
  font.name = std::string(line);

  if (!reader.next(line) || !parseInt(line, font.atlasWidth)) return fail("немає ширини атласа");
  if (!reader.next(line) || !parseInt(line, font.atlasHeight)) return fail("немає висоти атласа");
  if (!reader.next(line) || !parseFloat(line, font.size)) return fail("немає кегля");

  if (!reader.next(line) || line != "glyphs") return fail("немає секції glyphs");
  int glyphCount = 0;
  if (!reader.next(line) || !parseInt(line, glyphCount) || glyphCount < 0) {
    return fail("немає кількості гліфів");
  }

  for (int i = 0; i < glyphCount; ++i) {
    if (!reader.next(line)) return fail("гліфи обірвалися");
    const auto fields = split(line);
    if (fields.size() < 9) return fail("у гліфа замало полів");

    Glyph glyph;
    int code = 0;
    if (!parseInt(fields[0], code)) return fail("некоректний код гліфа");
    glyph.code = static_cast<std::uint32_t>(code);

    if (!parseFloat(fields[1], glyph.bearingLeft) || !parseFloat(fields[2], glyph.width) ||
        !parseFloat(fields[3], glyph.bearingRight) || !parseInt(fields[4], glyph.offsetY) ||
        !parseInt(fields[5], glyph.left) || !parseInt(fields[6], glyph.top) ||
        !parseInt(fields[7], glyph.right) || !parseInt(fields[8], glyph.bottom)) {
      return fail("некоректні метрики гліфа");
    }
    font.glyphs_.emplace(glyph.code, glyph);
  }

  // Секція кернінгу необов'язкова.
  if (reader.next(line) && line == "kerning") {
    int kerningCount = 0;
    if (!reader.next(line) || !parseInt(line, kerningCount) || kerningCount < 0) {
      return fail("немає кількості пар кернінгу");
    }
    for (int i = 0; i < kerningCount; ++i) {
      if (!reader.next(line)) return fail("кернінг обірвався");
      const auto fields = split(line);
      if (fields.size() < 3) return fail("у пари кернінгу замало полів");

      int first = 0, second = 0;
      float offset = 0.0f;
      if (!parseInt(fields[0], first) || !parseInt(fields[1], second) ||
          !parseFloat(fields[2], offset)) {
        return fail("некоректна пара кернінгу");
      }
      font.kerning_.emplace(Font::pairKey(static_cast<std::uint32_t>(first),
                                          static_cast<std::uint32_t>(second)),
                            offset);
    }
  }

  return font;
}

}  // namespace obf2::font
