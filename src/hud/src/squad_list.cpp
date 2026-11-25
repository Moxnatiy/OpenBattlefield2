#include "obf2/hud/squad_list.h"

#include "obf2/hud/text_case.h"

#include <algorithm>
#include <cstdlib>

namespace obf2::hud {

namespace {

constexpr float k255 = 1.0f / 255.0f;

// The colours, from `BF2.exe` .data (tools/pe_read.py) or the tables static
// initializers fill.
constexpr Color kText{191 * k255, 191 * k255, 143 * k255, 1.0f};     // 0x97de20
constexpr Color kFrame{0.576f, 0.573f, 0.451f, 1.0f};                // 0x97e2b0
constexpr Color kKitIcon{98 * k255, 92 * k255, 65 * k255, 1.0f};     // 0x97e2a0
constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};                      // 0x97dd50
constexpr Color kButton{1.0f, 0.792f, 0.043f, 1.0f};                 // 0x97de30
constexpr Color kHeaderRow{124 * k255, 122 * k255, 99 * k255, 1.0f}; // 0x97de10
constexpr Color kOddRow{185 * k255, 181 * k255, 148 * k255, 1.0f};   // 0x97de00, alpha 1 here (0x790a62)
constexpr Color kLocalRow{0.996f, 0.7529412f, 0.08235294f, 0.35f};   // 0x7909e6..0x790a06
// 0xa1ceb8: the picked squad or player, from 0x97e0e0 (init 0x865b70).
constexpr Color kPicked{1.0f, 1.0f, 1.0f, 1.0f};
// 0xa1c758: our own squad and the commander's own row, from 0xa1c9e8 (init
// 0x8650c0), itself from 0x97dd70 (init 0x8644d0).
constexpr Color kOwn{114 * k255, 227 * k255, 88 * k255, 1.0f};
// The name colours of a member row (0x7904a6..0x790571) are the scoreboard's
// friendly entries (+0x20) of its tables: dead 0xa1cf78, own squad 0xa1cc98,
// otherwise 0xa1c7d8 (docs/functions/hud-scoreboard.md).
constexpr Color kDead{74 * k255, 69 * k255, 53 * k255, 1.0f};
constexpr Color kSquadName{8 * k255, 119 * k255, 2 * k255, 1.0f};
constexpr Color kFriendly{0.0f, 97 * k255, 1.0f, 1.0f};
// An invite row's name outside our squad: 0xa1c798 (from 0x97e0b0, init
// 0x864d50), its alpha set to 0.5 by the row itself (0x790b80).
constexpr Color kOutside{1.0f, 0.9f, 0.2f, 0.5f};

// The invite list's checkboxes (ctor 0x791260: +0x1c, +0x20, +0x24, +0x28).
constexpr const char* kCheckboxEmpty = "Ingame/Scoreboard/Icons/checkbox_empty.tga";
constexpr const char* kCheckboxCross = "Ingame/Scoreboard/Icons/checkbox_cross.tga";
constexpr const char* kCheckboxAsk = "Ingame/Scoreboard/Icons/checkbox_ask.tga";
constexpr float kInviteNameX = 7.0f;  // +0xe0
constexpr int kSquadSize = 6;         // 0x791740, 0x791940: "6 − members"

// The textures the constructor loads (0x791260): +0x14 button_upp, +0x18
// button_down, +0x1c empty, +0x2c button_longlight7, +0x30 iconframe, +0x34
// lockicon, +0x44 kit_Commander, +0x48 kit_Squad.
constexpr const char* kArrowUp = "Ingame/Respawn/button_upp.tga";
constexpr const char* kArrowDown = "Ingame/Respawn/button_down.tga";
constexpr const char* kEmptyIcon = "Ingame/GeneralIcons/empty.tga";
constexpr const char* kButtonPlate = "Ingame/Respawn/button_longlight7.tga";
constexpr const char* kIconFrame = "Ingame/Respawn/iconframe.tga";
constexpr const char* kLockIcon = "Ingame/Respawn/lockicon.tga";
constexpr const char* kCommanderIcon = "Ingame/Kits/Icons/kit_Commander.tga";
constexpr const char* kSquadIcon = "Ingame/Kits/Icons/kit_Squad.tga";

// The constructor's numbers (0x791260): the columns' x (+0xb8..+0xe4), the
// rows' icon and text offsets (+0xb0 2.0, +0xb4 0.0), the row background (+0xa8
// 0, +0xac 19.0), 21 rows (+0x84).
constexpr float kFirstIconX = 5.0f;  // +0xb8
constexpr float kIconY = 2.0f;       // +0xb0
constexpr float kTextY = 0.0f;       // +0xb4
constexpr float kButtonY = kIconY - 1.0f;  // +0xb0 − [0x87fe78] (1.0)
constexpr float kBackgroundHeight = 19.0f;
constexpr int kRows = 21;

// The row kinds in the hidden column 14 (0x78f820, 0x78f8b0, 0x791ce0).
constexpr int kCommanderRow = 0;
constexpr int kSquadRow = 2;
constexpr int kCreateRow = 3;
constexpr int kPlayerRow = 4;

// The default squad names (0xa1e380, init 0x8673e0): 0 Unassigned, then
// the alphabet's words, G as Gamma.
constexpr const char* kSquadNames[] = {
    "Unassigned", "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Gamma", "Hotel",
    "India", "Juliet", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo",
    "Sierra", "Tango", "Uniform", "Victor", "Whiskey", "Xray", "Yankee", "Zulu"};

std::string number(int value) { return std::to_string(value); }  // "%i" (0x89e620)

}  // namespace

