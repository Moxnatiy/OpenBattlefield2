#include "obf2/hud/spawn_interface.h"

#include <cstdio>

namespace obf2::hud {

void SpawnInterface::setTeamFromServer(int team) {
  if (team <= 0 || team == choice_.team) return;
  choice_.team = team;
  // Кружечки належать прапорам своєї команди, тож вибір при зміні
  // команди більше не має сенсу.
  choice_.marker = 0;
  dirty_ = true;
}

int SpawnInterface::chosenPoint() const {
  if (choice_.marker < 0 || choice_.marker >= static_cast<int>(markerPoints_.size())) return 0;
  return markerPoints_[static_cast<std::size_t>(choice_.marker)];
}

void SpawnInterface::bind(engine::Console& console, std::function<bool(int, int, int)> request) {
  requestSpawn_ = std::move(request);

  // Імена команд — із `setButtonNodeConCmd` самих кнопок екрана
  // (docs/functions/hud-commands.md), а не наші.
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
    // **Екран закриваємо лише тоді, коли запит справді пішов.** Раніше
    // прапорець ставився першим ділом, і коли місце не було обране,
    // запит не йшов — а екран уже зникав. Виходила застигла картинка
    // без гравця, з якої немає виходу.
    if (point == 0 || !requestSpawn_) {
      std::printf("  екран появи: місце появи не обране — запит не пішов\n");
      return;
    }
    requested_ = requestSpawn_(choice_.team, choice_.kit, point);
    if (!requested_) std::printf("  екран появи: запит не пішов, екран лишається\n");
  });

  // Наша власна команда, не з рушія: кружечки місць появи вузлів у даних
  // не мають — їх ловить сама карта, тож консольного імені для них у грі
  // немає. Потрібна для перевірок, щоб вибирати місце командою, а не
  // наведенням миші в піксель.
  console.bind("openbf2.selectSpawn", [this](const con::Command& command) {
    choice_.marker = command.argInt(0).value_or(0);
    dirty_ = true;
  });

  // Ці дві ще не мають за чим працювати, але команду треба з'їсти —
  // інакше консоль вважатиме її невідомою.
  console.bind("spawnManager.selectNextUnlock", [](const con::Command&) {});
  console.bind("spawnManager.commitSuicide", [](const con::Command&) {});
}

}  // namespace obf2::hud
