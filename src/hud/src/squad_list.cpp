#include "obf2/hud/squad_list.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace obf2::hud {
namespace {

// The colours (OpenBF2 Menu/HudSquad.cpp, the 0x97de.. constants; Menu/HudColors.cpp
// for the palettes, read at index 2 as `HudSquad` reads them).
constexpr Color kTan{0.7490196f, 0.7490196f, 0.5607843f, 1.0f};        // 0x97de20, row text
constexpr Color kGold{1.0f, 0.792f, 0.043f, 1.0f};                     // 0x97de30, button captions
constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};                        // 0x97dd50
constexpr Color kBand{0.4862745f, 0.4784314f, 0.3882353f, 1.0f};       // 0x97de10, heading band
constexpr Color kOlive{0.576f, 0.573f, 0.451f, 1.0f};                  // 0x97e2b0, frames, greyed
constexpr Color kIcon{0.3843137f, 0.3607843f, 0.2549020f, 1.0f};       // 0x97e2a0
constexpr Color kOddRow{0.7254902f, 0.7098039f, 0.5803922f, 1.0f};     // 0x97de00
constexpr Color kLocalRow{0.9960784f, 0.7529412f, 0.0823529f, 0.35f};  // the local player's row
constexpr Color kSquadChat{0.4470588f, 0.8901961f, 0.3450980f, 1.0f};  // squadChatColors[2]
constexpr Color kDead{0.2901961f, 0.2705882f, 0.2078431f, 1.0f};       // scoreboardDeadColors[2]
constexpr Color kSquadName{0.0313726f, 0.4666667f, 0.0078431f, 1.0f};  // scoreboardSquadColors[2]
constexpr Color kName{0.0f, 0.3803922f, 1.0f, 1.0f};                   // scoreboardNameColors[2]

// The constructor's numbers (HudSquad::HudSquad): the column x positions, the
// icon and text y, the row band's y and height.
constexpr float kColumns[12] = {5, 5, 22, 45, 85, 196, 116, 133, 216, 216, 7, 181};
constexpr float kIconY = 2.0f;     // +0xb0
constexpr float kTextY = 0.0f;     // +0xb4
constexpr float kBandY = 0.0f;     // +0xa8
constexpr float kBandHeight = 19;  // +0xac
constexpr int kVisibleRows = 21;   // +0x84

// The textures (the constructor's `loadTexture` list).
constexpr const char* kButtonUp = "Ingame/Respawn/button_upp.tga";
constexpr const char* kButtonDown = "Ingame/Respawn/button_down.tga";
constexpr const char* kEmpty = "Ingame/GeneralIcons/empty.tga";
constexpr const char* kButtonBar = "Ingame/Respawn/button_longlight7.tga";
constexpr const char* kFrame = "Ingame/Respawn/iconframe.tga";
constexpr const char* kLock = "Ingame/Respawn/lockicon.tga";
constexpr const char* kKitCommander = "Ingame/Kits/Icons/kit_Commander.tga";
constexpr const char* kKitSquad = "Ingame/Kits/Icons/kit_Squad.tga";

// The 27 default names (`defaultSquadNames`), the engine's own.
constexpr const char* kDefaultNames[27] = {
    "Unassigned", "Alpha",   "Bravo",  "Charlie", "Delta",   "Echo",    "Foxtrot",
    "Gamma",      "Hotel",   "India",  "Juliet",  "Kilo",    "Lima",    "Mike",
    "November",   "Oscar",   "Papa",   "Quebec",  "Romeo",   "Sierra",  "Tango",
    "Uniform",    "Victor",  "Whiskey", "Xray",   "Yankee",  "Zulu"};

// "\xa7" "1%s" "\xa7" "0" and its kin: the font switch codes the list node reads.
std::string wrapped(const std::string& text) { return "\xa7" "1" + text + "\xa7" "0"; }

std::string upper(std::string text) {
  for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return text;
}

std::string replace(std::string text, std::string_view token, const std::string& value) {
  if (const auto at = text.find(token); at != std::string::npos) {
    text.replace(at, token.size(), value);
  }
  return text;
}

}  // namespace

