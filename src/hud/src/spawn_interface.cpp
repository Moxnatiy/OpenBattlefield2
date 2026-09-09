#include "obf2/hud/spawn_interface.h"

#include <cstdio>

namespace obf2::hud {

void SpawnInterface::setTeamFromServer(int team) {
  if (team <= 0 || team == choice_.team) return;
  choice_.team = team;
  // The circles belong to your own team's flags, so a choice made before the team
  // changed no longer makes sense.
  choice_.marker = 0;
  dirty_ = true;
}

int SpawnInterface::chosenPoint() const {
  if (choice_.marker < 0 || choice_.marker >= static_cast<int>(markerPoints_.size())) return 0;
  return markerPoints_[static_cast<std::size_t>(choice_.marker)];
}

void SpawnInterface::bind(engine::Console& console, std::function<bool(int, int, int)> request) {
  requestSpawn_ = std::move(request);

  // The command names come from the screen's own buttons' `setButtonNodeConCmd`
  // (docs/functions/hud-commands.md), not from us.
  console.bind("spawnManager.setPlayerKit", [this](const con::Command& command) {
    choice_.kit = command.argInt(0).value_or(choice_.kit);
    dirty_ = true;
  });
  console.bind("spawnManager.setPlayerTeam", [this](const con::Command& command) {
    choice_.team = command.argInt(0).value_or(choice_.team);
    dirty_ = true;
  });
  console.bind("SpawnManager.toggleMembers", [this](const con::Command& command) {
    choice_.membersTab = command.argInt(0).value_or(0) != 0;
    dirty_ = true;
  });

  console.bind("hudManager.setDone", [this](const con::Command& command) {
    if (command.argInt(0).value_or(1) == 0) {
      requested_ = false;
      return;
    }
    const int point = chosenPoint();
    // **We close the screen only when the request really went out.** The flag used
    // to be set first, and when no spawn point had been chosen the request did not
    // go — while the screen was already gone. The result was a frozen picture with
    // no player and no way out.
    if (point == 0 || !requestSpawn_) {
      std::printf("  spawn screen: no spawn point chosen — the request did not go\n");
      return;
    }
    requested_ = requestSpawn_(choice_.team, choice_.kit, point);
    if (!requested_) std::printf("  spawn screen: the request did not go, the screen stays\n");
  });

  // Our own command, not the engine's: the spawn circles have no nodes in the
  // data — the map catches them itself, so the game has no console name for them.
  // Needed for the checks, so a point can be chosen by a command rather than by
  // pointing the mouse at a pixel.
  console.bind("openbf2.selectSpawn", [this](const con::Command& command) {
    choice_.marker = command.argInt(0).value_or(0);
    dirty_ = true;
  });

  // These two have nothing to work on yet, but the command has to be eaten —
  // otherwise the console will consider it unknown.
  console.bind("spawnManager.selectNextUnlock", [](const con::Command&) {});
  console.bind("spawnManager.commitSuicide", [](const con::Command&) {});
}

}  // namespace obf2::hud