SquadList::SquadList() {
  // `SquadMemberList`'s columns (init 0x78dcb0); column 0 comes from the
  // ListBoxData constructor.
  members_.addColumn(5.0f, false);    // 1: +0xbc
  members_.addColumn(22.0f, true);    // 2: +0xc0
  members_.addColumn(22.0f, true);    // 3: +0xc0
  members_.addWidthColumn(45.0f, 166.0f);  // 4: +0xc4, 0xa6
  members_.addWidthColumn(45.0f, 69.0f);   // 5: +0xc4, 0x45
  members_.addWidthColumn(85.0f, 30.0f);   // 6: +0xc8, 0x1e
  members_.addColumn(116.0f, true);   // 7: +0xd0
  members_.addColumn(116.0f, true);   // 8: +0xd0
  members_.addColumn(133.0f, true);   // 9: +0xd4
  members_.addWidthColumn(133.0f, 80.0f);  // 10: +0xd4, 0x50
  members_.addColumn(216.0f, true);   // 11: +0xdc
  members_.addColumn(216.0f, true);   // 12: +0xdc
  members_.addHiddenColumn();         // 13: the id
  members_.addHiddenColumn();         // 14: the row kind
  members_.visibleRows = kRows;
  members_.reserveScrollbar = true;   // +0x21 = 1 (+0x23 = 0: purpose not established)
  // `SquadInviteList`: column 1 at +0xe4 (181), then a hidden one.
  invites_.addColumn(181.0f, false);
  invites_.addHiddenColumn();
  invites_.visibleRows = kRows;
}

std::string SquadList::label(const std::string& key) const { return text ? text(key) : key; }

bool SquadList::expanded(int squad) const {
  const int bit = std::clamp(squad, 0, 9);
  return (expanded_ >> bit) & 1u;
}

void SquadList::setExpanded(int squad, bool on) {
  const unsigned bit = 1u << std::clamp(squad, 0, 9);
  expanded_ = on ? expanded_ | bit : expanded_ & ~bit;
}

bool SquadList::update(const SquadWorld& world, bool visible) {
  // 0x792100: only while shown, and only with a local player.
  if (visible && world.available) {
    world_ = world;
    // Coming into view opens our own squad (0x792175..0x7921ab) and re-posts the
    // row count (0x78e2c0).
    if (visible != shown_) setExpanded(world_.squad, true);
    // Joining or creating a squad opens it: HUD event 0x16 (a player joined)
    // for the local player, and 0x18 (new squad leader) for him, each call
    // 0x78fa60(squad of the player, 1) when the squad menu exists (BF2.exe
    // 0x75b2a0 and 0x75b36a, in 0x759e50). The event itself is not routed to
    // the menu here, so the change of our squad number stands for it.
    if (world_.squad > 0 && world_.squad != lastSquad_) setExpanded(world_.squad, true);
    lastSquad_ = world_.squad;
    // NrOfSquads and NrOfUnassigned (0x78e330): the lexicon's strings with their
    // markers replaced. `#TOTAL#` of the squads is `[0xa08f60]->vtbl[0x94]`,
    // not read; 9, the squads the fill walks, stands in.
    int active = 0;
    for (int s = 1; s <= 9; ++s) active += !world_.squads[s].members.empty();
    const auto replace = [](std::string text, const std::string& marker, const std::string& with) {
      if (const auto at = text.find(marker); at != std::string::npos) text.replace(at, marker.size(), with);
      return text;
    };
    squadsText_ = replace(replace(label("HUD_TEXT_MENU_SPAWN_SQUAD_NUMBER_OF_SQUADS"), "#ACTIVE#",
                                  number(active)),
                          "#TOTAL#", number(9));
    unassignedText_ = replace(replace(label("HUD_TEXT_MENU_SPAWN_SQUAD_NUMBER_OF_UNASSIGNED"),
                                      "#UNASSIGNED#", number(world_.unassigned)),
                              "#TOTAL#", number(world_.teamPlayers));
    fill();
    // 0x78f1b0(0): the create panel's tag is the first free squad's number.
    int free = -1;
    for (int s = 1; s <= 9 && free < 0; ++s)
      if (world_.squads[s].members.empty()) free = s;
    createTag_ = number(free);
  }
  shown_ = visible;
  return true;
}