SquadList::SquadList() {
  // `registerData` (0x78db..): after column 0, these, then two hidden ones.
  members_.addColumn(kColumns[1], false);
  members_.addColumn(kColumns[2], true);
  members_.addColumn(kColumns[2], true);
  members_.addIconColumn(kColumns[3], 166);
  members_.addIconColumn(kColumns[3], 69);
  members_.addIconColumn(kColumns[4], 30);
  members_.addColumn(kColumns[6], true);
  members_.addColumn(kColumns[6], true);
  members_.addColumn(kColumns[7], true);
  members_.addIconColumn(kColumns[7], 80);
  members_.addColumn(kColumns[9], true);
  members_.addColumn(kColumns[9], true);
  members_.addHiddenColumn();
  members_.addHiddenColumn();
  members_.visibleRows = kVisibleRows;
  members_.reserveScrollbar = true;  // +0x21 (`m_field021 = true`)
}

int SquadList::membersOf(int squad) const {
  int count = 0;
  for (const SquadPlayer& player : world_.players) {
    if (player.team == team_ && player.squad == squad) ++count;
  }
  return count;
}

int SquadList::findEmptySquad() const {
  for (int squad = 1; squad < 10; ++squad) {
    if (membersOf(squad) == 0) return squad;
  }
  return -1;
}

std::string SquadList::squadName(int squad) const {
  std::string name = world_.name ? world_.name(team_, squad) : std::string();
  if (name.empty() && squad >= 0 && squad < 27) name = kDefaultNames[squad];
  return name;
}

std::string SquadList::defaultNameFor(int squad) const {
  // `getAvailableSquadName`: the default name of that index, unless a squad that
  // has players already carries it — then the next one.
  for (int index = std::max(0, squad); index < 27; ++index) {
    bool taken = false;
    for (int other = 0; other < 10 && !taken; ++other) {
      taken = membersOf(other) > 0 && squadName(other) == kDefaultNames[index];
    }
    if (!taken) return kDefaultNames[index];
  }
  return kDefaultNames[0];
}

void SquadList::update(const SquadWorld& world, const Text& text) {
  world_ = world;
  const auto self = std::find_if(world_.players.begin(), world_.players.end(),
                                 [&](const SquadPlayer& p) { return p.id == world_.localId; });
  if (self == world_.players.end()) {
    members_.clear();
    rowKeys_.clear();
    return;
  }
  // `setVisible(true)`.
  if (self->squad != squadId_) expanded_[static_cast<std::size_t>(std::clamp(self->squad, 0, 9))] = true;
  squadId_ = self->squad;
  squadLeader_ = self->leader;
  commander_ = self->commander;
  team_ = self->team;
  playerId_ = self->id;

  // `refreshTitle`: "<team> - <role>". The team's name is not ours to know here,
  // so the role stands alone; the kit's role (a soldier's default vehicle) is not
  // taken either.
  std::string role = text("HUD_TEXT_MENU_SPAWN_SQUAD_SOLDIER");
  if (commander_) {
    role = text("HUD_TEXT_MENU_SPAWN_SQUAD_COMMANDER");
  } else if (squadLeader_) {
    role = text("HUD_TEXT_MENU_SPAWN_SQUAD_SQUADLEADER");
  }
  title_ = role;

  // `refreshCounts`: the totals over the team; "#TOTAL#" of the squads is the
  // constant 9, the squad slots.
  int total = 0;
  int inSquads = 0;
  int active = 0;
  for (const SquadPlayer& player : world_.players) {
    if (player.team != team_) continue;
    ++total;
    if (player.squad > 0) ++inSquads;
  }
  for (int squad = 1; squad < 10; ++squad) active += membersOf(squad) > 0 ? 1 : 0;
  unassignedText_ = replace(replace(text("HUD_TEXT_MENU_SPAWN_SQUAD_NUMBER_OF_UNASSIGNED"),
                                    "#UNASSIGNED#", std::to_string(total - inSquads)),
                            "#TOTAL#", std::to_string(total));
  squadCountText_ = replace(replace(text("HUD_TEXT_MENU_SPAWN_SQUAD_NUMBER_OF_SQUADS"), "#ACTIVE#",
                                    std::to_string(active)),
                            "#TOTAL#", "9");

  // `refreshRows`.
  members_.clear();
  rowKeys_.clear();
  rowCount_ = 0;
  paintCommanderHeading(text);  // `m_showCommander` is true from the constructor
  if (commanderExpanded_) {
    for (const SquadPlayer& player : world_.players) {
      if (player.team == team_ && player.commander) paintPlayerRow(player);
    }
  }
  if (squadId_ <= 0 && !commander_) paintCreateRow(text);
  for (int squad = 1; squad < 10; ++squad) {
    std::vector<const SquadPlayer*> in;
    for (const SquadPlayer& player : world_.players) {
      if (player.team == team_ && player.squad == squad) in.push_back(&player);
    }
    if (in.empty()) continue;
    // `fillSquadRows`: the row, then while expanded the leader and the others.
    paintSquadRow(squad, static_cast<int>(in.size()), text);
    if (!expanded_[static_cast<std::size_t>(squad)]) continue;
    for (const SquadPlayer* player : in) {
      if (player->leader) paintPlayerRow(*player);
    }
    for (const SquadPlayer* player : in) {
      if (!player->leader) paintPlayerRow(*player);
    }
  }

  // `refreshCreateTag`: the number the next squad would take.
  createTag_ = std::to_string(findEmptySquad());
}

