// The scoreboard's rows: what the engine's `Scoreboard` writes into its lists and
// where the list node puts it (docs/functions/hud-scoreboard.md).
#include <cmath>
#include <map>
#include <string>

#include "check.h"
#include "obf2/con/interpreter.h"
#include "obf2/hud/hud.h"
#include "obf2/hud/list_data.h"
#include "obf2/hud/scoreboard.h"

using namespace obf2;

namespace {

class MemoryFiles : public con::FileProvider {
 public:
  std::map<std::string, std::string> files;
  std::optional<std::string> loadText(std::string_view path) override {
    const auto it = files.find(std::string(path));
    return it == files.end() ? std::nullopt : std::optional<std::string>{it->second};
  }
};

// The friendly list exactly as `HudElementsScoreboard.con` makes it.
hud::Builder friendlyList() {
  MemoryFiles files;
  files.files["hud.con"] =
      "hudBuilder.createListNode Scoreboard FriendlyScoreList 10 75 389 462 19 1\n"
      "hudBuilder.setListNodeData 2\n"
      "hudBuilder.setListNodeFont Fonts/scoreboardFontLocal_8.dif 0\n"
      "hudBuilder.setListNodeFont Fonts/scoreboardFont_8.dif 1\n"
      "hudBuilder.setListNodeBackgroundColor 0.745 0.729 0.58 0.9\n"
      "hudBuilder.setListNodeBorder 20 22 3 3\n"
      "hudBuilder.setListNodeBorderColor 0.482 0.474 0.388 1\n"
      "hudBuilder.setListNodeScrollbar 10 3\n";
  hud::Builder builder;
  con::Interpreter interpreter(files, [&](const con::Command& c) { builder.feed(c); });
  interpreter.runFile("hud.con");
  builder.finish();
  return builder;
}

hud::ScoreboardPlayer player(int index, const char* name, int team, int score) {
  hud::ScoreboardPlayer p;
  p.index = index;
  p.name = name;
  p.team = team;
  p.alive = true;
  hud::PlayerScore s;
  s.score = score;
  p.score = s;
  return p;
}

bool near(float a, float b) { return std::abs(a - b) < 0.002f; }

const std::string kLabel = "TOTAL PLAYERS: #PLAYERS#";

// Every run is ten units wide per character, its ink from 2 to 9.
std::optional<hud::RunMetrics> fixedMeasure(int, std::string_view text) {
  hud::RunMetrics metrics;
  metrics.width = 10.0f * static_cast<float>(text.size());
  metrics.top = 2.0f;
  metrics.bottom = 9.0f;
  metrics.empty = text.empty();
  return metrics;
}

}  // namespace

// `setListNodeBorder` is top, bottom, left, right (0x795720), and turns the
// border on; the font list keeps both numbers.
static void testListNodeCommands() {
  const hud::Builder builder = friendlyList();
  CHECK_EQ(builder.nodes().size(), std::size_t(1));
  if (builder.nodes().empty()) return;
  const hud::Node& node = builder.nodes().front();
  CHECK(node.listBordered);
  CHECK_EQ(node.listBorderTop, 20.0f);
  CHECK_EQ(node.listBorderBottom, 22.0f);
  CHECK_EQ(node.listBorderLeft, 3.0f);
  CHECK_EQ(node.listBorderRight, 3.0f);
  CHECK_EQ(node.listRowHeight, 19.0f);
  CHECK_EQ(node.listRowSpacing, 1.0f);  // not set in the data: the ctor's 1.0
  CHECK_EQ(node.listData, 2);
  CHECK_EQ(node.listFonts.size(), std::size_t(2));
  if (node.listFonts.size() == 2) {
    CHECK_EQ(node.listFonts[1], std::string("Fonts/scoreboardFont_8.dif"));
  }
  CHECK_EQ(node.listScrollbarWidth, 10.0f);
  CHECK_EQ(node.listScrollbarGap, 3.0f);
}

// `init` (0x7a2280): column 0 from the constructor, then the object's nine x
// positions, and a hidden column on the player lists.
static void testColumns() {
  const hud::Scoreboard board;
  const hud::ListData* friendly = board.list(2);
  const hud::ListData* total = board.list(10);
  CHECK(friendly != nullptr && total != nullptr && board.list(3) == nullptr);
  if (friendly == nullptr || total == nullptr) return;
  CHECK_EQ(friendly->columns().size(), std::size_t(11));
  const float xs[] = {0, 5, 21, 44, 212, 244, 276, 308, 340, 340};
  for (std::size_t i = 0; i < 10 && i < friendly->columns().size(); ++i) {
    CHECK_EQ(friendly->columns()[i].x, xs[i]);
  }
  if (friendly->columns().size() == 11) CHECK(!friendly->columns()[10].visible);
  CHECK_EQ(friendly->visibleRows, 21);
  CHECK_EQ(total->columns().size(), std::size_t(10));
  if (total->columns().size() > 3) CHECK(!total->columns()[3].toNext);
  CHECK_EQ(total->visibleRows, 1);
  CHECK(!total->reserveScrollbar);
}

