#include "obf2/hud/scoreboard.h"

#include <algorithm>
#include <cstdio>

namespace obf2::hud {
namespace {

constexpr float k255 = 1.0f / 255.0f;

// The name's text colour, by the row's kind; each table has a friendly and an
// enemy entry, and here the two are the same except for the ordinary one.
// Dead: table 0xa1cf58 (filled at 0x864a80) from 0x97e0a0 / 0x97df10.
constexpr Color kDead{74 * k255, 69 * k255, 53 * k255, 1.0f};
// In the local squad, or the local commander himself: 0xa1cc78 (0x864920) from
// 0x97e080 / 0x97def0.
constexpr Color kSquad{8 * k255, 119 * k255, 2 * k255, 1.0f};
// Ordinary: 0xa1c7b8 (0x8649d0) from 0x97e090 (friendly) and 0x97df00 (enemy).
constexpr Color kFriendly{0.0f, 97 * k255, 1.0f, 1.0f};
constexpr Color kEnemy{253 * k255, 0.0f, 1 * k255, 1.0f};

// Column 0's frame and column 1's kit icon (0x7a3ede, 0x7a3f21).
constexpr Color kFrame{0.576f, 0.573f, 0.451f, 1.0f};  // 0x97e2b0
constexpr Color kKitIcon{98 * k255, 92 * k255, 65 * k255, 1.0f};  // 0x97e2a0
constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

// The rows' backgrounds (0x7a4549, 0x7a44cd).
constexpr Color kOddRow{185 * k255, 181 * k255, 148 * k255, 0.8f};  // 0x97de00, alpha 0x3f4ccccd
constexpr Color kLocalRow{0.996f, 0.7529412f, 0.08235294f, 0.35f};

// A total row's colour (0x7a2bde..): 0x3f4bc6a8, 0x3f4bc6a8, 0x3f178d50, 1.0.
constexpr Color kTotal{0.796f, 0.796f, 0.592f, 1.0f};

// The object's own numbers (the constructor, 0x7a4870).
constexpr float kIconY = 2.0f;         // +0x30
constexpr float kTextY = 0.0f;         // +0x34
constexpr float kRowBackgroundY = 0.0f;       // +0x28
constexpr float kRowBackgroundHeight = 19.0f; // +0x2c
constexpr int kPlayerRows = 21;        // +0x10

// The engine's format strings are Latin-1: § is the byte 0xa7.
// "\xa71%s\xa70" (0x932030) and "\xa71%d\xa70" (0x933d54).
std::string nameText(const std::string& name) { return "\xa7" "1" + name + "\xa7" "0"; }
std::string numberText(int value) { return "\xa7" "1" + std::to_string(value) + "\xa7" "0"; }

void playerColumns(ListData& list, bool totals) {
  // `init` 0x7a2280: after column 0, the object's x positions (+0x304..+0x320,
  // the last twice), every one with `toNext` except column 3 on a total list.
  for (int i = 1; i < 9; ++i) list.addColumn(kScoreboardColumnX[i], !(totals && i == 3));
  list.addColumn(kScoreboardColumnX[8], true);
  if (!totals) list.addHiddenColumn();
  list.visibleRows = totals ? 1 : kPlayerRows;
  list.reserveScrollbar = !totals;  // +0x21
  list.columnsToNext = true;        // +0x22
}

// `sortByScore` (Linux 0x6815f0): best first — the score, then the skill score,
// then fewer deaths, then longer connected. Exact ties keep the list's order,
// which is what a stable sort gives.
bool better(const ScoreboardPlayer& a, const ScoreboardPlayer& b) {
  const PlayerScore sa = a.score.value_or(PlayerScore{});
  const PlayerScore sb = b.score.value_or(PlayerScore{});
  if (sa.score != sb.score) return sa.score > sb.score;
  if (sa.skill != sb.skill) return sa.skill > sb.skill;
  if (sa.deaths != sb.deaths) return sa.deaths < sb.deaths;
  return a.timeConnected > b.timeConnected;
}

}  // namespace

Scoreboard::Scoreboard() {
  playerColumns(friendly_, false);
  playerColumns(enemy_, false);
  playerColumns(friendlyTotal_, true);
  playerColumns(enemyTotal_, true);
}

const ListData* Scoreboard::list(int data) const {
  // 0x7af170.
  switch (data) {
    case 1: return &enemy_;
    case 2: return &friendly_;
    case 9: return &enemyTotal_;
    case 10: return &friendlyTotal_;
    default: return nullptr;
  }
}

void Scoreboard::update(const std::vector<ScoreboardPlayer>& players, int localIndex,
                        const std::string& totalLabel) {
  // 0x7a4c80: nothing happens without a local player.
  const ScoreboardPlayer* local = nullptr;
  for (const ScoreboardPlayer& player : players) {
    if (player.index == localIndex) local = &player;
  }
  if (local == nullptr) return;
  localSquad_ = local->squad;
  localCommander_ = local->commander;
  localTeam_ = local->team;
  localIndex_ = local->index;

  const int enemyTeam = 2 - (localTeam_ != 1 ? 1 : 0);
  fillTeam(friendly_, players, localTeam_);
  fillTeam(enemy_, players, enemyTeam);
  friendlyTotal_.clear();
  totalRow(friendlyTotal_, players, localTeam_, totalLabel);
  enemyTotal_.clear();
  totalRow(enemyTotal_, players, enemyTeam, totalLabel);

  // A key over everything the nodes draw, so they are rebuilt only on a change.
  std::string key;
  for (const ListData* list : {&friendly_, &enemy_, &friendlyTotal_, &enemyTotal_}) {
    for (const ListColumn& column : list->columns()) {
      for (const ListCell& cell : column.cells) {
        key += cell.text;
        key += '|';
        key += cell.texture;
        key += cell.hasBackground ? '#' : '.';
        char colour[64];
        std::snprintf(colour, sizeof colour, "%.3f%.3f%.3f%.3f", cell.color.r, cell.color.g,
                      cell.color.b, cell.color.a);
        key += colour;
      }
      key += '/';
    }
  }
  if (key != lastKey_) {
    lastKey_ = std::move(key);
    ++revision_;
  }
}

void Scoreboard::fillTeam(ListData& list, const std::vector<ScoreboardPlayer>& players,
                          int team) {
  // 0x7a4b30 → 0x7a4820: clear, reset the row count and the team's sums, then a
  // row for each of `getPlayersSortedByScore(team)`.
  list.clear();
  row_ = 0;
  if (team >= 0 && team < 3) sums_[team] = Sums{};
  std::vector<ScoreboardPlayer> ofTeam;
  for (const ScoreboardPlayer& player : players) {
    if (player.team == team) ofTeam.push_back(player);
  }
  std::stable_sort(ofTeam.begin(), ofTeam.end(), better);
  for (const ScoreboardPlayer& player : ofTeam) playerRow(list, player, team);
}

void Scoreboard::playerRow(ListData& list, const ScoreboardPlayer& player, int team) {
  // 0x7a38d0, the view without `ToggleManage`.
  const bool friendly = localTeam_ == team;
  Color color = friendly ? kFriendly : kEnemy;
  if (!player.alive) {
    // `hud->vtbl[0x34c](player)` leads here too; what it asks is not established.
    color = kDead;
  } else if (player.squad == localSquad_ && localSquad_ > 0 && friendly) {
    color = kSquad;
  } else if (player.index == localIndex_ && localCommander_) {
    color = kSquad;
  }

  // Column 0: the frame (+0x324) at x +5 (+0x300).
  list.addTexture(0, "Ingame/Respawn/iconframe.tga", 15.0f, 15.0f, kScoreboardColumnX[0], kIconY,
                  kFrame);
  // Column 1: the kit icon (0x74f320 with the third argument 0).
  std::string kit;
  if (!player.alive) {
    kit = "Ingame/Scoreboard/Icons/icon_Death.tga";
  } else if (player.team != localTeam_) {
    kit = "Ingame/Scoreboard/Icons/icon_Faded.tga";
  } else {
    kit = player.kitIcon;
  }
  list.addTexture(1, kit, 15.0f, 15.0f, 0.0f, kIconY, kKitIcon);
  // Column 2 and 3: `ToggleScore` is on (+0x365, 1 from the constructor): the
  // rank (0x74f8d0) at y +1 and the name at x 0. The rank of a player whose score
  // we do not have is taken as a fresh score's 0.
  const int rank = player.score ? player.score->rank : 0;
  char rankPath[64];
  std::snprintf(rankPath, sizeof rankPath, "Ingame/GeneralIcons/Ranks/rank_%02i.tga",
                rank >= 0 && rank < 0x16 ? rank : 0);
  list.addTexture(2, rankPath, 16.0f, 16.0f, 0.0f, kIconY - 1.0f, kWhite);
  list.addText(3, nameText(player.name), 0.0f, kTextY, color, 0);

  if (player.score) {
    const PlayerScore& s = *player.score;
    list.addText(4, numberText(s.score), 0.0f, kTextY, color, 1);
    list.addText(5, numberText(s.teamwork), 0.0f, kTextY, color, 1);
    list.addText(6, numberText(s.kills), 0.0f, kTextY, color, 1);
    list.addText(7, numberText(s.deaths), 0.0f, kTextY, color, 1);
    list.addEmpty(8);
    list.addText(9, numberText(player.ping), 0.0f, kTextY, color, 1);
    if (team >= 0 && team < 3) {
      Sums& sums = sums_[team];
      sums.ping += player.ping;
      sums.score += s.score;
      sums.kills += s.kills;
      sums.deaths += s.deaths;
      sums.teamwork += s.teamwork;
    }
  } else {
    for (int column = 4; column <= 9; ++column) list.addEmpty(column);
  }
  // Column 10, hidden: the player's index, "%i" (0x89e620).
  list.addText(10, std::to_string(player.index), 0.0f, 0.0f, color, 0);

  // The row's background (0x7a4490..0x7a44cd).
  if (player.index == localIndex_) {
    list.setRowBackground(row_, kLocalRow, 0.0f, kRowBackgroundY, kRowBackgroundHeight);
  } else if (row_ % 2 > 0) {
    list.setRowBackground(row_, kOddRow, 0.0f, kRowBackgroundY, kRowBackgroundHeight);
  }
  ++row_;
}

void Scoreboard::totalRow(ListData& list, const std::vector<ScoreboardPlayer>& players, int team,
                          const std::string& totalLabel) {
  // 0x7a2b60 with squad 0xb.
  int count = 0;
  for (const ScoreboardPlayer& player : players) {
    if (player.team == team) ++count;
  }
  list.addEmpty(0);
  list.addEmpty(1);
  // `#PLAYERS#` replaced by the team's player count (`playerManager->vtbl[0x94]`),
  // at x -37 (0xc2140000), with no § code: the node's font 0.
  std::string label = totalLabel;
  const std::string marker = "#PLAYERS#";
  if (const std::size_t at = label.find(marker); at != std::string::npos) {
    label.replace(at, marker.size(), std::to_string(count));
  }
  list.addText(3, label, -37.0f, kTextY, kTotal, 0);
  list.addEmpty(2);

  const Sums sums = team >= 0 && team < 3 ? sums_[team] : Sums{};
  list.addText(4, numberText(sums.score), 0.0f, kTextY, kTotal, 1);
  list.addText(5, numberText(sums.teamwork), 0.0f, kTextY, kTotal, 1);
  list.addText(6, numberText(sums.kills), 0.0f, kTextY, kTotal, 1);
  list.addText(7, numberText(sums.deaths), 0.0f, kTextY, kTotal, 1);
  list.addEmpty(8);
  list.addText(9, numberText(count > 0 ? sums.ping / count : 0), 0.0f, kTextY, kTotal, 1);
  if (team >= 0 && team < 3) sums_[team] = Sums{};
}

}  // namespace obf2::hud