void SquadList::fill() {
  // 0x791b20: clear, then the commander's row, the create row while we are in no
  // squad and no commander, then the squads 1..9 that have members.
  members_.clear();
  row_ = 0;
  if (showCommander_) commanderRow();
  if (world_.squad < 1 && !world_.commander) createRow();
  for (int squad = 1; squad <= 9; ++squad) {
    const auto& info = world_.squads[squad];
    if (info.members.empty()) continue;  // 0x791110: `list->size != 0`
    squadRow(squad, static_cast<int>(info.members.size()));
    if (expanded(squad))
      for (const auto& member : info.members) memberRow(member);
  }
  if (inviteList_) inviteFill();
  ++revision_;
}

namespace {
bool contains(const std::vector<int>& list, int value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}
void toggle(std::vector<int>& list, int value) {
  if (const auto at = std::find(list.begin(), list.end(), value); at != list.end()) list.erase(at);
  else list.push_back(value);
}
}  // namespace

const SquadWorld::TeamPlayer* SquadList::teamPlayer(int index) const {
  for (const auto& player : world_.teamPlayerList)
    if (player.index == index) return &player;
  return nullptr;
}

void SquadList::inviteFill() {
  // 0x791b20 step 5: the invite list's own rows.
  invites_.clear();
  row_ = 0;
  // 0x791740: the rows for those picked to invite and not marked to kick
  // (+0xf4), then the squad's empty places, 6 − members − picked + kicked.
  std::vector<const SquadWorld::TeamPlayer*> pending;
  for (int index : picked_)
    if (!contains(kicked_, index))
      if (const auto* player = teamPlayer(index)) pending.push_back(player);
  const int members =
      world_.squad >= 1 && world_.squad <= 9 ? static_cast<int>(world_.squads[world_.squad].members.size()) : 0;
  const int empty = kSquadSize - members - static_cast<int>(picked_.size()) + static_cast<int>(kicked_.size());
  for (int i = 0; i < empty; ++i) pending.push_back(nullptr);

  // 0x791090: a null place, or a player who is not the commander, not us and
  // (when `squad` is 0 or more) not in that squad.
  const auto rows = [&](const std::vector<const SquadWorld::TeamPlayer*>& players, int squad) {
    for (const auto* player : players) {
      if (player != nullptr &&
          (player->commander || player->index == world_.localIndex || (squad >= 0 && player->squad == squad)))
        continue;
      inviteRow(player, squad);
    }
  };
  inviteHeader("HUD_SQUADTAB_SQUAD", "HUD_SQUADTAB_KICK");
  std::vector<const SquadWorld::TeamPlayer*> squadPlayers, teamPlayers;
  for (const auto& player : world_.teamPlayerList) {
    if (world_.squad > 0 && player.squad == world_.squad) squadPlayers.push_back(&player);
    teamPlayers.push_back(&player);
  }
  rows(squadPlayers, -1);  // `getPlayersInSquadSortedBy…(team, squad)`
  rows(pending, -1);
  inviteHeader("HUD_SQUADTAB_TEAM", "HUD_SQUADTAB_INVITE");
  rows(teamPlayers, world_.squad);  // `getPlayersSortedBy…(team)`
}

