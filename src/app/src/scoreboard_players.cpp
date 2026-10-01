#include "obf2/app/scoreboard_players.h"

namespace obf2::app {

std::vector<hud::ScoreboardPlayer> scoreboardPlayers(
    const std::map<std::uint32_t, net::bf2::RemotePlayer>& players, int ownPlayer, bool ownAlive) {
  std::vector<hud::ScoreboardPlayer> out;
  for (const auto& [id, remote] : players) {
    hud::ScoreboardPlayer player;
    player.index = static_cast<int>(id);
    player.name = remote.name;
    player.team = remote.team;
    // The player's own state (player_state.h) when it has come; until then the
    // stand-ins below.
    if (remote.alive) {
      player.alive = *remote.alive;
    } else {
      player.alive = static_cast<int>(id) == ownPlayer ? ownAlive : remote.object != 0;
    }
    if (remote.score) {
      hud::PlayerScore score;
      score.score = remote.score->score;
      score.teamwork = remote.score->teamwork;
      score.kills = remote.score->kills;
      score.deaths = remote.score->deaths;
      score.rank = remote.rank.value_or(0);
      player.score = score;
    }
    player.ping = static_cast<int>(remote.ping.value_or(0));
    player.squad = static_cast<int>(remote.squad.value_or(0));
    player.commander = remote.commander.value_or(false);
    out.push_back(std::move(player));
  }
  return out;
}

std::vector<hud::ScoreboardPlayer> scoreboardPlayers(const std::vector<server::Player>& players) {
  std::vector<hud::ScoreboardPlayer> out;
  for (const server::Player& served : players) {
    hud::ScoreboardPlayer player;
    player.index = static_cast<int>(served.id);
    player.name = served.name;
    player.team = served.team;
    player.alive = served.alive;
    out.push_back(std::move(player));
  }
  return out;
}

}  // namespace obf2::app
