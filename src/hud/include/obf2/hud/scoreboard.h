#pragma once
// The scoreboard's rows: the engine's `Scoreboard` (`BF2/Menu/Hud/Scoreboard.cpp`,
// ctor 0x7a4870, `update` 0x7a4c80), which writes the players into the four lists
// the scoreboard's list nodes show. docs/functions/hud-scoreboard.md has it whole.
//
// Only the default view is here — `ToggleScore`, the players by score. The squad
// view (`ToggleSquads`) and the manage view (`ToggleManage`) are debt, and say so
// in the notes.
#include <optional>
#include <string>
#include <vector>

#include "obf2/hud/list_data.h"

namespace obf2::hud {

// A player's `PlayerScoreData` (`player->vtbl[0x170]`), with the fields named by
// the Linux server's `pmgr_getScore` (0x4fc780).
struct PlayerScore {
  int score = 0;       // [0]
  int teamwork = 0;    // [1] rplScore
  int skill = 0;       // [2] skillScore
  int commander = 0;   // [3] cmdScore
  int deaths = 0;      // [4]
  int kills = 0;       // [5]
  int rank = 0;        // [8]
};

struct ScoreboardPlayer {
  int index = 0;       // `vtbl[0x50]`
  std::string name;    // `vtbl[0x20]`
  int team = 0;        // `vtbl[0xe4]`
  int squad = 0;       // `vtbl[0x108]`, 0 when in none
  bool commander = false;  // `vtbl[0x110]`
  bool alive = false;      // `vtbl[0x68]`, `Player::getIsAlive` (Linux 0x4a23f0)
  // Absent when we do not have the player's score: the engine then leaves
  // columns 4..9 empty (0x7a4353..0x7a443e).
  std::optional<PlayerScore> score;
  int ping = 0;        // `vtbl[0x164]`
  // The kit's own icon for a living teammate (`hud->vtbl[0x64]`, 0x74f320).
  // Which of the kit's icons it is has not been established, so it stays empty.
  std::string kitIcon;
  double timeConnected = 0.0;  // `getTimeConnected`, the sort's last key
};

class Scoreboard {
 public:
  Scoreboard();

  // `update(true)` (0x7a4c80): the lists from the players and the local one.
  // `totalLabel` is `HUD_TEXT_MENU_SCOREBOARD_NUMBER_OF_PLAYERS` as the
  // dictionary gives it, `#PLAYERS#` still in it.
  void update(const std::vector<ScoreboardPlayer>& players, int localIndex,
              const std::string& totalLabel);

  // `setListNodeData <n>` (0x7af170): 1 and 2 are the enemy and friendly players,
  // 9 and 10 the enemy and friendly totals; any other number is not ours.
  const ListData* list(int data) const;

  // Whether the lists changed at the last update: the node is rebuilt only then.
  int revision() const { return revision_; }

 private:
  void fillTeam(ListData& list, const std::vector<ScoreboardPlayer>& players, int team);
  void playerRow(ListData& list, const ScoreboardPlayer& player, int team);
  void totalRow(ListData& list, const std::vector<ScoreboardPlayer>& players, int team,
                const std::string& totalLabel);

  ListData friendly_, friendlyTotal_, enemy_, enemyTotal_;

  // The local player as `update` reads him (+0x14, +0x18, +0x1c, +0x25).
  int localSquad_ = 0;
  int localIndex_ = -1;
  int localTeam_ = 2;
  bool localCommander_ = false;
  int row_ = 0;  // +0x38

  // The team's sums (+0x60 ping, +0xf0 score, +0x174 kills, +0x1f8 deaths,
  // +0x27c teamwork), per team; the squads' split does not matter to the total.
  struct Sums {
    int ping = 0, score = 0, kills = 0, deaths = 0, teamwork = 0;
  };
  Sums sums_[3];

  int revision_ = 0;
  std::string lastKey_;
};

// The columns' x (the constructor, 0x7a4921): +0x300..+0x320.
inline constexpr float kScoreboardColumnX[9] = {5, 5, 21, 44, 212, 244, 276, 308, 340};

}  // namespace obf2::hud