void SquadList::inviteHeader(const std::string& left, const std::string& right) {
  // 0x78e520: the two labels in 0x97de20, the first at +0xe0 alignment 0, the
  // second alignment 2; "-1" as the index; the header background.
  invites_.addText(0, label(left), kInviteNameX, kTextY, kText, 0);
  invites_.addText(1, label(right), 0.0f, kTextY, kText, 2);
  invites_.addNamed(2, "-1");
  invites_.setRowBackground(row_, kHeaderRow, 0.0f, 0.0f, kBackgroundHeight);
  ++row_;
}

void SquadList::inviteRow(const SquadWorld::TeamPlayer* player, int squad) {
  // 0x790a90.
  if (player == nullptr) {
    invites_.addEmpty(0);
    invites_.addEmpty(1);
    invites_.addNamed(2, "-1");
  } else {
    const bool ours = world_.squad > 0 && player->squad == world_.squad;
    const bool picked = contains(picked_, player->index);
    const bool kicked = contains(kicked_, player->index);
    // The checkbox: blank for one outside our squad among the squad's rows;
    // a cross for one of ours marked to kick; "ask" for one picked (or already
    // invited) and not marked; an empty box otherwise.
    const char* box = kCheckboxEmpty;  // +0x20
    if (squad < 0 && !ours) {
      box = kEmptyIcon;  // +0x1c
    } else if (kicked && ours) {
      box = kCheckboxCross;  // +0x24
    } else if ((picked || contains(world_.invited, player->index)) && !kicked) {
      box = kCheckboxAsk;  // +0x28
    }
    Color name = kOutside;
    if (squad >= 0 || ours) {
      if (!player->alive) name = kDead;  // 0xa1cf78 (`hud->vtbl[0x34c]` not read)
      else if (player->index == selectedPlayer_) name = kPicked;
      else if (ours) name = kSquadName;  // 0xa1cc98
      else name = kFriendly;             // 0xa1c7d8
    }
    invites_.addText(0, "\xa7" "1" + player->name, kInviteNameX, kTextY, name, 0);
    invites_.addTexture(1, box, 15.0f, 15.0f, 0.0f, kIconY, kWhite);
    invites_.addNamed(2, number(player->index));
  }
  if (row_ % 2 != 0) invites_.setRowBackground(row_, kOddRow, 0.0f, 0.0f, kBackgroundHeight);
  ++row_;
}

bool SquadList::singleClickInvite() {
  // 0x791f50: column 1 only; the row's index from column 2 (0x791940).
  if (invites_.selectedColumn != 1) return true;
  const int index = std::atoi(invites_.selectedText(2).c_str());
  const auto* player = teamPlayer(index);
  if (player == nullptr) return true;
  const bool ours = world_.squad > 0 && player->squad == world_.squad;
  const bool invited = contains(world_.invited, index);
  bool ok = true;
  if (!ours && !invited) {
    if (contains(picked_, index)) {
      toggle(picked_, index);  // 0x78fbc0
    } else {
      const int members = static_cast<int>(world_.squads[world_.squad].members.size());
      if (static_cast<int>(picked_.size()) - static_cast<int>(kicked_.size()) >= kSquadSize - members) ok = false;
      else picked_.push_back(index);
    }
  } else {
    toggle(kicked_, index);  // 0x78fb50
  }
  fill();
  return ok;
}

std::vector<SquadList::Action> SquadList::applyInviteList() {
  // 0x791fd0: the marked first — ours are kicked, the rest withdrawn — then the
  // picked who are not marked are invited; then HUD state 0x11 for a leader.
  std::vector<Action> out;
  for (int index : kicked_) {
    const auto* player = teamPlayer(index);
    if (player == nullptr) continue;
    const bool ours = world_.squad > 0 && player->squad == world_.squad;
    out.push_back(Action{ours ? Action::Kind::Kick : Action::Kind::Uninvite, world_.squad, {}, false, index});
  }
  for (int index : picked_)
    if (!contains(kicked_, index)) out.push_back(Action{Action::Kind::Invite, world_.squad, {}, false, index});
  if (world_.leader) requestedState_ = 0x11;
  return out;
}

