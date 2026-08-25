#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/font/text.h"
#include "obf2/loc/lexicon.h"

using namespace obf2;

namespace {

// Форма взята з реального 800/scoreboardFont_8.dif.
const std::string kDif =
    "header\r\n"
    "2\r\n"
    "TestFont\r\n"
    "128\r\n"
    "64\r\n"
    "8.000000\r\n"
    "glyphs\r\n"
    "4\r\n"
    "32\t0.000000\t0.066667\t4.266667\t6\t0\t0\t0\t0\r\n"
    "33\t1.000000\t0.800000\t2.266667\t0\t0\t0\t1\t7\r\n"
    "65\t0.066667\t4.600000\t1.266667\t0\t35\t7\t40\t14\r\n"
    "8217\t0.100000\t1.000000\t0.500000\t0\t50\t2\t52\t6\r\n"
    "kerning\r\n"
    "2\r\n"
    "65\t84\t-0.466667\r\n"
    "65\t65\t-0.100000\r\n";

std::vector<std::byte> utf16(const std::string& utf8Ascii, bool withBom = true) {
  std::vector<std::byte> out;
  if (withBom) {
    out.push_back(std::byte{0xFF});
    out.push_back(std::byte{0xFE});
  }
  for (const char c : utf8Ascii) {
    out.push_back(static_cast<std::byte>(c));
    out.push_back(std::byte{0});
  }
  return out;
}

}  // namespace

static void testParseDif() {
  std::string error;
  const auto font = font::parseDif(kDif, &error);
  CHECK(font.has_value());
  if (!font) {
    std::fprintf(stderr, "  причина: %s\n", error.c_str());
    return;
  }

  CHECK_EQ(font->name, std::string("TestFont"));
  CHECK_EQ(font->atlasWidth, 128);
  CHECK_EQ(font->atlasHeight, 64);
  CHECK_EQ(font->glyphCount(), std::size_t(4));
  CHECK_EQ(font->kerningCount(), std::size_t(2));

  const font::Glyph* letterA = font->glyph('A');
  CHECK(letterA != nullptr);
  if (letterA != nullptr) {
    // Прямокутник в атласі: (35,7)-(40,14) це 5x7.
    CHECK_EQ(letterA->pixelWidth(), 5);
    CHECK_EQ(letterA->pixelHeight(), 7);
    CHECK_EQ(letterA->offsetY, 0);
  }

  // Пробіл не має прямокутника, але має крок.
  const font::Glyph* space = font->glyph(' ');
  CHECK(space != nullptr);
  if (space != nullptr) {
    CHECK_EQ(space->pixelWidth(), 0);
    CHECK(space->advance() > 4.3f && space->advance() < 4.4f);
  }

  CHECK(font->kerning('A', 'T') < -0.4f);
  CHECK_EQ(font->kerning('A', 'B'), 0.0f);  // такої пари немає
}

static void testMeasureUsesKerning() {
  const auto font = font::parseDif(kDif);
  CHECK(font.has_value());
  if (!font) return;

  const float single = font->measure("A");
  const float doubled = font->measure("AA");
  // Дві "A" з від'ємним кернінгом коротші за подвоєну ширину однієї.
  CHECK(doubled < single * 2.0f);
  CHECK(doubled > single);
}

static void testUtf8IsDecodedAsCodepoints() {
  const auto font = font::parseDif(kDif);
  CHECK(font.has_value());
  if (!font) return;

  // U+2019 (’) у UTF-8 — три байти. Якщо читати побайтово, гліф 8217
  // не знайдеться, а на екрані буде сміття замість апострофа.
  const std::string apostrophe = "\xE2\x80\x99";
  CHECK(font->measure(apostrophe) > 1.5f);

  std::size_t position = 0;
  CHECK_EQ(font::nextCodepoint(apostrophe, position), 8217u);
  CHECK_EQ(position, std::size_t(3));
}