void SquadList::paintCommanderHeading(const Text& text) {
  int commander = -1;
  for (const SquadPlayer& player : world_.players) {
    if (player.team == team_ && player.commander) commander = player.id;
  }
  Color color = kTan;
  std::string button = text("HUD_TEXT_MENU_SPAWN_SQUAD_APPLY");
  std::string arrow = commanderExpanded_ ? kButtonUp : kButtonDown;
  if (commander_) {
    button = text("HUD_TEXT_COMMANDER_RESIGN");
    color = kSquadChat;
  } else if (commander >= 0) {
    // The mutiny button stands while a vote may be issued; we hold no votes.
    button.clear();
  } else {
    arrow = kEmpty;
  }
  if (selectedPlayer_ >= 0 && commander == selectedPlayer_) color = kWhite;
  members_.addTexture(0, kFrame, 15, 15, kColumns[0], kIconY, color);
  members_.addTexture(1, kKitCommander, 15, 15, 0, kIconY, color);
  members_.addText(2, "", 0, 0, kWhite, 0);
  members_.addText(3, "", 0, 0, kWhite, 0);
  members_.addText(4, "", 0, 0, kWhite, 0);
  members_.addText(5, text("HUD_HUD_MENU_COMMANDER_HEADING"), -21, kTextY, color, 0);
  members_.addText(6, "", 0, 0, kWhite, 0);
  members_.addText(7, "", 0, 0, kWhite, 0);
  members_.addText(8, "", 0, 0, kWhite, 0);
  if (button.empty()) {
    members_.addText(9, "", 0, 0, kWhite, 0);
    members_.addText(10, "", 0, 0, kWhite, 0);
  } else {
    members_.addTexture(9, kButtonBar, 80, 17, 0, kIconY - 1.0f, kWhite);
    members_.addText(10, button, 0, kTextY, kGold, 1);
  }
  members_.addText(11, "", 0, 0, kWhite, 0);
  members_.addTexture(12, arrow, 17, 17, 0, kIconY - 1.0f, kWhite);
  members_.addText(13, std::to_string(commander), 0, 0, kWhite, 0);
  members_.addText(14, "0", 0, 0, kWhite, 0);
  members_.setRowBackground(rowCount_, kBand, 0, kBandY, kBandHeight);
  rowKeys_.push_back({commander, 0});
  ++rowCount_;
}

void SquadList::paintCreateRow(const Text& text) {
  members_.addTexture(0, kFrame, 15, 15, kColumns[0], kIconY, kTan);
  members_.addTexture(1, kKitSquad, 15, 15, 0, kIconY, kTan);
  members_.addText(2, "", 0, 0, kWhite, 0);
  members_.addText(3, "", 0, 0, kWhite, 0);
  members_.addText(4, "", 0, 0, kWhite, 0);
  members_.addText(5, text("HUD_SQUADTAB_SQUAD"), -21, kTextY, kTan, 0);
  members_.addText(6, "", 0, 0, kWhite, 0);
  members_.addText(7, "", 0, 0, kWhite, 0);
  members_.addText(8, "", 0, 0, kWhite, 0);
  members_.addTexture(9, kButtonBar, 80, 17, 0, kIconY - 1.0f, kWhite);
  members_.addText(10, text("HUD_SQUADTAB_CREATE"), 0, kTextY, kGold, 1);
  members_.addText(11, "", 0, 0, kWhite, 0);
  members_.addText(12, "", 0, 0, kWhite, 0);
  members_.addText(13, "", 0, 0, kWhite, 0);
  members_.addText(14, "3", 0, 0, kWhite, 0);
  members_.setRowBackground(rowCount_, kBand, 0, kBandY, kBandHeight);
  rowKeys_.push_back({-1, 3});
  ++rowCount_;
}