void SquadList::commanderRow() {
  // 0x78e6a0. The label: APPLY with no commander, MUTINY when one can mutiny
  // (0x597a40, not read: never offered here), RESIGN for the commander himself.
  Color color = kText;
  std::string button;
  std::string arrow = commanderExpanded_ ? kArrowUp : kArrowDown;  // +0x14 / +0x18
  if (!world_.commander) {
    if (world_.commanderIndex < 0) {
      arrow = kEmptyIcon;  // +0x1c
      button = label("HUD_TEXT_MENU_SPAWN_SQUAD_APPLY");
    }
  } else {
    button = label("HUD_TEXT_COMMANDER_RESIGN");
    color = kOwn;
  }
  if (selectedPlayer_ >= 0 && world_.commanderIndex == selectedPlayer_) color = kPicked;
  members_.addTexture(0, kIconFrame, 15.0f, 15.0f, kFirstIconX, kIconY, color);
  members_.addTexture(1, kCommanderIcon, 15.0f, 15.0f, 0.0f, kIconY, color);
  members_.addEmpty(2);
  members_.addEmpty(3);
  members_.addEmpty(4);
  // Plain, as the create row's label: 0x78e6a0 never uses the "§1%s§0" format
  // (0x932030), so Retail draws COMMANDER in the row's own font
  // (retail-squads-fushe-8, 22 Nov).
  members_.addText(5, label("HUD_HUD_MENU_COMMANDER_HEADING"), -21.0f, kTextY, color, 0);
  members_.addEmpty(6);
  members_.addEmpty(7);
  members_.addEmpty(8);
  if (!button.empty()) {
    members_.addTexture(9, kButtonPlate, 80.0f, 17.0f, 0.0f, kButtonY, kWhite);
    members_.addText(10, button, 0.0f, kTextY, kButton, 1);
  } else {
    members_.addEmpty(9);
    members_.addEmpty(10);
  }
  members_.addEmpty(11);
  members_.addTexture(12, arrow, 17.0f, 17.0f, 0.0f, kButtonY, kWhite);
  members_.addNamed(13, number(world_.commanderIndex));
  members_.addNamed(14, number(kCommanderRow));
  members_.setRowBackground(row_, kHeaderRow, 0.0f, 0.0f, kBackgroundHeight);
  ++row_;
  if (commanderExpanded_ && world_.commanderIndex >= 0) {
    // 0x791030: the commander himself under his row.
    SquadWorld::Member commander;
    commander.index = world_.commanderIndex;
    commander.name = world_.commanderName;
    commander.alive = world_.commanderAlive;
    memberRow(commander);
  }
}

void SquadList::createRow() {
  // 0x78eca0: the row that offers a new squad.
  members_.addTexture(0, kIconFrame, 15.0f, 15.0f, kFirstIconX, kIconY, kText);
  members_.addTexture(1, kSquadIcon, 15.0f, 15.0f, 0.0f, kIconY, kText);
  members_.addEmpty(2);
  members_.addEmpty(3);
  members_.addEmpty(4);
  members_.addText(5, label("HUD_SQUADTAB_SQUAD"), -21.0f, kTextY, kText, 0);
  members_.addEmpty(6);
  members_.addEmpty(7);
  members_.addEmpty(8);
  members_.addTexture(9, kButtonPlate, 80.0f, 17.0f, 0.0f, kButtonY, kWhite);
  members_.addText(10, label("HUD_SQUADTAB_CREATE"), 0.0f, kTextY, kButton, 1);
  members_.addEmpty(11);
  members_.addEmpty(12);
  members_.addEmpty(13);
  members_.addNamed(14, number(kCreateRow));
  members_.setRowBackground(row_, kHeaderRow, 0.0f, 0.0f, kBackgroundHeight);
  ++row_;
}

