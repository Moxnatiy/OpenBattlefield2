#include "obf2/app/squad_roster.h"

#include <algorithm>
#include <cstdio>

#include "obf2/net/bf2_link.h"
#include "obf2/session/remote_world.h"

namespace obf2::app {

namespace bf2 = obf2::net::bf2;

const char* defaultSquadName(int squad) {
  static constexpr const char* kNames[] = {"Alpha", "Bravo", "Charlie", "Delta", "Echo",
                                           "Foxtrot", "Gamma", "Hotel", "India"};
  return squad >= 1 && squad <= 9 ? kNames[squad - 1] : "";
}

SquadRoster squadRoster(const session::RemoteWorld& remote) {
  SquadRoster out;
  out.player = remote.world.ownPlayer();
  out.team = remote.world.ownTeam();
  out.invitation = remote.world.squadInvitation();
  if (out.player < 0 || out.team < 1 || out.team > 2) return out;
  out.available = true;
  const auto& players = remote.world.players();
  if (const auto self = players.find(static_cast<std::uint32_t>(out.player)); self != players.end()) {
    out.squad = static_cast<int>(self->second.squad.value_or(0));
    out.leader = self->second.squadLeader;
  }
  for (int squad = 1; squad <= 9; ++squad) {
    const auto& info = remote.world.squads()[out.team][squad];
    out.squads[squad].name = info.name.empty() ? defaultSquadName(squad) : info.name;
    out.squads[squad].locked = info.locked;
  }
  for (const auto& [id, player] : players) {
    if (player.team != out.team) continue;
    const auto squad = player.squad.value_or(0);
    if (squad > 0 && squad < out.squads.size()) {
      out.squads[squad].members.push_back(player.name + (player.squadLeader ? " (leader)" : ""));
    }
  }
  // The leader first, so a one-line summary names who leads the squad.
  for (auto& info : out.squads) {
    std::stable_partition(info.members.begin(), info.members.end(),
                          [](const std::string& m) { return m.ends_with(" (leader)"); });
  }
  return out;
}

bool requestSquad(session::RemoteWorld& remote, const SquadRoster& roster, SquadAction action,
                  int squad, const std::string& name) {
  if (!roster.available || squad < 1 || squad > 9) return false;
  const auto& info = roster.squads[squad];
  auto& link = remote.link;
  switch (action) {
    case SquadAction::Join:
      if (squad == roster.squad || info.members.empty() || info.locked) return false;
      link.queue(bf2::joinSquadEvent(roster.team, roster.player, squad));
      return true;
    case SquadAction::Leave:
      if (squad != roster.squad) return false;
      link.queue(bf2::leaveSquadEvent(roster.team, roster.player, squad, false));
      return true;
    case SquadAction::Create:
      if (!info.members.empty() || roster.squad || name.empty() || name.size() > 31) return false;
      link.queue(bf2::changeSquadNameEvent(roster.team, squad, roster.player, name));
      return true;
    case SquadAction::Lock:
    case SquadAction::Unlock:
      if (!roster.leader || squad != roster.squad) return false;
      link.queue(bf2::setPrivateSquadEvent(roster.team, squad, action == SquadAction::Lock));
      return true;
  }
  return false;
}

hud::SquadWorld squadWorld(const session::RemoteWorld& remote, bool ownAlive) {
  hud::SquadWorld out;
  const SquadRoster roster = squadRoster(remote);
  if (!roster.available) return out;
  out.available = true;
  out.localIndex = roster.player;
  out.team = roster.team;
  out.squad = roster.squad;
  out.leader = roster.leader;
  for (int squad = 1; squad <= 9; ++squad) {
    out.squads[squad].name = roster.squads[squad].name;
    out.squads[squad].locked = roster.squads[squad].locked;
  }
  for (const auto& [id, player] : remote.world.players()) {
    if (player.team != out.team) continue;
    ++out.teamPlayers;
    const bool alive = player.alive ? *player.alive
                                    : (static_cast<int>(id) == out.localIndex ? ownAlive : player.object != 0);
    // The invite list's source: every player of the team (0x44 / 0x4c).
    out.teamPlayerList.push_back(hud::SquadWorld::TeamPlayer{
        static_cast<int>(id), player.name, alive, static_cast<int>(player.squad.value_or(0)),
        player.commander.value_or(false)});
    if (player.commander.value_or(false)) {
      out.commanderIndex = static_cast<int>(id);
      out.commanderName = player.name;
      out.commanderAlive = alive;
      if (static_cast<int>(id) == out.localIndex) out.commander = true;
      continue;  // the commander is in squad 0 and shows on his own row
    }
    const auto squad = player.squad.value_or(0);
    if (squad < 1 || squad > 9) {
      ++out.unassigned;
      continue;
    }
    hud::SquadWorld::Member member;
    member.index = static_cast<int>(id);
    member.name = player.name;
    member.alive = alive;
    member.squadLeader = player.squadLeader;
    out.squads[squad].members.push_back(std::move(member));
  }
  // 0x791110 lists the leader (`[0x99ef84]->vtbl[0x3c]`) first, then the others.
  for (auto& squad : out.squads) {
    std::stable_partition(squad.members.begin(), squad.members.end(),
                          [](const hud::SquadWorld::Member& m) { return m.squadLeader; });
  }
  return out;
}

bool sendSquadListAction(session::RemoteWorld& remote, const hud::SquadList::Action& action) {
  const SquadRoster roster = squadRoster(remote);
  using Kind = hud::SquadList::Action::Kind;
  switch (action.kind) {
    case Kind::Join: return requestSquad(remote, roster, SquadAction::Join, action.squad);
    case Kind::Leave: return requestSquad(remote, roster, SquadAction::Leave, action.squad);
    case Kind::Lock: return requestSquad(remote, roster, SquadAction::Lock, action.squad);
    case Kind::Unlock: return requestSquad(remote, roster, SquadAction::Unlock, action.squad);
    case Kind::Create:
      // 0x7b15f0 calls `setSquadName(team, squad, player, name, false)`, which
      // sends `ChangeSquadNameEvent` alone (Linux 0x4be950): the create panel's
      // lock (`action.locked`) is never sent (docs/functions/squad-menu.md).
      return requestSquad(remote, roster, SquadAction::Create, action.squad, action.name);
    case Kind::Invite:
    case Kind::Uninvite:
      // `inviteToSquad(team, squad, player, invite, false)` → `InviteEvent`
      // (Linux 0x4beff0); a leader only, for our own squad.
      if (!roster.available || !roster.leader || action.squad != roster.squad || action.player < 0) return false;
      remote.link.queue(net::bf2::inviteSquadEvent(static_cast<unsigned>(roster.team),
                                                   static_cast<unsigned>(action.player),
                                                   static_cast<unsigned>(action.squad), action.kind == Kind::Invite));
      return true;
    case Kind::Kick:
      // `removeFromSquad(team, squad, player, true)` → `LeaveSquadEvent` with
      // the flag set (Linux 0x4bef40: team +0x10, player +0x11, squad +0x12,
      // the flag +0x13).
      if (!roster.available || !roster.leader || action.squad != roster.squad || action.player < 0) return false;
      remote.link.queue(net::bf2::leaveSquadEvent(roster.team, action.player, action.squad, true));
      return true;
    case Kind::CommanderApply:
    case Kind::CommanderResign: {
      // The commander row's apply / resign (0x7b1c50, `vtbl[0x2b4]`) is a
      // `CommanderEvent` with our team and player (bf2_link.h).
      if (!roster.available) return false;
      const bool apply = action.kind == Kind::CommanderApply;
      remote.link.queue(net::bf2::commanderEvent(
          apply ? net::bf2::CommanderAction::Apply : net::bf2::CommanderAction::Resign, roster.team,
          roster.player));
      std::printf("  squad menu: commander %s sent\n", apply ? "apply" : "resign");
      return true;
    }
  }
  return false;
}

}  // namespace obf2::app
