#pragma once
// BF2 fonts: a pair of files, `.dif` + `.dds`.
//
// The `.dif` is a **text** metrics format (no reversing was needed), and the
// `.dds` next to it is the glyph atlas. Both live in `Fonts_client.zip`, with
// separate directories per resolution (`800/`) and language (`Chinese/800/`).
//
// The `.dif` layout, taken from real files:
//
//   header
//   2                          version
//   scoreboardFont_8           name
//   128                        atlas width
//   128                        atlas height
//   8.000000                   point size / line height
//   glyphs
//   328                        how many
//   65<TAB>0.066667<TAB>4.600000<TAB>1.266667<TAB>0<TAB>35<TAB>7<TAB>40<TAB>14
//   ...
//   kerning
//   216
//   65<TAB>84<TAB>-0.466667    a character pair and its adjustment
//
// A glyph's fields: code, left bearing, width, right bearing, vertical offset,
// then the rectangle in the atlas (left, top, right, bottom). That it really is
// a rectangle is visible from the data: `!` gives 1x7, `"` gives 2x3, `A` 5x7.
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
  float width = 0.0f;   // width in pixels, fractional
  float bearingRight = 0.0f;
  int offsetY = 0;      // offset from the line's top: comma and full stop sit lower

  // The rectangle in the atlas, in pixels.
  int left = 0, top = 0, right = 0, bottom = 0;

  int pixelWidth() const { return right - left; }
  int pixelHeight() const { return bottom - top; }
  // The step to the next character, kerning aside.
  float advance() const { return bearingLeft + width + bearingRight; }
};

class Font {
 public:
  std::string name;
  int atlasWidth = 0;
  int atlasHeight = 0;
  float size = 0.0f;  // point size, also the line height

  const Glyph* glyph(std::uint32_t code) const;
  float kerning(std::uint32_t first, std::uint32_t second) const;

  // The string's width in pixels, kerning included.
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

// Reads one UTF-8 character and advances the position. Localisation strings are UTF-8.
std::uint32_t nextCodepoint(std::string_view text, std::size_t& position);

// nullopt plus an explanation: a corrupt font must not bring the engine down.
std::optional<Font> parseDif(std::string_view text, std::string* error = nullptr);

}  // namespace obf2::font