void SquadList::squadRow(int squad, int memberCount) {
  // 0x78fc30: the squad's header.
  const auto& info = world_.squads[squad];
  Color color = kText;
  if (squad == selectedSquad_) color = kPicked;
  else if (squad == world_.squad) color = kOwn;
  const Color lockColor = info.locked ? kText : kFrame;  // the squad's vtbl[4]
  // The button: JOIN, or LEAVE on our own squad; dimmed to the frame colour when
  // we are in another squad or command (0x78fd70..0x78fe1a).
  std::string button = label("HUD_TEXT_MENU_SPAWN_SQUAD_JOIN");
  Color buttonColor = kButton;
  if ((world_.squad == squad || world_.squad < 1) && !world_.commander) {
    if (world_.squad == squad) button = label("HUD_TEXT_MENU_SPAWN_SQUAD_LEAVE");
  } else {
    buttonColor = kFrame;
  }
  const char* arrow = expanded(squad) ? kArrowUp : kArrowDown;
  members_.addTexture(0, kIconFrame, 15.0f, 15.0f, kFirstIconX, kIconY, color);
  members_.addText(1, number(squad), 5.0f, kTextY, color, 0);
  members_.addEmpty(2);
  members_.addEmpty(3);
  members_.addEmpty(4);
  // 0x431410 passes the name through 0x4311b0, the HUD's upper case: Retail
  // draws "ALPHA" (retail-squads-fushe-8, 22 Nov).
  members_.addText(5, "\xa7" "1" + upperCase(info.name) + "\xa7" "0", -21.0f, kTextY, color, 0);
  members_.addText(6, "\xa7" "1" + number(memberCount) + "/6" "\xa7" "0", 0.0f, kTextY, color, 2);
  members_.addTexture(7, kIconFrame, 15.0f, 15.0f, 0.0f, kIconY, kFrame);
  members_.addTexture(8, kLockIcon, 15.0f, 15.0f, 0.0f, kIconY, lockColor);
  members_.addTexture(9, kButtonPlate, 80.0f, 17.0f, 0.0f, kButtonY, kWhite);
  members_.addText(10, button, 0.0f, kTextY, buttonColor, 1);
  members_.addEmpty(11);
  members_.addTexture(12, arrow, 17.0f, 17.0f, 0.0f, kButtonY, kWhite);
  members_.addNamed(13, number(squad));
  members_.addNamed(14, number(kSquadRow));
  members_.setRowBackground(row_, kHeaderRow, 0.0f, 0.0f, kBackgroundHeight);
  ++row_;
}

void SquadList::memberRow(const SquadWorld::Member& member) {
  // 0x7902c0: one player.
  Color name = kDead;  // dead, or `hud->vtbl[0x34c]` (not read)
  if (member.alive) {
    if (member.index == selectedPlayer_) name = kPicked;
    else if (world_.squad >= 1 && world_.squads[world_.squad].members.end() !=
                                      std::find_if(world_.squads[world_.squad].members.begin(),
                                                   world_.squads[world_.squad].members.end(),
                                                   [&](const auto& m) { return m.index == member.index; }))
      name = kSquadName;
    else if (member.index == world_.localIndex && world_.commander) name = kSquadName;
    else name = kFriendly;
  }
  const std::string kit = member.alive ? member.kitIcon : "Ingame/Scoreboard/Icons/icon_Death.tga";
  // The leader's squad mark (`mini_Squad_%d_small.tga`) in column 9, else the
  // empty icon (+0x1c).
  int squad = 0;
  for (int s = 1; s <= 9; ++s)
    for (const auto& m : world_.squads[s].members)
      if (m.index == member.index) squad = s;
  const std::string mark = member.squadLeader && squad > 0
                               ? "Ingame/Player/Icons/Minimap/mini_Squad_" + number(squad) + "_small.tga"
                               : kEmptyIcon;
  members_.addTexture(0, kIconFrame, 15.0f, 15.0f, kFirstIconX, kIconY, kFrame);
  members_.addTexture(1, member.kitIcon.empty() ? kEmptyIcon : member.kitIcon, 15.0f, 15.0f, 0.0f,
                      kIconY, kKitIcon);
  members_.addTexture(2, kIconFrame, 15.0f, 15.0f, 0.0f, kIconY, kFrame);
  members_.addTexture(3, kit, 15.0f, 15.0f, 0.0f, kIconY, kKitIcon);
  members_.addText(4, "\xa7" "1" + member.name, -4.0f, kTextY, name, 0);
  members_.addEmpty(5);
  members_.addEmpty(6);
  members_.addEmpty(7);
  members_.addEmpty(8);
  // x = (+0xdc − +0xd4) − [0x932078] = 216 − 133 − 18.
  members_.addTexture(9, mark, 16.0f, 16.0f, 216.0f - 133.0f - 18.0f, kButtonY, kOwn);
  members_.addEmpty(10);
  members_.addTexture(11, kIconFrame, 15.0f, 15.0f, 0.0f, kIconY, kFrame);
  // Column 12 is the voice icon when the player has voice (`vtbl[0x268]`); our
  // voice state is not wired to the menu, so the cell stays empty (0x79093e).
  members_.addEmpty(12);
  members_.addNamed(13, number(member.index));
  members_.addNamed(14, number(kPlayerRow));
  if (member.index == world_.localIndex) {
    members_.setRowBackground(row_, kLocalRow, 0.0f, 0.0f, kBackgroundHeight);  // 0x7c4af0
  } else if (row_ % 2 != 0) {
    members_.setRowBackground(row_, kOddRow, 0.0f, 0.0f, kBackgroundHeight);
  }
  ++row_;
}

