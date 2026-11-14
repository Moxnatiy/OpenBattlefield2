#pragma once
// The spawn screen's squad tab: the member list (`SquadMemberList`, list data 5)
// and the create panel, as `HudSquad` fills and answers them.
//
// Taken from OpenBF2's reconstruction (Menu/HudSquad.cpp, `BF2.exe` 0x78e2c0 ..
// 0x791ce0): the rows (`refreshRows`, `paintCommanderHeading`, `paintCreateRow`,
// `paintSquadRow`, `paintPlayerRow`), the columns (`registerData`), the colours
// (the 0x97de.. constants and HudColors' palettes at index 2) and the click
// (`singleClickNew`). Nothing here talks to a server: a click gives back an
// `Action`, and whoever holds the connection turns it into the request
// (`ClientSquadManager`: `JoinSquadEvent`, `LeaveSquadEvent`,
// `ChangeSquadNameEvent`, `SetPrivateSquadEvent`).
//
// Not done, and said so in the log when reached: the commander band's apply,
// resign and mutiny (no commander requests yet), the invite list (data 8, the
// squad leader's "manage"), the squad-name edit box (the default name is used),
// voice icons and the vehicle and kit icons in a player row.
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/hud/list_data.h"

namespace obf2::hud {

struct SquadPlayer {
  int id = -1;
  std::string name;
  int team = 0;
  int squad = 0;        // 0 when in none (`getSquadId`)
  bool leader = false;  // `getIsSquadLeader`
  bool commander = false;
  bool alive = false;
};

struct SquadWorld {
  std::vector<SquadPlayer> players;
  int localId = -1;
  // The names and locks the server set, by team (1, 2) and squad (1..9); an empty
  // name is the default one.
  std::function<std::string(int team, int squad)> name;
  std::function<bool(int team, int squad)> locked;
};

class SquadList {
 public:
  using Text = std::function<std::string(std::string_view key)>;

  SquadList();

  // `setVisible(true)` → `refreshTitle`, `refreshCounts`, `refreshRows`.
  void update(const SquadWorld& world, const Text& text);

  // `setListNodeData 5` is the member list; any other number is not ours.
  const ListData* list(int data) const { return data == 5 ? &members_ : nullptr; }

  // What a click asks for.
  struct Action {
    enum class Kind { None, Join, Leave, Create, SetPrivate, NotDone } kind = Kind::None;
    int squad = 0;
    std::string name;
    bool flag = false;
    std::string what;  // NotDone: what the original would do here
  };

  // The list was clicked at a row and a column (`singleClickNew`, 0x791ce0).
  Action click(int row, int column);
  // `SquadMenu.setSquadCreateSelect 0|1` (0x78f5e0): open or close the panel.
  Action setCreateMode(bool enabled);
  // `SquadMenu.createSquad` (0x78f9b0 → `BF2HudManager::createSquad`).
  Action createSquad();
  // `SquadMenu.squadCreateLockToggle` (0x78f220).
  void toggleCreateLock() { createLocked_ = !createLocked_; }

  // What the screen's variables read (`registerData`).
  bool inSquad() const { return squadId_ > 0; }
  bool squadLeader() const { return squadLeader_; }
  bool createVisible() const { return createVisible_; }
  bool createLocked() const { return createLocked_; }
  const std::string& title() const { return title_; }
  const std::string& unassignedText() const { return unassignedText_; }
  const std::string& squadCountText() const { return squadCountText_; }
  const std::string& createTag() const { return createTag_; }

 private:
  int membersOf(int squad) const;
  int findEmptySquad() const;  // `BF2HudManager::findEmptySquad`
  std::string squadName(int squad) const;
  std::string defaultNameFor(int squad) const;  // `getAvailableSquadName`

  void paintCommanderHeading(const Text& text);
  void paintCreateRow(const Text& text);
  void paintSquadRow(int squad, int memberCount, const Text& text);
  void paintPlayerRow(const SquadPlayer& player);

  ListData members_;
  SquadWorld world_;
  int rowCount_ = 0;
  int team_ = 2;
  int squadId_ = 0;
  int playerId_ = -1;
  bool squadLeader_ = false;
  bool commander_ = false;
  bool commanderExpanded_ = true;   // +0x9f
  int selectedSquad_ = -1;          // +0x8c
  int selectedPlayer_ = -1;         // +0x90
  std::array<bool, 10> expanded_{};  // +0xa4, a bitset
  bool createVisible_ = false;
  bool createLocked_ = false;
  std::string title_, unassignedText_, squadCountText_, createTag_;
  // The hidden columns 13 and 14 of each row: the squad or player and the type
  // (0 commander band, 2 squad, 3 create, 4 player).
  std::vector<std::pair<int, int>> rowKeys_;
};

}  // namespace obf2::hud
