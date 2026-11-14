// The spawn screen's squad tab (obf2/hud/squad_list.h): the rows `HudSquad`
// paints (OpenBF2 Menu/HudSquad.cpp) and what a click on them asks for.
#include <string>
#include <string_view>

#include "check.h"
#include "obf2/hud/squad_list.h"

using obf2::hud::SquadList;
using obf2::hud::SquadPlayer;
using obf2::hud::SquadWorld;

namespace {

std::string echo(std::string_view key) { return std::string(key); }

SquadWorld world(int ourSquad, bool leader) {
  SquadWorld w;
  w.localId = 1;
  w.players.push_back(SquadPlayer{1, "us", 2, ourSquad, leader, false, false});
  w.players.push_back(SquadPlayer{250, "bot a", 2, 1, true, false, true});
  w.players.push_back(SquadPlayer{251, "bot b", 2, 1, false, false, true});
  w.players.push_back(SquadPlayer{252, "enemy", 1, 1, true, false, true});
  w.name = [](int, int) { return std::string(); };
  w.locked = [](int, int) { return false; };
  return w;
}

int rowTypeAt(const SquadList& list, int row) {
  const auto& cells = list.list(5)->columns()[14].cells;
  return std::stoi(cells[static_cast<std::size_t>(row)].text);
}

}  // namespace

// Not in a squad: the commander band, the create row, then each squad of our
// team — the enemy's squad 1 is not ours. Squads are closed until opened.
static void testRowsOutsideASquad() {
  SquadList list;
  list.update(world(0, false), echo);
  CHECK(list.list(5) != nullptr);
  CHECK(list.list(8) == nullptr);
  CHECK_EQ(list.list(5)->rows(), 3);
  CHECK_EQ(rowTypeAt(list, 0), 0);
  CHECK_EQ(rowTypeAt(list, 1), 3);
  CHECK_EQ(rowTypeAt(list, 2), 2);
  CHECK(!list.inSquad());
  CHECK_EQ(list.createTag(), std::string("2"));  // squad 1 is taken
  CHECK(list.unassignedText().find("#UNASSIGNED#") == std::string::npos ||
        list.unassignedText() == "HUD_TEXT_MENU_SPAWN_SQUAD_NUMBER_OF_UNASSIGNED");
}

// The squad row's button joins; the create row's creates the first empty squad
// with its default name; the arrow opens the squad's members.
static void testClicks() {
  SquadList list;
  list.update(world(0, false), echo);
  const auto join = list.click(2, 10);
  CHECK(join.kind == SquadList::Action::Kind::Join && join.squad == 1);
  const auto create = list.click(1, 10);
  CHECK(create.kind == SquadList::Action::Kind::Create && create.squad == 2 &&
        create.name == "Bravo");
  list.click(2, 12);
  list.update(world(0, false), echo);
  CHECK_EQ(list.list(5)->rows(), 5);  // squad 1 open: its leader and its member
}

// In a squad of our own as its leader: no create row, the button leaves, and the
// lock turns the squad private.
static void testLeaderOfASquad() {
  SquadList list;
  list.update(world(3, true), echo);
  CHECK(list.inSquad());
  CHECK(list.squadLeader());
  // band, squad 1, squad 3 and (opened, ours) our row
  CHECK_EQ(rowTypeAt(list, 1), 2);
  const auto leave = list.click(2, 10);
  CHECK(leave.kind == SquadList::Action::Kind::Leave && leave.squad == 3);
  const auto lock = list.click(2, 8);
  CHECK(lock.kind == SquadList::Action::Kind::SetPrivate && lock.squad == 3 && lock.flag);
  // Another squad's lock is not ours to turn.
  CHECK(list.click(1, 8).kind == SquadList::Action::Kind::None);
}

TEST_MAIN({
  testRowsOutsideASquad();
  testClicks();
  testLeaderOfASquad();
})