std::optional<SquadList::Action> SquadList::singleClickNew() {
  // 0x791ce0: the row's kind (column 14) and the picked column (+0xc).
  const int kind = std::atoi(members_.selectedText(14).c_str());
  const int id = std::atoi(members_.selectedText(13).c_str());
  const int column = members_.selectedColumn;
  std::optional<Action> out;
  const auto pickPlayer = [&](int player) {  // 0x78f240
    selectedPlayer_ = player == selectedPlayer_ ? -1 : player;
  };
  switch (kind) {
    case kCommanderRow:
      if ((column >= 0 && column <= 6) || column == 11) pickPlayer(world_.commanderIndex);
      // Column 10 (0x791ce0): resign as the commander (`hud->vtbl[0x2b4]`),
      // apply with no commander (0x2b0, 0x7b1c50). A mutiny (0x2b8) needs
      // 0x597a40, not read, so it is never offered (as the row's label).
      else if (column == 10 && world_.commander) out = Action{Action::Kind::CommanderResign, 0, {}, false};
      else if (column == 10 && world_.commanderIndex < 0) out = Action{Action::Kind::CommanderApply, 0, {}, false};
      else if (column == 12) commanderExpanded_ = !commanderExpanded_;
      break;
    case kSquadRow:
      if ((column >= 0 && column <= 6) || column == 11) {
        selectedSquad_ = id == selectedSquad_ ? -1 : id;  // 0x78f270
      } else if (column == 8) {
        // 0x78f940: our own squad, as its leader, flips the lock.
        const auto& info = world_.squads[std::clamp(id, 0, 9)];
        if (id > 0 && world_.leader && world_.squad == id)
          out = Action{info.locked ? Action::Kind::Unlock : Action::Kind::Lock, id, {}, false};
      } else if (column == 10) {
        // In the invite state as a leader, back to the members (0x791dcf).
        if (inviteList_ && world_.leader) requestedState_ = 0x11;
        if (id == world_.squad) out = Action{Action::Kind::Leave, id, {}, false};  // 0x78f480
        else if (!world_.commander && world_.squad == 0)
          out = Action{Action::Kind::Join, id, {}, false};  // 0x78f2a0
        // In another squad 0x78f2a0 shows HUD_HUD_SQUAD_MUST_LEAVE_TO_JOIN instead.
      } else if (column == 12) {
        setExpanded(id, !expanded(id));  // 0x790ef0 -> 0x78fa60
      }
      break;
    case kCreateRow:
      // Column 10 commits the name box (`hud->vtbl[0x28c]` -> 0x7b15f0).
      if (column == 10) out = createSquad();
      break;
    case kPlayerRow:
      pickPlayer(id);
      break;
    default:
      break;
  }
  fill();
  return out;
}

std::string SquadList::firstFreeName() const {
  // 0x78f0e0: the default name of the first free squad, moved on while one of
  // the team's squads already has it.
  int free = -1;
  for (int s = 1; s <= 9 && free < 0; ++s)
    if (world_.squads[s].members.empty()) free = s;
  if (free < 0) return {};
  int index = free;
  const int count = static_cast<int>(std::size(kSquadNames));
  while (index < count) {
    bool taken = false;
    for (int s = 0; s <= 9; ++s)
      if (!world_.squads[s].members.empty() && world_.squads[s].name == kSquadNames[index]) taken = true;
    if (!taken) break;
    ++index;
  }
  return index < count ? kSquadNames[index] : kSquadNames[count - 1];
}

std::optional<SquadList::Action> SquadList::createSquad() {
  // 0x78f9b0 / 0x7b15f0: not a commander, a free squad, and we are in none
  // (0x7b15f0 leaves the one we are in first, 0x78f480). The name is the box's
  // (`vtbl[0x288]`), re-filled with the default first when the panel was shut
  // (CREATE on the create row); an empty box gives the squad's default.
  std::optional<Action> out;
  int free = -1;
  for (int s = 1; s <= 9 && free < 0; ++s)
    if (world_.squads[s].members.empty()) free = s;
  if (!createShow_) nameBox_ = firstFreeName();
  if (!world_.commander && free > 0) {
    out = Action{Action::Kind::Create, free, nameBox_.empty() ? firstFreeName() : nameBox_, createLocked_};
  }
  setSquadCreateSelect(false);
  return out;
}

