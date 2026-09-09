// The spawn screen as state and commands — without a window.
//
// That is exactly what it was pulled out for: the screen's buttons have no logic of
// their own, they run console commands, and so the whole screen is checked by
// running the same lines a click would.
#include <string>
#include <vector>

#include "obf2/hud/spawn_interface.h"
#include "check.h"

using namespace obf2;

void testConsoleCommandsChangeChoice() {
  engine::Console console;
  hud::SpawnInterface spawn;
  spawn.bind(console, [](int, int, int) { return true; });

  CHECK_EQ(spawn.choice().kit, 0);
  console.executeLine("spawnManager.setPlayerKit 3");
  CHECK_EQ(spawn.choice().kit, 3);
  CHECK(spawn.dirty());

  console.executeLine("SpawnManager.toggleMembers 1");
  CHECK(spawn.choice().membersTab);
}

// The team is assigned by the server, and on a change the point choice is reset:
// the circles belong to your own team's flags.
void testServerTeamResetsMarker() {
  engine::Console console;
  hud::SpawnInterface spawn;
  spawn.bind(console, [](int, int, int) { return true; });
  spawn.setMarkerPoints({401, 402});
  console.executeLine("openbf2.selectSpawn 1");
  CHECK_EQ(spawn.chosenPoint(), 402);

  spawn.setTeamFromServer(2);
  CHECK_EQ(spawn.choice().team, 2);
  CHECK_EQ(spawn.choice().marker, 0);
  CHECK_EQ(spawn.chosenPoint(), 401);
}

// The main thing this was pulled out for: DONE with no point chosen does **not**
// close the screen. It used to close, and the result was a frozen picture with no
// player.
void testDoneWithoutMarkerKeepsScreen() {
  engine::Console console;
  hud::SpawnInterface spawn;
  int calls = 0;
  spawn.bind(console, [&](int, int, int) {
    ++calls;
    return true;
  });

  console.executeLine("hudManager.setDone 1");
  CHECK_EQ(calls, 0);
  CHECK(!spawn.requested());

  spawn.setMarkerPoints({401});
  console.executeLine("hudManager.setDone 1");
  CHECK_EQ(calls, 1);
  CHECK(spawn.requested());
}

// And the other way round: if the request did not go (the server did not take it),
// the screen stays too.
void testRefusedRequestKeepsScreen() {
  engine::Console console;
  hud::SpawnInterface spawn;
  spawn.bind(console, [](int, int, int) { return false; });
  spawn.setMarkerPoints({401});
  console.executeLine("hudManager.setDone 1");
  CHECK(!spawn.requested());
}

TEST_MAIN({
  testConsoleCommandsChangeChoice();
  testServerTeamResetsMarker();
  testDoneWithoutMarkerKeepsScreen();
  testRefusedRequestKeepsScreen();
});
