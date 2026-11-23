#pragma once

#include <array>
#include <string>
#include <vector>

#include "obf2/hud/squad_list.h"

namespace obf2::session { struct RemoteWorld; }

namespace obf2::app {

// Our team's squads on --connect, and the requests a player makes about them.
//
// Squads 1..9 per team; a player's squad is his own state's 0x2000
// (`setSquadId`, player-state.md), names and locks are `ChangeSquadNameEvent`
// (22) and `SetPrivateSquadEvent` (23), sent for every non-default squad by
// `SquadManager::sendSquadManagerState` (bf2_events.h). Bots are players too:
// a squad the server's AI formed shows its bot members like anyone else's.
struct SquadRoster {
  struct Squad {
    std::string name;
    std::vector<std::string> members;  // "(leader)" appended to the leader
    bool locked = false;
  };
  bool available = false;  // our player and team are known
  int player = -1;
  int team = 0;
  int squad = 0;            // ours, 0 for none
  bool leader = false;
  unsigned invitation = 0;  // squad that invited us, 0 for none
  std::array<Squad, 10> squads;  // [1..9]; [0] unused
};

SquadRoster squadRoster(const session::RemoteWorld& remote);

enum class SquadAction { Join, Leave, Create, Lock, Unlock };

// Queues the event for `action` when the roster allows it; false otherwise.
// Join: a squad with members that is not locked and not ours
// (`JoinSquadEvent` 16). Leave: our own (`LeaveSquadEvent` 17). Create: an
// empty squad while we are in none — naming it creates it
// (`ChangeSquadNameEvent` 22). Lock/Unlock: our squad, as its
// leader (`SetPrivateSquadEvent` 23).
bool requestSquad(session::RemoteWorld& remote, const SquadRoster& roster, SquadAction action,
                  int squad, const std::string& name = {});

// The default name of squad 1..9 shown before the server names it: Retail's
// table 0xa1e380 (init 0x8673e0), which has Gamma for G.
const char* defaultSquadName(int squad);

// The classic squad tab's world (`SquadMenuNew::update`, 0x792100) from the
// session: our player and our team's squads, the leader first in each.
// `ownAlive` stands in for our own player's alive bit, as for the scoreboard.
hud::SquadWorld squadWorld(const session::RemoteWorld& remote, bool ownAlive);

// Sends what the classic squad tab asked for, through the same events as
// `requestSquad`. Create names the squad, which creates it (`ChangeSquadNameEvent`
// 22); the create panel's lock is not sent (its event is not read).
bool sendSquadListAction(session::RemoteWorld& remote, const hud::SquadList::Action& action);

}  // namespace obf2::app
