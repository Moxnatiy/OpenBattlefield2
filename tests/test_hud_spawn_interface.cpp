// Екран появи як стан і команди — без вікна.
//
// Саме заради цього він і виніс: кнопки екрана не мають власної логіки,
// вони виконують консольні команди, а отже весь екран перевіряється
// виконанням тих самих рядків, що й натискання.
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

// Команду призначає сервер, і при зміні вибір місця скидається:
// кружечки належать прапорам своєї команди.
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

// Головне, заради чого це виносилося: DONE без обраного місця **не**
// закриває екран. Раніше він закривався, і виходила застигла картинка
// без гравця.
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

// І навпаки: якщо запит не пішов (сервер його не взяв), екран теж
// лишається.
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
