#include "obf2/app/live_hud.h"

#include "obf2/app/awards.h"
#include "obf2/app/kill_feed.h"

namespace obf2::app {

hud::Manager::ChatLine chatLineForHud(const net::bf2::ChatString& chat,
                                      const std::map<std::uint32_t, net::bf2::RemotePlayer>& players,
                                      int ownPlayer) {
  hud::Manager::ChatLine line;
  line.channel = chat.channel;
  line.type = chat.type;
  line.text = chat.text;
  const auto sender = players.find(static_cast<std::uint32_t>(chat.sender));
  if (chat.sender < 0 || sender == players.end()) return line;
  line.senderKnown = true;
  line.senderName = sender->second.name;
  line.senderTeam = sender->second.team;
  line.senderCommander = sender->second.commander.value_or(false);
  line.speaker.commander = line.senderCommander;
  const auto self = players.find(static_cast<std::uint32_t>(ownPlayer));
  const auto squad = sender->second.squad.value_or(0);
  line.speaker.squadColour = squad != 0 && self != players.end() &&
                             self->second.team == sender->second.team &&
                             self->second.squad.value_or(0) == squad;
  return line;
}

void updateLiveHud(session::RemoteWorld& remote, hud::Manager& hud, float seconds) {
  const auto& players = remote.world.players();
  const int ownPlayer = remote.world.ownPlayer();
  const int own = remote.world.ownTeam() == 2 ? 2 : 1;

  const auto& score = remote.world.score();
  if (score.known) {
    const auto tickets = [&](int team) {
      const auto& value = score.teams[static_cast<std::size_t>(team - 1)];
      return hud::Manager::TeamTickets{value.tickets, value.ticketBleed, value.ticketState};
    };
    hud.updateTickets(tickets(own), tickets(3 - own), seconds);
  }

  if (!remote.kills.empty()) {
    const auto local = localForHud(players, ownPlayer, remote.world.ownTeam());
    for (const auto& kill : remote.kills) {
      hud.killedBy(killForHud(kill, players, ownPlayer, remote.templateNumbers, remote.registry),
                   local);
    }
    remote.kills.clear();
  }

  for (const auto& chat : remote.chats) {
    hud.chatString(chatLineForHud(chat, players, ownPlayer), ownPlayer >= 0);
  }
  remote.chats.clear();

  std::map<int, int> teams;
  for (const auto& [id, point] : remote.world.controlPoints()) {
    if (point.stateKnown) teams[point.levelId] = point.team;
  }
  if (!teams.empty()) hud.controlPointTeams(teams);

  takeAwards(remote, hud);
}

}  // namespace obf2::app
