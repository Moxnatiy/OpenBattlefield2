#include "obf2/hud/list_data.h"

#include <algorithm>
#include <cmath>

namespace obf2::hud {

ListData::ListData() { addColumn(0.0f, true); }

void ListData::addColumn(float x, bool toNext) {
  ListColumn column;
  column.x = x;
  column.toNext = toNext;
  columns_.push_back(std::move(column));
}

void ListData::addHiddenColumn() {
  // 0x7c59b0: x 0, not visible, `toNext` 1.
  ListColumn column;
  column.visible = false;
  column.toNext = true;
  columns_.push_back(std::move(column));
}

void ListData::clear() {
  for (ListColumn& column : columns_) column.cells.clear();
}

void ListData::addText(int column, std::string text, float x, float y, Color color, int align) {
  if (column < 0 || static_cast<std::size_t>(column) >= columns_.size()) return;
  ListCell cell;
  cell.text = std::move(text);
  cell.x = x;
  cell.y = y;
  cell.color = color;
  cell.align = align;
  columns_[static_cast<std::size_t>(column)].cells.push_back(std::move(cell));
}

void ListData::addTexture(int column, std::string texture, float width, float height, float x,
                          float y, Color color) {
  if (column < 0 || static_cast<std::size_t>(column) >= columns_.size()) return;
  ListCell cell;
  cell.texture = std::move(texture);
  cell.isTexture = true;
  cell.width = width;
  cell.height = height;
  cell.x = x;
  cell.y = y;
  cell.color = color;
  columns_[static_cast<std::size_t>(column)].cells.push_back(std::move(cell));
}

void ListData::addEmpty(int column) {
  // 0x7c8090 with "": a texture cell by name, no texture and no size.
  if (column < 0 || static_cast<std::size_t>(column) >= columns_.size()) return;
  columns_[static_cast<std::size_t>(column)].cells.push_back(ListCell{});
}

void ListData::setRowBackground(int row, Color color, float x, float y, float height) {
  // 0x7c4af0 walks the columns for the first that has a cell at `row`; column 0
  // always has, since every row starts there.
  for (ListColumn& column : columns_) {
    if (row < 0 || static_cast<std::size_t>(row) >= column.cells.size()) continue;
    ListCell& cell = column.cells[static_cast<std::size_t>(row)];
    cell.hasBackground = true;
    cell.background = color;
    cell.backgroundX = x;
    cell.backgroundY = y;
    cell.backgroundHeight = height;
    return;
  }
}

int ListData::rows() const {
  return columns_.empty() ? 0 : static_cast<int>(columns_.front().cells.size());
}

namespace {

// A column's right edge in the node, 0x7c4d20.
float columnRight(const Node& node, const ListData& data, std::size_t index) {
  const int right = node.listBordered ? static_cast<int>(node.listBorderRight) : 0;
  const int inner = static_cast<int>(std::floor(node.width)) - right;
  const ListColumn& column = data.columns()[index];
  int edge = inner;
  if (data.columnsToNext && column.toNext) {
    for (std::size_t next = index + 1; next < data.columns().size(); ++next) {
      if (!data.columns()[next].visible) continue;
      edge = static_cast<int>(data.columns()[next].x);
      break;
    }
  }
  if (column.width > 0.0f && column.width < static_cast<float>(edge)) {
    edge = static_cast<int>(column.width + column.x);
  }
  if (data.reserveScrollbar) {
    const int reserve =
        static_cast<int>(std::floor(node.listScrollbarGap + node.listScrollbarWidth));
    if (inner - reserve < edge) edge -= reserve;
  } else if (inner - 6 < edge) {
    edge -= 6;
  }
  return static_cast<float>(edge);
}

struct Run {
  std::string text;
  int font = 0;
  bool highlight = false;
};

// The codes a list text carries (0x7c66ec..0x7c6dc6): `§0`..`§3` pick a font and
// take two characters, `§C`/`§c` toggles the highlight and takes six. The engine's
// strings are Latin-1, so § is the byte 0xa7.
std::vector<Run> splitRuns(std::string_view text, int font) {
  std::vector<Run> runs;
  Run current{std::string(), font, false};
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    const char next = i + 1 < text.size() ? text[i + 1] : '\0';
    if (c == 0xa7 && next >= '0' && next <= '3') {
      if (!current.text.empty()) runs.push_back(current);
      current.text.clear();
      current.font = next - '0';
      i += 2;
      continue;
    }
    if (c == 0xa7 && (next == 'C' || next == 'c')) {
      if (!current.text.empty()) runs.push_back(current);
      current.text.clear();
      current.highlight = !current.highlight;
      i += 6;
      continue;
    }
    current.text.push_back(text[i]);
    ++i;
  }
  if (!current.text.empty()) runs.push_back(current);
  return runs;
}

}  // namespace