// A player's row (0x7a38d0): frame, kit icon, rank, name, four numbers, an
// empty cell, the ping, and the hidden index.
static void testPlayerRow() {
  hud::Scoreboard board;
  hud::ScoreboardPlayer me = player(3, "me", 1, 12);
  me.score->teamwork = 4;
  me.score->kills = 5;
  me.score->deaths = 2;
  me.ping = 40;
  board.update({me}, 3, kLabel);
  const hud::ListData& list = *board.list(2);
  CHECK_EQ(list.rows(), 1);
  if (list.rows() != 1) return;
  const auto cell = [&](int column) { return list.columns()[static_cast<std::size_t>(column)].cells[0]; };
  CHECK(cell(0).isTexture);
  CHECK_EQ(cell(0).texture, std::string("Ingame/Respawn/iconframe.tga"));
  CHECK_EQ(cell(0).x, 5.0f);
  CHECK_EQ(cell(0).y, 2.0f);
  CHECK_EQ(cell(2).texture, std::string("Ingame/GeneralIcons/Ranks/rank_00.tga"));
  CHECK_EQ(cell(2).y, 1.0f);
  CHECK_EQ(cell(3).text, std::string("\xa7" "1me\xa7" "0"));
  CHECK_EQ(cell(4).text, std::string("\xa7" "112\xa7" "0"));
  CHECK_EQ(cell(5).text, std::string("\xa7" "14\xa7" "0"));   // teamwork
  CHECK_EQ(cell(6).text, std::string("\xa7" "15\xa7" "0"));   // kills
  CHECK_EQ(cell(7).text, std::string("\xa7" "12\xa7" "0"));   // deaths
  CHECK(cell(8).text.empty() && cell(8).texture.empty());
  CHECK_EQ(cell(9).text, std::string("\xa7" "140\xa7" "0"));
  CHECK_EQ(cell(4).align, 1);
  CHECK_EQ(cell(3).align, 0);
  // Blue: the friendly entry of 0xa1c7b8.
  CHECK(near(cell(3).color.g, 97.0f / 255.0f) && near(cell(3).color.b, 1.0f));
  // The local player's row is yellow at 0.35.
  CHECK(cell(0).hasBackground);
  CHECK(near(cell(0).background.a, 0.35f));
}

// Colours and icons by kind: dead is grey with the skull, the other team red
// with the faded icon, the local squad green; odd rows get a background.
static void testKindsAndRows() {
  hud::Scoreboard board;
  hud::ScoreboardPlayer me = player(1, "me", 1, 0);
  me.squad = 2;
  hud::ScoreboardPlayer mate = player(2, "mate", 1, 0);
  mate.squad = 2;
  hud::ScoreboardPlayer dead = player(4, "dead", 1, 0);
  dead.alive = false;
  hud::ScoreboardPlayer enemy = player(5, "enemy", 2, 0);
  board.update({me, mate, dead, enemy}, 1, kLabel);
  const hud::ListData& friendly = *board.list(2);
  CHECK_EQ(friendly.rows(), 3);
  if (friendly.rows() != 3) return;
  const auto& names = friendly.columns()[3].cells;
  const auto& kits = friendly.columns()[1].cells;
  CHECK(near(names[1].color.g, 119.0f / 255.0f));  // the squad mate, green
  CHECK(near(names[2].color.r, 74.0f / 255.0f));   // dead, grey
  CHECK_EQ(kits[2].texture, std::string("Ingame/Scoreboard/Icons/icon_Death.tga"));
  CHECK(!friendly.columns()[0].cells[0].hasBackground || near(friendly.columns()[0].cells[0].background.a, 0.35f));
  CHECK(friendly.columns()[0].cells[1].hasBackground);   // row 1, odd
  CHECK(near(friendly.columns()[0].cells[1].background.a, 0.8f));
  CHECK(!friendly.columns()[0].cells[2].hasBackground);  // row 2, even

  const hud::ListData& enemies = *board.list(1);
  CHECK_EQ(enemies.rows(), 1);
  if (enemies.rows() != 1) return;
  CHECK(near(enemies.columns()[3].cells[0].color.r, 253.0f / 255.0f));
  CHECK_EQ(enemies.columns()[1].cells[0].texture,
           std::string("Ingame/Scoreboard/Icons/icon_Faded.tga"));
}

// `sortByScore` (Linux 0x6815f0): score, then skill, then fewer deaths.
static void testSort() {
  hud::Scoreboard board;
  hud::ScoreboardPlayer a = player(1, "a", 1, 5);
  hud::ScoreboardPlayer b = player(2, "b", 1, 9);
  hud::ScoreboardPlayer c = player(3, "c", 1, 5);
  c.score->skill = 1;
  hud::ScoreboardPlayer d = player(4, "d", 1, 5);
  d.score->deaths = 3;
  hud::ScoreboardPlayer e = player(6, "e", 1, 5);
  board.update({a, b, c, d, e}, 1, kLabel);
  const auto& names = board.list(2)->columns()[3].cells;
  CHECK_EQ(names.size(), std::size_t(5));
  if (names.size() != 5) return;
  CHECK_EQ(names[0].text, std::string("\xa7" "1b\xa7" "0"));
  CHECK_EQ(names[1].text, std::string("\xa7" "1c\xa7" "0"));
  CHECK_EQ(names[2].text, std::string("\xa7" "1a\xa7" "0"));  // a tie keeps the order
  CHECK_EQ(names[3].text, std::string("\xa7" "1e\xa7" "0"));
  CHECK_EQ(names[4].text, std::string("\xa7" "1d\xa7" "0"));
}

