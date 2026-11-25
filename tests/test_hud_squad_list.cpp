// The squad tab: what `SquadMenuNew` writes into `SquadMemberList` and what its
// commands ask for (docs/functions/squad-menu.md).
#include <string>

#include "check.h"
#include "obf2/hud/squad_list.h"

using namespace obf2;

namespace {

hud::SquadWorld world() {
  hud::SquadWorld w;
  w.available = true;
  w.localIndex = 1;
  w.team = 2;
  w.squad = 0;
  w.teamPlayers = 4;
  w.unassigned = 1;
  w.squads[1].name = "Alpha";
  w.squads[1].members = {{3, "Leader", true, true, {}}, {4, "Rifle", false, false, {}}};
  w.squads[3].name = "Charlie";
  w.squads[3].locked = true;
  w.squads[3].members = {{5, "Medic", true, true, {}}};
  return w;
}

int kind(const hud::ListData& list, int row) { return std::stoi(list.text(14, row)); }

void pick(hud::SquadList& menu, int row, int column) {
  menu.memberList().selectedRow = row;
  menu.memberList().selectedColumn = column;
}

// The invite list (docs/functions/squad-menu.md, "The invite list"): we lead
// Alpha (1) with one more member; two of the team are in no squad, one leads
// Charlie (3).
void inviteList() {
  hud::SquadWorld w;
  w.available = true;
  w.localIndex = 1;
  w.team = 2;
  w.squad = 1;
  w.leader = true;
  w.squads[1].name = "Alpha";
  w.squads[1].members = {{1, "Me", true, true, {}}, {4, "Rifle", true, false, {}}};
  w.squads[3].members = {{5, "Medic", true, true, {}}};
  w.teamPlayerList = {{1, "Me", true, 1, false}, {4, "Rifle", true, 1, false}, {5, "Medic", true, 3, false},
                      {6, "Loner", true, 0, false}, {7, "Ghost", false, 0, false}, {8, "Boss", true, 0, true}};
  hud::SquadList menu;
  menu.update(w, true);
  menu.setShowInviteList(true);
  hud::ListData& list = menu.inviteList();
  // SQUAD/KICK, Rifle (not us), 4 empty places (6 − 2 members), TEAM/INVITE,
  // then the team outside our squad: Medic, Loner, Ghost (not the commander).
  CHECK(list.rows() == 1 + 1 + 4 + 1 + 3);
  CHECK(list.text(0, 0) == "HUD_SQUADTAB_SQUAD" && list.text(1, 0) == "HUD_SQUADTAB_KICK");
  CHECK(list.text(2, 1) == "4");
  CHECK(list.text(2, 2) == "-1" && list.text(2, 5) == "-1");
  CHECK(list.text(0, 6) == "HUD_SQUADTAB_TEAM" && list.text(1, 6) == "HUD_SQUADTAB_INVITE");
  CHECK(list.text(2, 7) == "5" && list.text(2, 8) == "6" && list.text(2, 9) == "7");

  const auto click = [&](int row) {
    list.selectedRow = row;
    list.selectedColumn = 1;
    return menu.singleClickInvite();
  };
  const auto rowOf = [&](int index) {
    for (int r = 0; r < list.rows(); ++r)
      if (list.text(2, r) == std::to_string(index)) return r;
    return -1;
  };
  // Mark Rifle to kick (a place more: 6 − 2 + 1), pick Loner (the pending row
  // and a place fewer again).
  CHECK(click(1));
  CHECK(list.rows() == 1 + 1 + 5 + 1 + 3);
  CHECK(click(rowOf(6)));
  CHECK(list.text(2, 1) == "4");
  CHECK(list.text(2, 2) == "6");  // Loner pending, then 6 − 2 − 1 + 1 = 4 places
  CHECK(list.rows() == 1 + 1 + 1 + 4 + 1 + 3);
  // APPLY: the kick first, then the invite.
  const auto actions = menu.applyInviteList();
  CHECK(actions.size() == 2);
  CHECK(actions[0].kind == hud::SquadList::Action::Kind::Kick && actions[0].player == 4 && actions[0].squad == 1);
  CHECK(actions[1].kind == hud::SquadList::Action::Kind::Invite && actions[1].player == 6);

  // The room: 6 − members (2) = 4 picks, the fifth refused.
  menu.setShowInviteList(true);
  for (int i = 10; i < 15; ++i) w.teamPlayerList.push_back({i, "Bot", true, 0, false});
  menu.update(w, true);
  int refused = 0;
  for (int index = 10; index < 15; ++index) refused += click(rowOf(index)) ? 0 : 1;
  CHECK(refused == 1);
}

}  // namespace

