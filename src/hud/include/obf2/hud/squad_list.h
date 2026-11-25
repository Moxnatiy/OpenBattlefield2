#pragma once
// The spawn screen's squad tab: the engine's `SquadMenuNew`
// (`BF2/Menu/Hud/SquadMenuNew.cpp`, vtable 0x932084, ctor 0x791260, init
// 0x78dcb0). It owns `SquadMemberList` (`setListNodeData 5`, +0x124) and
// `SquadInviteList` (8, +0x128), and answers the `SquadMenu.*` console
// commands the screen's buttons and list rows run. docs/functions/squad-menu.md
// has it whole.
//
// Nothing here talks to the network: what the menu asks of the squad manager
// (`[0x99ef84]`) comes back as an `Action` for the caller to send.
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "obf2/hud/list_data.h"

namespace obf2::hud {

// What the menu reads of the world each update (0x792100): the local player's
// own state and his team's squads.
struct SquadWorld {
  struct Member {
    int index = 0;             // `vtbl[0x50]`
    std::string name;          // `vtbl[0x20]`
    bool alive = false;        // `vtbl[0x68]`
    bool squadLeader = false;  // `vtbl[0x138]`
    // The two kit icons (`hud->vtbl[0x60](player, 1)` column 1 and
    // `hud->vtbl[0x64]` column 3). Which of the kit's icons they are is not
    // established (docs/functions/hud-scoreboard.md), so they come empty.
    std::string kitIcon;
  };
  struct Squad {
    std::string name;  // the squad's `vtbl[0]()`
    bool locked = false;  // the squad's `vtbl[4]()`
    std::vector<Member> members;  // the leader first, as 0x791110 lists them
  };
  bool available = false;  // `[0xa08f60]->vtbl[0x30]()` gave a player
  int localIndex = -1;  // +0x80
  int team = 0;         // +0x98
  int squad = 0;        // +0x94
  bool leader = false;  // +0x9c
  bool commander = false;  // +0x9d
  int commanderIndex = -1;  // `[0x99ef84]->vtbl[0x70](team)`, -1 for none
  std::string commanderName;
  bool commanderAlive = false;
  int teamPlayers = 0;     // for NrOfUnassigned's #TOTAL#
  int unassigned = 0;      // players of the team in no squad
  std::array<Squad, 10> squads;  // [1..9]

  // The invite list's sources. Every player of our team, in the order the
  // world lists them: Retail asks `[0xa08f60]` for them sorted by rank (0x44)
  // or, with +0x88, by score (0x4c) — the sort is not ported.
  struct TeamPlayer {
    int index = 0;           // `getId`
    std::string name;        // `getName`
    bool alive = false;      // `getIsAlive`
    int squad = 0;           // `getSquadId`
    bool commander = false;  // `getIsCommander`
  };
  std::vector<TeamPlayer> teamPlayerList;
  // `getInvitedPlayers(team, squad)` (0xa0): who our squad has invited. What
  // feeds it on a client is not read, so it comes empty.
  std::vector<int> invited;
};

class SquadList {
 public:
  SquadList();

  // What a command asks of the squad manager, for the caller to send.
  struct Action {
    // Invite, Uninvite: `inviteToSquad(team, squad, player, 1 / 0, 0)` (0x2c);
    // Kick: `removeFromSquad(team, squad, player, 1)` (0x34) — applyInviteList.
    // CommanderApply / CommanderResign: the commander row's column 10,
    // `hud->vtbl[0x2b0]` (0x7b1c50) and `vtbl[0x2b4]`.
    enum class Kind { Join, Leave, Lock, Unlock, Create, Invite, Uninvite, Kick, CommanderApply, CommanderResign };
    Kind kind = Kind::Join;
    int squad = 0;
    std::string name;  // Create: the squad's name
    // Create: `SquadCreateLockedShow` at the time. Retail never sends it
    // (+0xea has no reader, docs/functions/squad-menu.md).
    bool locked = false;
    int player = -1;   // Invite, Uninvite, Kick
  };

  // `update(visible)` (0x792100): read the world; when the tab is shown, fill the
  // lists. Returns whether the member list changed.
  bool update(const SquadWorld& world, bool visible);