// A player without a score block: the engine leaves columns 4..9 empty.
static void testNoScore() {
  hud::Scoreboard board;
  hud::ScoreboardPlayer me = player(1, "me", 1, 0);
  me.score.reset();
  board.update({me}, 1, kLabel);
  const hud::ListData& list = *board.list(2);
  for (int column = 4; column <= 9; ++column) {
    const hud::ListCell& cell = list.columns()[static_cast<std::size_t>(column)].cells[0];
    CHECK(cell.text.empty() && cell.texture.empty());
  }
}

// The total row (0x7a2b60): the label with the count, the sums, the mean ping.
static void testTotalRow() {
  hud::Scoreboard board;
  hud::ScoreboardPlayer a = player(1, "a", 2, 10);
  a.score->kills = 3;
  a.ping = 30;
  hud::ScoreboardPlayer b = player(2, "b", 2, 6);
  b.score->kills = 1;
  b.ping = 51;
  board.update({a, b}, 1, kLabel);
  const hud::ListData& total = *board.list(10);
  CHECK_EQ(total.rows(), 1);
  if (total.rows() != 1) return;
  const auto cell = [&](int column) { return total.columns()[static_cast<std::size_t>(column)].cells[0]; };
  CHECK_EQ(cell(3).text, std::string("TOTAL PLAYERS: 2"));
  CHECK_EQ(cell(3).x, -37.0f);
  CHECK_EQ(cell(4).text, std::string("\xa7" "116\xa7" "0"));
  CHECK_EQ(cell(6).text, std::string("\xa7" "14\xa7" "0"));
  CHECK_EQ(cell(9).text, std::string("\xa7" "140\xa7" "0"));  // (30 + 51) / 2
}

// Where the node puts things (0x7c5c80): the plate, the border's strips, a row's
// background once (the node remembers the rows it has drawn, 0x7c6217), a
// centred number under its header icon.
static void testPlacement() {
  const hud::Builder builder = friendlyList();
  if (builder.nodes().empty()) return;
  const hud::Node& node = builder.nodes().front();
  hud::Scoreboard board;
  hud::ScoreboardPlayer me = player(1, "me", 1, 7);
  me.ping = 5;
  hud::ScoreboardPlayer other = player(2, "xx", 1, 3);
  board.update({me, other}, 1, kLabel);
  const auto items = hud::placeList(node, board.list(2), fixedMeasure);

  // The plate first, then top, right, bottom and left strips.
  CHECK(items.size() > 5);
  if (items.size() <= 5) return;
  CHECK(near(items[0].x, 10.0f) && near(items[0].width, 389.0f) && near(items[0].height, 462.0f));
  CHECK(near(items[1].height, 20.0f));                               // top
  CHECK(near(items[2].x, 396.0f) && near(items[2].width, 3.0f));     // right
  CHECK(near(items[3].y, 75.0f + 462.0f - 22.0f));                   // bottom

  int backgrounds = 0;
  const hud::PlacedListItem* score = nullptr;
  const hud::PlacedListItem* ping = nullptr;
  const hud::PlacedListItem* name = nullptr;
  for (const hud::PlacedListItem& item : items) {
    if (item.kind == hud::PlacedListItem::Kind::Plate && near(item.height, 19.0f)) {
      ++backgrounds;
      CHECK(near(item.x, 13.0f) && near(item.width, 383.0f));
    }
    if (item.kind != hud::PlacedListItem::Kind::Text) continue;
    if (item.text == "7") score = &item;
    if (item.text == "5") ping = &item;
    if (item.text == "me") name = &item;
  }
  // Both rows have a background (the local one and the odd one), once each.
  CHECK_EQ(backgrounds, 2);
  CHECK(score != nullptr && ping != nullptr && name != nullptr);
  if (score == nullptr || ping == nullptr || name == nullptr) return;
  // Column 4 runs 212..244: centre 10 + 228 = 238, less half of 10.
  CHECK(near(score->x, 233.0f));
  // Column 9 runs 340 to 386 less floor(3 + 10): centre 10 + 340 + 16.5.
  CHECK(near(ping->x, std::floor(366.5f - 5.0f)));
  // The name at column 3's x, in font 1 (its `§1`).
  CHECK(near(name->x, 54.0f));
  CHECK_EQ(name->font, 1);
  // The first row's top is 75 + 20 + 1; the ink (7 high) centred in 19, less its top.
  CHECK(near(score->y, std::floor(96.0f + 6.0f) - 2.0f));
}

TEST_MAIN({
  testListNodeCommands();
  testColumns();
  testPlayerRow();
  testKindsAndRows();
  testSort();
  testNoScore();
  testTotalRow();
  testPlacement();
})