void run() {
  inviteList();
  hud::SquadList menu;
  const hud::ListData& list = *menu.list(5);

  // Columns as init 0x78dcb0 makes them: 13 visible, then the id and the kind.
  CHECK(list.columns().size() == 15);
  CHECK(list.columns()[4].x == 45.0f && list.columns()[4].width == 166.0f);
  CHECK(!list.columns()[13].visible && !list.columns()[14].visible);
  CHECK(list.visibleRows == 21);

  // Hidden: no rows.
  menu.update(world(), false);
  CHECK(list.rows() == 0);

  // Shown, in no squad: commander row, create row, then the two squads with members,
  // collapsed (only our own squad opens on show, and we have none).
  menu.update(world(), true);
  CHECK(list.rows() == 4);
  CHECK(kind(list, 0) == 0);
  CHECK(kind(list, 1) == 3);
  CHECK(kind(list, 2) == 2 && list.text(13, 2) == "1");
  CHECK(kind(list, 3) == 2 && list.text(13, 3) == "3");
  // The squad's name and count in Retail's font codes (0x932030, 0x932024);
  // the name upper-cased by 0x4311b0.
  CHECK(list.text(5, 2) == "\xa7" "1ALPHA\xa7" "0");
  CHECK(list.text(6, 2) == "\xa7" "12/6\xa7" "0");

  // The arrow (column 12) opens a squad: its two members follow, leader first.
  pick(menu, 2, 12);
  CHECK(!menu.singleClickNew());
  CHECK(list.rows() == 6);
  CHECK(kind(list, 3) == 4 && list.text(13, 3) == "3");
  CHECK(kind(list, 4) == 4 && list.text(13, 4) == "4");
  // The leader's squad mark in column 9.
  CHECK(list.columns()[9].cells[3].texture == "Ingame/Player/Icons/Minimap/mini_Squad_1_small.tga");
  // A dead member's kit icon is the death icon (0x74f320).
  CHECK(list.columns()[3].cells[4].texture == "Ingame/Scoreboard/Icons/icon_Death.tga");

  // The button column (10) on a squad we are not in: join.
  pick(menu, 2, 10);
  auto join = menu.singleClickNew();
  CHECK(join && join->kind == hud::SquadList::Action::Kind::Join && join->squad == 1);

  // Double clicks are 0x83d270, a bare `ret`: nothing to test but that there is no
  // command for them in the menu.

  // In squad 1 as its leader: the button leaves, the lock column locks.
  auto led = world();
  led.squad = 1;
  led.leader = true;
  led.localIndex = 3;
  menu.update(led, true);
  int alphaRow = -1;
  for (int r = 0; r < list.rows(); ++r)
    if (kind(list, r) == 2 && list.text(13, r) == "1") alphaRow = r;
  CHECK(alphaRow >= 0);
  pick(menu, alphaRow, 10);
  auto leave = menu.singleClickNew();
  CHECK(leave && leave->kind == hud::SquadList::Action::Kind::Leave);
  pick(menu, alphaRow, 8);
  auto lock = menu.singleClickNew();
  CHECK(lock && lock->kind == hud::SquadList::Action::Kind::Lock && lock->squad == 1);
  // In a squad there is no create row.
  for (int r = 0; r < list.rows(); ++r) CHECK(kind(list, r) != 3);

  // Create: the first free squad (2) with the first default name no squad has.
  menu.update(world(), true);
  menu.setSquadCreateSelect(true);
  bool shown = false;
  CHECK(menu.boolVariable("SquadCreateShow", shown) && shown);
  // The name box starts with that default (vtbl[0x284]) and is edit string 2.
  CHECK(menu.editString(2) != nullptr && *menu.editString(2) == "Bravo");
  CHECK(menu.editString(1) == nullptr && menu.editString(3) == nullptr);
  // The name box's button focuses it only while the panel is up (0x78f190);
  // typed text stops at the edit node's 10 characters.
  CHECK(menu.setSquadNameSelect() && menu.nameFocused());
  menu.setNameBox("Nightwatchmen");
  CHECK(*menu.editString(2) == "Nightwatch");
  menu.setNameBox("Bravo");
  auto create = menu.createSquad();
  CHECK(create && create->kind == hud::SquadList::Action::Kind::Create && create->squad == 2);
  CHECK(create->name == "Bravo");
  CHECK(!menu.nameFocused() && !menu.setSquadNameSelect());  // CREATE shut the panel
  CHECK(menu.boolVariable("SquadCreateShow", shown) && !shown);
  // The server then puts us in squad 2 as its leader: it opens at once, our
  // own row followed by its member (HUD events 0x16 / 0x18, 0x78fa60).
  auto made = world();
  made.squad = 2;
  made.leader = true;
  made.squads[2].name = "Bravo";
  made.squads[2].members = {{1, "Me", true, true, {}}};
  const auto meShown = [&] {
    for (int r = 0; r < list.rows(); ++r)
      if (kind(list, r) == 4 && list.text(13, r) == "1") return true;
    return false;
  };
  menu.update(made, true);
  CHECK(meShown());
  // The same squad again does not re-open it once closed by its arrow.
  for (int r = 0; r < list.rows(); ++r)
    if (kind(list, r) == 2 && list.text(13, r) == "2") pick(menu, r, 12);
  menu.singleClickNew();
  CHECK(!meShown());
  menu.update(made, true);
  CHECK(!meShown());
  menu.update(world(), true);  // back out of the squad for what follows

  // The commander row's column 10: APPLY with no commander, RESIGN as one.
  pick(menu, 0, 10);
  auto apply = menu.singleClickNew();
  CHECK(apply && apply->kind == hud::SquadList::Action::Kind::CommanderApply);
  auto commanding = world();
  commanding.commander = true;
  commanding.commanderIndex = 1;
  menu.update(commanding, true);
  pick(menu, 0, 10);
  auto resign = menu.singleClickNew();
  CHECK(resign && resign->kind == hud::SquadList::Action::Kind::CommanderResign);
  // Someone else commanding: a mutiny needs 0x597a40 (not read), nothing is sent.
  auto other = world();
  other.commanderIndex = 5;
  menu.update(other, true);
  pick(menu, 0, 10);
  CHECK(!menu.singleClickNew());
  menu.update(world(), true);

  // The create row's button does the same.
  pick(menu, 1, 10);
  auto fromRow = menu.singleClickNew();
  CHECK(fromRow && fromRow->kind == hud::SquadList::Action::Kind::Create);

  // NrOfSquads with the lexicon's markers.
  menu.text = [](const std::string& key) {
    return key == "HUD_TEXT_MENU_SPAWN_SQUAD_NUMBER_OF_SQUADS" ? std::string("#ACTIVE#/#TOTAL#") : key;
  };
  menu.update(world(), true);
  std::string squads;
  CHECK(menu.stringVariable("NrOfSquads", squads) && squads == "2/9");

}

TEST_MAIN({ run(); })