  // The `SquadMenu.*` commands (handlers 0x76d8d5..0x76de3e). Each returns the
  // action it asks for, if any.
  std::optional<Action> singleClickNew();          // 0x791ce0
  std::optional<Action> createSquad();             // 0x78f9b0
  void setSquadCreateSelect(bool on);              // 0x78f5e0
  void squadCreateLockToggle();                    // 0x78f220
  // `setSquadNameSelect` 0x78f190: while the create panel is up, focus the
  // name box (0x7b11e0: HUD state 0xd, the edit node takes the keyboard).
  bool setSquadNameSelect();
  // The keyboard's text into the box, at most kNameLength characters
  // (`setEditNodeMaxLength 10`, HudElementsSquadsNew.con:182).
  void setNameBox(const std::string& text);
  void endNameEdit() { nameFocused_ = false; }
  bool nameFocused() const { return nameFocused_ && createShow_; }
  static constexpr std::size_t kNameLength = 10;
  void setShowInviteList(bool on);                 // 0x7918e0
  // `singleClickInvite` 0x791f50: the checkbox column toggles the row's player
  // (0x791940). False when the squad has no room ("warning" in Retail).
  bool singleClickInvite();
  // `applyInviteList` 0x791fd0: the kicks and withdrawals, then the invites.
  std::vector<Action> applyInviteList();
  // `doubleClickNew` and `doubleClickInvite` both run 0x83d270, a bare `ret`.

  // `setListNodeData 5` and `8`.
  const ListData* list(int data) const;
  // `setEditNodeString 2`: the create panel's name box, null for any other.
  const std::string* editString(int string) const;
  const std::string& nameBox() const { return nameBox_; }
  ListData& memberList() { return members_; }
  ListData& inviteList() { return invites_; }

  // The variables the menu registers in its init (0x78dcb0), by name.
  bool boolVariable(const std::string& name, bool& out) const;
  bool stringVariable(const std::string& name, std::string& out) const;

  int revision() const { return revision_; }
  // The HUD state a command asks for (`hud->vtbl[0x1dc](state, 1)`), once.
  std::optional<int> takeRequestedState();

  // The lexicon for the labels; unset gives the keys themselves.
  std::function<std::string(const std::string&)> text;

 private:
  void fill();                                 // 0x791b20
  void commanderRow();                         // 0x78e6a0
  void createRow();                            // 0x78eca0
  void squadRow(int squad, int memberCount);   // 0x78fc30
  void memberRow(const SquadWorld::Member& member);  // 0x7902c0
  void inviteFill();                                     // 0x791b20 step 5
  void inviteHeader(const std::string& left, const std::string& right);  // 0x78e520
  void inviteRow(const SquadWorld::TeamPlayer* player, int squad);    // 0x790a90
  const SquadWorld::TeamPlayer* teamPlayer(int index) const;  // `getPlayerFromId`
  bool expanded(int squad) const;
  void setExpanded(int squad, bool on);
  std::string label(const std::string& key) const;
  std::string firstFreeName() const;           // 0x78f0e0 over 0x7b1750

  ListData members_, invites_;
  // The name box's text: the manager's string table slot the box shows
  // (`hud->vtbl[0x284]` 0x7b13a0 writes it, `vtbl[0x288]` 0x7b1410 reads it).
  std::string nameBox_;
  bool nameFocused_ = false;  // the edit node's focus (+0x28 of the node)
  // The invite list's state, player indices (0x791260's lists): picked to
  // invite (+0x100), ours marked to kick (+0x10c).
  std::vector<int> picked_, kicked_;
  SquadWorld world_;
  int row_ = 0;               // +0x78
  int selectedSquad_ = -1;    // +0x8c
  int selectedPlayer_ = -1;   // +0x90
  bool showCommander_ = true;     // +0x9e
  bool commanderExpanded_ = true; // +0x9f
  bool shown_ = false;            // +0xa0 SquadMemberListShow
  int lastSquad_ = 0;             // our squad at the previous update (not a Retail field)
  bool inviteList_ = false;       // +0xa1 InviteListShow
  unsigned expanded_ = 0;         // +0xa4, a bit per squad 0..9
  bool createShow_ = false;       // +0xe8 SquadCreateShow
  bool renameShow_ = false;       // +0xe9 SquadRenameShow
  bool createLocked_ = false;     // +0xea SquadCreateLockedShow
  std::string createTag_;         // +0xec SquadCreateTagString
  std::string squadsText_, unassignedText_;  // NrOfSquads, NrOfUnassigned
  std::optional<int> requestedState_;
  int revision_ = 0;
};

}  // namespace obf2::hud
