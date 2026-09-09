#pragma once
// Building text geometry: a string -> quads with atlas coordinates.
//
// The renderer draws this with the same pipeline as everything else, so text
// becomes an ordinary RenderMesh with one texture — the font's atlas.
#include <string>
#include <string_view>
#include <vector>

#include "obf2/font/dif_font.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::font {

struct TextLayout {
  float x = 0.0f;      // left and up, in screen pixels
  float y = 0.0f;
  float scale = 1.0f;  // a multiplier on the point size
  int screenWidth = 1280;
  int screenHeight = 720;
};

// Geometry in NDC coordinates, ready to draw without a matrix.
// An empty result means not a single glyph was found.
mesh::RenderMesh buildText(const Font& font, std::string_view text, const TextLayout& layout,
                           const std::string& atlasPath);

// Splits the text into lines that fit the given width. Wrapping happens only at
// spaces: a word longer than the line stays whole and overflows the edge —
// which is what the original does too.
std::vector<std::string> wrapText(const Font& font, std::string_view text, float maxWidth,
                                  float scale);

// How many pixels a string takes — for centring or right-aligning it.
float textWidth(const Font& font, std::string_view text, float scale);

}  // namespace obf2::font
