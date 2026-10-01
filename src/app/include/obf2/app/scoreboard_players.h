#pragma once
// Who the scoreboard lists, from whatever keeps the players in this mode: the
// session's world on `--connect`, our own server on `--hosted`.
//
// The engine reads them from its player manager (`Scoreboard::update`, 0x7a4c80,
// docs/functions/hud-scoreboard.md). Ours knows less than that manager does, and
// what it does not know is left out rather than made up: no score block, so the
// score columns stay empty, as the engine leaves them for a player without one.
#include <cstdint>
#include <map>
#include <vector>

#include "obf2/hud/scoreboard.h"
#include "obf2/net/bf2_world.h"
#include "obf2/server/game_server.h"

namespace obf2::app {

// `--connect`: the players `CreatePlayerEvent` named, with what their own
// networkable said (docs/functions/player-state.md): alive, the score block,
// ping, squad, rank. A player whose records have not come yet has no score
// block, and "alive" stands in as "the server gave him an object" (for us,
// `ownAlive`) until his first record says.
std::vector<hud::ScoreboardPlayer> scoreboardPlayers(
    const std::map<std::uint32_t, net::bf2::RemotePlayer>& players, int ownPlayer, bool ownAlive);

// `--hosted`: our server's own players, which do keep `alive`.
std::vector<hud::ScoreboardPlayer> scoreboardPlayers(const std::vector<server::Player>& players);

}  // namespace obf2::app
