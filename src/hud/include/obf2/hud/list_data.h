#pragma once
// What a list node shows: the engine's `ListBoxData` (vtable 0x936880, ctor
// 0x7c7fd0) — columns, each holding one cell per row — and where the list node
// (`Bf2NewListBoxNode`, render 0x7c5c80) puts every cell of it.
//
// The whole of it is in docs/functions/hud-scoreboard.md. Nothing here draws:
// `placeList` gives back positions and colours, and render.cpp turns them into
// geometry — the same split the engine makes between the node and the card.
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/hud/hud.h"

namespace obf2::hud {

// A cell (0x94 bytes in the engine). Offsets name where each field lives.
struct ListCell {
  std::string text;      // +0x00 (or the wide one at +0x1c)
  std::string texture;   // +0x4c, a handle there; a path here
  bool isTexture = false;  // +0x60
  float width = 0.0f, height = 0.0f;  // +0x50, +0x54 — a texture's size
  float x = 0.0f, y = 0.0f;           // +0x58, +0x5c
  Color color;                        // +0x3c..+0x48
  int align = 0;          // +0x64: 0 left, 1 centred, 2 right (0x7c5c80)
  int verticalAlign = 1;  // +0x68: always 1 from the constructors (0x7c2bf0)
  int font = 0;           // +0x90: the font a text starts with
  // The row's background (0x7c4af0). Kept on column 0's cell of the row.
  bool hasBackground = false;  // +0x6c
  Color background;            // +0x70..+0x7c
  float backgroundX = 0.0f;    // +0x84
  float backgroundY = 0.0f;    // +0x88
  float backgroundHeight = 0.0f;  // +0x80
};

// A column (0x24 bytes, 0x7c5850).
struct ListColumn {
  float x = 0.0f;         // +0x14
  bool visible = true;    // +0x18: 0 only for addHiddenColumn (0x7c59b0)
  bool toNext = false;    // +0x19: its right edge is the next visible column's x
  float width = -1.0f;    // +0x20
  std::vector<ListCell> cells;  // +0x08..+0x10, one per row
};

class ListData {
 public:
  // The constructor adds column 0 at x 0 with `toNext` set (0x7c7fd0 → 0x7c5850(0, 1, 0)).
  ListData();

  void addColumn(float x, bool toNext);  // 0x7c5850
  void addHiddenColumn();                // 0x7c59b0
  void clear();                          // 0x7c89b0: every column's cells

  // Text and texture cells, appended to a column (0x7c8120, 0x7c8260).
  void addText(int column, std::string text, float x, float y, Color color, int align);
  void addTexture(int column, std::string texture, float width, float height, float x,
                  float y, Color color);
  // A cell with nothing in it: the engine's empty texture name (0x7c8090 with "").
  void addEmpty(int column);
  // The row's background, on column 0's cell of that row (0x7c4af0).
  void setRowBackground(int row, Color color, float x, float y, float height);

  int rows() const;  // the cells in column 0
  const std::vector<ListColumn>& columns() const { return columns_; }

  // `+0x2c`: how many rows the node shows (0x14 from the constructor).
  int visibleRows = 20;
  // `+0x21`: the last column gives way to the scrollbar (0x7c4d20).
  bool reserveScrollbar = true;
  // `+0x22`: a column with `toNext` ends where the next visible one starts.
  bool columnsToNext = true;

 private:
  std::vector<ListColumn> columns_;
};

// One thing the list node draws, in HUD units (800x600).
struct PlacedListItem {
  enum class Kind { Plate, Text, Texture };
  Kind kind = Kind::Plate;
  // Plate and Texture: the rectangle. Text: `x` is the pen's start and `y` the
  // line's top, as `font::buildText` takes them.
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
  Color color;
  std::string text;     // Text: one run, the § codes taken out
  std::string texture;  // Texture
  int font = 0;         // Text: the node's font number (`setListNodeFont <file> <n>`)
};

// A run of text in one font: how wide it is, and its glyphs' ink from the
// highest top to the lowest bottom, measured from the line's top. `nullopt` when
// the node has no font of that number.
struct RunMetrics {
  float width = 0.0f;
  float top = 0.0f;
  float bottom = 0.0f;
  bool empty = true;  // no glyph found: top and bottom mean nothing
};
using TextMeasure = std::function<std::optional<RunMetrics>(int font, std::string_view text)>;

// Everything a list node draws for `data`, in order: the plate and border, then
// per column the rows' backgrounds and cells (0x7c5c80).
std::vector<PlacedListItem> placeList(const Node& node, const ListData* data,
                                      const TextMeasure& measure);

// The highlight colour `§C` switches to: 0x3f7efeff, 0x3f23a3a4, 0x3e2cacad, 1.0,
// stored just before the glyph draw at 0x7c7497.
inline constexpr Color kListHighlight{0.996f, 0.64f, 0.169f, 1.0f};

}  // namespace obf2::hud