void SquadList::paintSquadRow(int squad, int memberCount, const Text& text) {
  Color color = kTan;
  if (squad == selectedSquad_) {
    color = kWhite;
  } else if (squad == squadId_) {
    color = kSquadChat;
  }
  const bool locked = world_.locked && world_.locked(team_, squad);
  std::string button = text("HUD_TEXT_MENU_SPAWN_SQUAD_JOIN");
  Color buttonColor = kGold;
  if ((squadId_ != squad && squadId_ > 0) || commander_) {
    buttonColor = kOlive;
  } else if (squadId_ == squad) {
    button = text("HUD_TEXT_MENU_SPAWN_SQUAD_LEAVE");
  }
  const bool open = expanded_[static_cast<std::size_t>(std::clamp(squad, 0, 9))];
  members_.addTexture(0, kFrame, 15, 15, kColumns[0], kIconY, color);
  members_.addText(1, std::to_string(squad), 5, kTextY, color, 0);
  members_.addText(2, "", 0, 0, kWhite, 0);
  members_.addText(3, "", 0, 0, kWhite, 0);
  members_.addText(4, "", 0, 0, kWhite, 0);
  members_.addText(5, wrapped(upper(squadName(squad))), -21, kTextY, color, 0);
  members_.addText(6, wrapped(std::to_string(memberCount) + "/6"), 0, kTextY, color, 2);
  members_.addTexture(7, kFrame, 15, 15, 0, kIconY, kOlive);
  members_.addTexture(8, kLock, 15, 15, 0, kIconY, locked ? kTan : kOlive);
  members_.addTexture(9, kButtonBar, 80, 17, 0, kIconY - 1.0f, kWhite);
  members_.addText(10, button, 0, kTextY, buttonColor, 1);
  members_.addText(11, "", 0, 0, kWhite, 0);
  members_.addTexture(12, open ? kButtonUp : kButtonDown, 17, 17, 0, kIconY - 1.0f, kWhite);
  members_.addText(13, std::to_string(squad), 0, 0, kWhite, 0);
  members_.addText(14, "2", 0, 0, kWhite, 0);
  members_.setRowBackground(rowCount_, kBand, 0, kBandY, kBandHeight);
  rowKeys_.push_back({squad, 2});
  ++rowCount_;
}

void SquadList::paintPlayerRow(const SquadPlayer& player) {
  Color color = kName;
  if (!player.alive) {
    color = kDead;
  } else if (player.id == selectedPlayer_) {
    color = kWhite;
  } else if ((player.squad == squadId_ && squadId_ > 0) ||
             (player.id == playerId_ && commander_)) {
    color = kSquadName;
  }
  // The squad leader's own mark, `mini_Squad_<n>_small.tga`.
  const std::string leader =
      player.leader ? "Ingame/Player/Icons/Minimap/mini_Squad_" + std::to_string(player.squad) +
                          "_small.tga"
                    : kEmpty;
  members_.addTexture(0, kFrame, 15, 15, kColumns[0], kIconY, kOlive);
  members_.addTexture(1, kEmpty, 15, 15, 0, kIconY, kIcon);  // the vehicle icon: not taken
  members_.addTexture(2, kFrame, 15, 15, 0, kIconY, kOlive);
  members_.addTexture(3, kEmpty, 15, 15, 0, kIconY, kIcon);  // the kit icon: not taken
  members_.addText(4, "\xa7" "1" + player.name, -4, kTextY, color, 0);
  members_.addText(5, "", 0, 0, kWhite, 0);
  members_.addText(6, "", 0, 0, kWhite, 0);
  members_.addText(7, "", 0, 0, kWhite, 0);
  members_.addText(8, "", 0, 0, kWhite, 0);
  members_.addTexture(9, leader, 16, 16, (kColumns[9] - kColumns[7]) - 18.0f, kIconY - 1.0f,
                      kSquadChat);
  members_.addText(10, "", 0, 0, kWhite, 0);
  members_.addTexture(11, kFrame, 15, 15, 0, kIconY, kOlive);
  members_.addText(12, "", 0, 0, kWhite, 0);  // the voice icon: no voice
  members_.addText(13, std::to_string(player.id), 0, 0, kWhite, 0);
  members_.addText(14, "4", 0, 0, kWhite, 0);
  if (player.id == playerId_) {
    members_.setRowBackground(rowCount_, kLocalRow, 0, kBandY, kBandHeight);
  } else if (rowCount_ % 2 > 0) {
    members_.setRowBackground(rowCount_, kOddRow, 0, kBandY, kBandHeight);
  }
  rowKeys_.push_back({player.id, 4});
  ++rowCount_;
}