std::vector<PlacedListItem> placeList(const Node& node, const ListData* data,
                                      const TextMeasure& measure) {
  std::vector<PlacedListItem> out;
  const float x0 = node.absX;
  const float y0 = node.absY;
  const float top = node.listBordered ? node.listBorderTop : 0.0f;
  const float bottom = node.listBordered ? node.listBorderBottom : 0.0f;
  const float left = node.listBordered ? node.listBorderLeft : 0.0f;
  const float right = node.listBordered ? node.listBorderRight : 0.0f;

  auto plate = [&](float x, float y, float w, float h, Color color) {
    if (w <= 0.0f || h <= 0.0f || color.a <= 0.0f) return;
    PlacedListItem item;
    item.kind = PlacedListItem::Kind::Plate;
    item.x = x;
    item.y = y;
    item.width = w;
    item.height = h;
    item.color = color;
    out.push_back(std::move(item));
  };

  // The plate over the whole node, then the border's four strips over it
  // (0x7c5c80, before the rows). A colour with no alpha is left out: it would
  // draw nothing.
  plate(x0, y0, node.width, node.height, node.listBackground);
  if (node.listBordered) {
    plate(x0, y0, node.width, top, node.listBorderColor);
    plate(x0 + node.width - right, y0 + top, right, node.height - top, node.listBorderColor);
    plate(x0, y0 + node.height - bottom, node.width - right, bottom, node.listBorderColor);
    plate(x0, y0 + top, left, node.height - top - bottom, node.listBorderColor);
  }
  if (data == nullptr) return out;

  const int rows = std::min(data->visibleRows, data->rows());
  const float rowHeight = node.listRowHeight;
  const float spacing = node.listRowSpacing;
  const std::vector<ListColumn>& columns = data->columns();

  for (std::size_t c = 0; c < columns.size(); ++c) {
    const ListColumn& column = columns[c];
    float rowY = y0 + top + spacing;
    for (int r = 0; r < rows; ++r, rowY += spacing + rowHeight) {
      // The row's background, from column 0's cell. It sits inside the column loop,
      // but the node keeps a list of the rows whose background is down
      // (searched at 0x7c6217, added to at 0x7c62b8), so it is drawn once: in
      // column 0's pass, under every cell of the row.
      const ListCell& first = columns.front().cells[static_cast<std::size_t>(r)];
      if (c == 0 && first.hasBackground) {
        const float x = x0 + left + first.backgroundX;
        plate(x, rowY + first.backgroundY, node.width - first.backgroundX - left - right,
              first.backgroundHeight, first.background);
      }
      if (!column.visible) continue;
      if (rowY > y0 + top + node.height - bottom - rowHeight) continue;
      if (static_cast<std::size_t>(r) >= column.cells.size()) continue;
      const ListCell& cell = column.cells[static_cast<std::size_t>(r)];

      if (cell.isTexture) {
        if (cell.texture.empty()) continue;
        PlacedListItem item;
        item.kind = PlacedListItem::Kind::Texture;
        item.x = x0 + column.x + cell.x;
        item.y = rowY + cell.y;
        item.width = cell.width;
        item.height = cell.height;
        item.texture = cell.texture;
        item.color = cell.color;
        out.push_back(std::move(item));
        continue;
      }
      if (cell.text.empty() || !measure) continue;

      // The runs and the whole text's width and ink. The ink's top starts at 10
      // and its bottom at 0 (0x7c6700: `local_7c = 10.0`, `local_d4 = 0`).
      const std::vector<Run> runs = splitRuns(cell.text, cell.font);
      float width = 0.0f;
      float inkTop = 10.0f;
      float inkBottom = 0.0f;
      std::vector<float> widths;
      for (const Run& run : runs) {
        const std::optional<RunMetrics> metrics = measure(run.font, run.text);
        const float w = metrics ? metrics->width : 0.0f;
        widths.push_back(w);
        width += w;
        if (metrics && !metrics->empty) {
          inkTop = std::min(inkTop, metrics->top);
          inkBottom = std::max(inkBottom, metrics->bottom);
        }
      }

      float x = x0 + column.x + cell.x;
      if (cell.align == 1) {
        const float edge = columnRight(node, *data, c);
        x = x0 + column.x + cell.x + std::floor((edge - column.x)) * 0.5f - width * 0.5f;
      } else if (cell.align == 2) {
        x = x0 + columnRight(node, *data, c) - cell.x - width;
      }
      x = std::floor(x);
      // Vertical alignment 1 centres the ink in the row (0x7c7366).
      float inkY = rowY;
      if (cell.verticalAlign == 1) inkY = rowY + (rowHeight - (inkBottom - inkTop)) * 0.5f;
      else if (cell.verticalAlign == 4) inkY = rowY + rowHeight;
      if (cell.verticalAlign == 3) inkTop = 0.0f;
      inkY = std::floor(inkY);

      for (std::size_t i = 0; i < runs.size(); ++i) {
        PlacedListItem item;
        item.kind = PlacedListItem::Kind::Text;
        item.x = x;
        item.y = inkY - inkTop;
        item.text = runs[i].text;
        item.font = runs[i].font;
        item.color = runs[i].highlight ? kListHighlight : cell.color;
        out.push_back(std::move(item));
        x += widths[i];
      }
    }
  }
  return out;
}

}  // namespace obf2::hud