void SquadList::setSquadCreateSelect(bool on) {
  // 0x78f5e0: opening needs a free squad and no command; closing always works.
  if (!on) {
    createShow_ = false;
    renameShow_ = false;
    nameFocused_ = false;
    return;
  }
  if (world_.commander) return;  // HUD_HUD_SQUAD_COMMANDER_MUST_RESIGN
  if (world_.squad > 0) return;  // HUD_HUD_SQUAD_MUST_LEAVE_TO_CREATE
  bool free = false;
  for (int s = 1; s <= 9; ++s) free |= world_.squads[s].members.empty();
  if (!free) return;  // HUD_HUD_SQUAD_MAX_REACHED
  createShow_ = true;
  nameBox_ = firstFreeName();  // 0x78f0e0's default into the box (`vtbl[0x284]`)
  createLocked_ = false;  // 0x78f1b0(1)
}

const std::string* SquadList::editString(int string) const {
  // The box's slot: `setEditNodeString 2` in HudElementsSquadsNew.con; the
  // manager's index field (+0xf4) holding 2 is not read.
  return string == 2 ? &nameBox_ : nullptr;
}

void SquadList::squadCreateLockToggle() { createLocked_ = !createLocked_; }  // 0x78f220

bool SquadList::setSquadNameSelect() {
  // 0x78f190: only while the create panel is shown (+0xe8); then 0x7b11e0 asks
  // HUD state 0xd and focuses the edit node (`vtbl[0x268](1)`).
  if (!createShow_) return false;
  nameFocused_ = true;
  requestedState_ = 0xd;
  return true;
}

void SquadList::setNameBox(const std::string& text) {
  // The edit node keeps at most its max length (`setEditNodeMaxLength`,
  // vtbl[0xd4]); what it does past that is not read, so the rest is dropped.
  nameBox_ = text.substr(0, kNameLength);
}

void SquadList::setShowInviteList(bool on) {
  // 0x7918e0: leaders only; HUD state 0x1a for the list, 0x11 back.
  if (!world_.leader) return;
  inviteList_ = on;
  requestedState_ = on ? 0x1a : 0x11;
  if (on) {
    // 0x791690: the picked start as `getInvitedPlayers(team, squad)`. That
    // the two lists are emptied first is assumed: 0x7918e0's body is not read.
    picked_ = world_.invited;
    kicked_.clear();
  }
  fill();
}

std::optional<int> SquadList::takeRequestedState() {
  auto out = requestedState_;
  requestedState_.reset();
  return out;
}

const ListData* SquadList::list(int data) const {
  if (data == 5) return &members_;
  if (data == 8) return &invites_;
  return nullptr;
}

bool SquadList::boolVariable(const std::string& name, bool& out) const {
  if (name == "SquadMemberListShow") out = shown_;
  else if (name == "SquadCreateShow") out = createShow_;
  else if (name == "SquadRenameShow") out = renameShow_;
  else if (name == "SquadCreateLockedShow") out = createLocked_;
  else if (name == "InviteListShow") out = inviteList_;
  else if (name == "IsInSquad") out = world_.squad > 0;  // +0xa2
  else if (name == "IsSquadLeader") out = world_.leader;  // +0x9c
  else return false;
  return true;
}

bool SquadList::stringVariable(const std::string& name, std::string& out) const {
  if (name == "NrOfSquads") out = squadsText_;
  else if (name == "NrOfUnassigned") out = unassignedText_;
  else if (name == "SquadCreateTagString") out = createTag_;
  else if (name == "NewSquadName") out = firstFreeName();
  // 0x78e130's role: `HUD_TEXT_MENU_SPAWN_SQUAD_COMMANDER`, `_SQUADLEADER` or
  // `_SOLDIER`. The original formats it `"%s - %s"` with the team, which is
  // not ported: the role alone stands.
  else if (name == "TeamTopLabelString")
    out = label(world_.commander ? "HUD_TEXT_MENU_SPAWN_SQUAD_COMMANDER"
                : world_.leader  ? "HUD_TEXT_MENU_SPAWN_SQUAD_SQUADLEADER"
                                 : "HUD_TEXT_MENU_SPAWN_SQUAD_SOLDIER");
  else return false;
  return true;
}

}  // namespace obf2::hud