static void testBuildTextGeometry() {
  const auto font = font::parseDif(kDif);
  CHECK(font.has_value());
  if (!font) return;

  font::TextLayout layout;
  layout.x = 0.0f;
  layout.y = 0.0f;
  layout.screenWidth = 100;
  layout.screenHeight = 100;

  const auto geometry = font::buildText(*font, "A A", layout, "atlas.dds");
  // Дві "A" дають по чотири вершини; пробіл геометрії не додає.
  CHECK_EQ(geometry.vertices.size(), std::size_t(8));
  CHECK_EQ(geometry.indices.size(), std::size_t(12));
  CHECK_EQ(geometry.ranges.size(), std::size_t(1));
  if (!geometry.ranges.empty()) {
    CHECK_EQ(geometry.ranges[0].maps.at(0), std::string("atlas.dds"));
  }

  // Текст без жодного відомого гліфа не дає геометрії взагалі.
  const auto empty = font::buildText(*font, "\x01\x02", layout, "atlas.dds");
  CHECK(empty.indices.empty());
  CHECK(empty.ranges.empty());
}

static void testWrapText() {
  const auto font = font::parseDif(kDif);
  CHECK(font.has_value());
  if (!font) return;

  const float oneLetter = font->measure("A");
  const auto lines = font::wrapText(*font, "A A A A", oneLetter * 2.5f, 1.0f);
  CHECK(lines.size() > 1);
  // Жодне слово не губиться.
  std::string joined;
  for (const auto& line : lines) {
    if (!joined.empty()) joined += " ";
    joined += line;
  }
  CHECK_EQ(joined, std::string("A A A A"));
}

static void testLexicon() {
  loc::Lexicon lexicon;
  // Ключ, пробіли, два ESC, значення, два ESC.
  // \033 (ESC) — вісімковий запис: \x1B з'їдав би наступну шістнадцяткову
  // цифру, і "\x1BEnglish" стало б одним символом поза діапазоном.
  const std::string line1 = "HUD_INGAME_QUIT   \033\033Quit\033\033\r\n";
  const std::string line2 = "ID_LANGUAGE       \033\033English\033\033\r\n";

  CHECK(lexicon.addUtxt(utf16(line1 + line2)));
  CHECK_EQ(lexicon.size(), std::size_t(2));
  CHECK_EQ(lexicon.text("HUD_INGAME_QUIT"), std::string_view("Quit"));
  CHECK_EQ(lexicon.text("ID_LANGUAGE"), std::string_view("English"));

  // Невідомий ключ повертається як є — так само видно й у грі.
  CHECK_EQ(lexicon.text("НЕМАЄ_ТАКОГО"), std::string_view("НЕМАЄ_ТАКОГО"));
  CHECK(!lexicon.find("НЕМАЄ_ТАКОГО").has_value());
}

static void testLexiconLaterFilesOverride() {
  // Гра читає кілька файлів на мову, і патч перекриває основний.
  loc::Lexicon lexicon;
  CHECK(lexicon.addUtxt(utf16("KEY   \033\033Old\033\033\r\n")));
  CHECK(lexicon.addUtxt(utf16("KEY   \033\033New\033\033\r\n")));
  CHECK_EQ(lexicon.size(), std::size_t(1));
  CHECK_EQ(lexicon.text("KEY"), std::string_view("New"));
}

static void testUtf16Decoding() {
  // BOM пропускається, сурогатна пара складається в один символ.
  const std::vector<std::byte> withEmoji = {
      std::byte{0xFF}, std::byte{0xFE},                    // BOM
      std::byte{0x3D}, std::byte{0xD8},                    // старший сурогат
      std::byte{0x00}, std::byte{0xDE},                    // молодший
  };
  const std::string decoded = loc::utf16ToUtf8(withEmoji);
  CHECK_EQ(decoded.size(), std::size_t(4));  // U+1F600 -> 4 байти UTF-8
}

TEST_MAIN({
  testParseDif();
  testMeasureUsesKerning();
  testUtf8IsDecodedAsCodepoints();
  testBuildTextGeometry();
  testWrapText();
  testLexicon();
  testLexiconLaterFilesOverride();
  testUtf16Decoding();
})
