#pragma once
// What a live server's round hands the HUD each frame, from the session's world:
//
//   * the tickets — the score manager's ghost (score_state.h) through
//     `HudInformationLayer::updateTickets` (PE 0x4646b0), our team's and the
//     other's (`(team != 2) + 1`);
//   * the kill feed — each `KilledByEvent` the session kept (`kills`) as the
//     manager's lines (kill_feed.h, `hud::Manager::killedBy`);
//   * the chat — each string the session assembled (`chats`, chat_string.h) as
//     `Game::outputChatString` routes it (`hud::Manager::chatString`);
//   * the control points' owners — each point's `ControlPointNetworkable`
//     team (control_point_state.h, bit 0x01, `setTeam`) once a record has
//     come, so the map's flags follow captures (`hud::Manager::controlPointTeams`);
//   * our awards — the medal case's window (awards.h).
//
// The session's queues are drained here, once per frame.
#include <map>

#include "obf2/hud/manager.h"
#include "obf2/net/bf2_world.h"
#include "obf2/net/chat_string.h"
#include "obf2/session/remote_world.h"

namespace obf2::app {

// One chat string as the HUD takes it: the sender's name, team and whether he
// is the commander from the player list, and the squad colour
// (`selectPlayerColors`: a squadmate of ours on our side).
hud::Manager::ChatLine chatLineForHud(const net::bf2::ChatString& chat,
                                      const std::map<std::uint32_t, net::bf2::RemotePlayer>& players,
                                      int ownPlayer);

void updateLiveHud(session::RemoteWorld& remote, hud::Manager& hud, float seconds);

}  // namespace obf2::app