SquadList::Action SquadList::click(int row, int column) {
  Action action;
  if (row < 0 || static_cast<std::size_t>(row) >= rowKeys_.size()) return action;
  const auto [key, type] = rowKeys_[static_cast<std::size_t>(row)];
  switch (type) {
    case 0:
      if (column == 12) {
        commanderExpanded_ = !commanderExpanded_;
      } else if (column == 10) {
        action.kind = Action::Kind::NotDone;
        action.what = commander_ ? "commander resign" : "commander apply or mutiny";
      } else if (column != 7 && column != 8 && column != 9) {
        selectedPlayer_ = key == selectedPlayer_ ? -1 : key;
      }
      return action;
    case 2:
      if (column == 8) {
        // `toggleSquadPrivate`: only the leader of that squad.
        if (key > 0 && squadLeader_ && squadId_ == key) {
          action.kind = Action::Kind::SetPrivate;
          action.squad = key;
          action.flag = !(world_.locked && world_.locked(team_, key));
        }
      } else if (column == 10) {
        if (key == squadId_) {
          // `leaveAndReinvite`: leave, keeping no invite.
          action.kind = Action::Kind::Leave;
          action.squad = key;
        } else if (!commander_ && squadId_ <= 0 && key > -1 && membersOf(key) > 0) {
          // `joinSquad`: only from no squad; from another one the original says
          // "leave first" in the chat, which we do not have.
          action.kind = Action::Kind::Join;
          action.squad = key;
        } else if (squadId_ > 0) {
          action.kind = Action::Kind::NotDone;
          action.what = "the chat line HUD_HUD_SQUAD_MUST_LEAVE_TO_JOIN";
        }
      } else if (column == 12) {
        expanded_[static_cast<std::size_t>(std::clamp(key, 0, 9))] =
            !expanded_[static_cast<std::size_t>(std::clamp(key, 0, 9))];
      } else if (column != 7 && column != 9) {
        selectedSquad_ = key == selectedSquad_ ? -1 : key;
      }
      return action;
    case 3:
      if (column == 10) return createSquad();
      return action;
    case 4:
      selectedPlayer_ = key == selectedPlayer_ ? -1 : key;
      return action;
    default:
      return action;
  }
}

SquadList::Action SquadList::setCreateMode(bool enabled) {
  Action action;
  if ((squadId_ == 0 && !commander_) || !enabled) {
    int squad = findEmptySquad();
    if (squadId_ > 0 && membersOf(squadId_) == 1) squad = squadId_;
    if (squad > 0) {
      createVisible_ = enabled;
      if (enabled) createLocked_ = false;  // `refreshCreateTag(true)`
      return action;
    }
    createVisible_ = false;
    action.kind = Action::Kind::NotDone;
    action.what = "the chat line HUD_HUD_SQUAD_MAX_REACHED";
    return action;
  }
  action.kind = Action::Kind::NotDone;
  action.what = commander_ ? "the chat line HUD_HUD_SQUAD_COMMANDER_MUST_RESIGN"
                           : "the chat line HUD_HUD_SQUAD_MUST_LEAVE_TO_CREATE";
  return action;
}

SquadList::Action SquadList::createSquad() {
  // `finishSquadCreate` → `BF2HudManager::createSquad`: the name of an empty
  // squad is set, and that creates it. The edit box is not ours yet, so the name
  // is the default one the panel would have offered.
  Action action;
  createVisible_ = false;
  if (commander_) return action;
  int squad = findEmptySquad();
  if (squadId_ != 0 && membersOf(squadId_) == 1) squad = squadId_;
  if (squad <= 0) return action;
  action.kind = Action::Kind::Create;
  action.squad = squad;
  action.name = defaultNameFor(squad);
  action.flag = createLocked_;
  return action;
}

}  // namespace obf2::hud
