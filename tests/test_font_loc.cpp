#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/font/text.h"
#include "obf2/loc/lexicon.h"

using namespace obf2;

namespace {

// The shape is taken from a real 800/scoreboardFont_8.dif.
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
    std::fprintf(stderr, "  reason: %s\n", error.c_str());
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
    // The rectangle in the atlas: (35,7)-(40,14) is 5x7.
    CHECK_EQ(letterA->pixelWidth(), 5);
    CHECK_EQ(letterA->pixelHeight(), 7);
    CHECK_EQ(letterA->offsetY, 0);
  }

  // A space has no rectangle but has an advance.
  const font::Glyph* space = font->glyph(' ');
  CHECK(space != nullptr);
  if (space != nullptr) {
    CHECK_EQ(space->pixelWidth(), 0);
    CHECK(space->advance() > 4.3f && space->advance() < 4.4f);
  }

  CHECK(font->kerning('A', 'T') < -0.4f);
  CHECK_EQ(font->kerning('A', 'B'), 0.0f);  // there is no such pair
}

static void testMeasureUsesKerning() {
  const auto font = font::parseDif(kDif);
  CHECK(font.has_value());
  if (!font) return;

  const float single = font->measure("A");
  const float doubled = font->measure("AA");
  // Two "A"s with negative kerning are shorter than twice the width of one.
  CHECK(doubled < single * 2.0f);
  CHECK(doubled > single);
}

static void testUtf8IsDecodedAsCodepoints() {
  const auto font = font::parseDif(kDif);
  CHECK(font.has_value());
  if (!font) return;

  // U+2019 (’) in UTF-8 is three bytes. Read byte by byte, glyph 8217 would not be
  // found, and the screen would show rubbish instead of an apostrophe.
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
  // Two "A"s give four vertices each; a space adds no geometry.
  CHECK_EQ(geometry.vertices.size(), std::size_t(8));
  CHECK_EQ(geometry.indices.size(), std::size_t(12));
  CHECK_EQ(geometry.ranges.size(), std::size_t(1));
  if (!geometry.ranges.empty()) {
    CHECK_EQ(geometry.ranges[0].maps.at(0), std::string("atlas.dds"));
  }

  // Text with not a single known glyph gives no geometry at all.
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
  // Not a word is lost.
  std::string joined;
  for (const auto& line : lines) {
    if (!joined.empty()) joined += " ";
    joined += line;
  }
  CHECK_EQ(joined, std::string("A A A A"));
}

static void testLexicon() {
  loc::Lexicon lexicon;
  // The key, spaces, two ESCs, the value, two ESCs.
  // \033 (ESC) is written in octal: \x1B would eat the next hexadecimal digit, and
  // "\x1BEnglish" would become one character outside the range.
  const std::string line1 = "HUD_INGAME_QUIT   \033\033Quit\033\033\r\n";
  const std::string line2 = "ID_LANGUAGE       \033\033English\033\033\r\n";

  CHECK(lexicon.addUtxt(utf16(line1 + line2)));
  CHECK_EQ(lexicon.size(), std::size_t(2));
  CHECK_EQ(lexicon.text("HUD_INGAME_QUIT"), std::string_view("Quit"));
  CHECK_EQ(lexicon.text("ID_LANGUAGE"), std::string_view("English"));

  // An unknown key comes back as it is — the same is visible in the game.
  CHECK_EQ(lexicon.text("NO_SUCH_KEY"), std::string_view("NO_SUCH_KEY"));
  CHECK(!lexicon.find("NO_SUCH_KEY").has_value());
}

static void testLexiconLaterFilesOverride() {
  // The game reads several files per language, and a patch overrides the main one.
  loc::Lexicon lexicon;
  CHECK(lexicon.addUtxt(utf16("KEY   \033\033Old\033\033\r\n")));
  CHECK(lexicon.addUtxt(utf16("KEY   \033\033New\033\033\r\n")));
  CHECK_EQ(lexicon.size(), std::size_t(1));
  CHECK_EQ(lexicon.text("KEY"), std::string_view("New"));
}

static void testUtf16Decoding() {
  // The BOM is skipped, a surrogate pair is combined into one character.
  const std::vector<std::byte> withEmoji = {
      std::byte{0xFF}, std::byte{0xFE},                    // BOM
      std::byte{0x3D}, std::byte{0xD8},                    // the high surrogate
      std::byte{0x00}, std::byte{0xDE},                    // the low one
  };
  const std::string decoded = loc::utf16ToUtf8(withEmoji);
  CHECK_EQ(decoded.size(), std::size_t(4));  // U+1F600 -> 4 UTF-8 bytes
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
