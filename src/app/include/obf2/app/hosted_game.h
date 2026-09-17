#pragma once
// A single-player game: our own server and a client on an in-memory loop.
//
// In BF2 the server owns the world even offline, so the placement the renderer
// draws comes from the packets that arrived over that loop rather than from the
// level's file. The settings the server starts with come from the same files the
// original reads — `GameLogicInit.con` for the tickets and
// `Settings/ServerSettings.con` for the round.
#include <cstdint>
#include <memory>
#include <vector>

#include "obf2/level/level.h"
#include "obf2/server/collision_objects.h"
#include "obf2/server/game_client.h"
#include "obf2/server/game_server.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

struct HostedGame {
  std::unique_ptr<server::GameServer> server;
  std::unique_ptr<server::GameClient> client;
  // The soldier the server gave our player, zero until he has spawned.
  std::uint32_t localSoldierId = 0;
  // What the client heard about: the scene is built from this, not from the
  // level's own object list.
  std::vector<level::StaticObject> placement;
};

// The round's settings as the game's data gives them.
server::ServerSettings hostedSettings(FileSystem& files, const level::Level& level);

// Starts the pair and spins until the client stops receiving new objects. The
// collision library must outlive the game: the world it builds points into it.
HostedGame startHostedGame(FileSystem& files, const level::Level& level,
                           server::CollisionLibrary& collision);

}  // namespace obf2::app
