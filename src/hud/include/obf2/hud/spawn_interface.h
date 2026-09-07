#pragma once
// Екран появи: стан вибору й консольні команди, якими його змінюють.
//
// Назва не наша: в оригіналі це `Code/BF2/Menu/Hud/SpawnInterface.cpp`
// (шлях видно в `BF2_r.exe`), і наш модуль тримається того самого поділу.
//
// Кнопки цього екрана не мають власної логіки — кожна виконує консольну
// команду з `setButtonNodeConCmd` (docs/functions/hud-commands.md). Тому
// весь екран зводиться до кількох значень і обробників, які їх міняють,
// а це можна перевірити тестом без вікна.
#include <functional>
#include <string>
#include <vector>

#include "obf2/engine/console.h"

namespace obf2::hud {

// Що обрав гравець. Команду призначає сервер, а не гравець: у знятому
// трафіку оригінальний клієнт `NESelectTeam` навіть не шле — приймає ту,
// яку дав сервер у `CreatePlayerEvent`.
struct SpawnChoice {
  int team = 1;
  int kit = 0;
  // Номер кружечка в переліку місць появи, а не номер точки.
  int marker = 0;
  bool membersTab = false;
};

class SpawnInterface {
 public:
  // Реєструє обробники команд екрана появи. `requestSpawn` викликається
  // з (команда, набір, номер контрольної точки) і повертає, чи запит
  // справді пішов серверу: якщо ні, екран лишається на місці. Інакше
  // виходила застигла картинка без гравця.
  void bind(engine::Console& console, std::function<bool(int, int, int)> requestSpawn);

  const SpawnChoice& choice() const { return choice_; }
  // Поки екран появи ще наполовину в `main.cpp`, йому потрібен прямий
  // доступ. Разом із рештою переїзду це має зникнути.
  SpawnChoice& mutableChoice() { return choice_; }
  void markDirty() { dirty_ = true; }
  bool requested() const { return requested_; }
  void setRequested(bool value) { requested_ = value; }
  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }

  // Команду ставить сервер; при зміні скидається вибір місця, бо кружечки
  // належать прапорам своєї команди.
  void setTeamFromServer(int team);
  void setMarkerPoints(std::vector<int> points) { markerPoints_ = std::move(points); }
  const std::vector<int>& markerPoints() const { return markerPoints_; }

  // Номер контрольної точки під обраним кружечком; нуль — не обрано.
  int chosenPoint() const;

  // Гравець з'явився або помер — екран відкривається знову.
  void reset() { requested_ = false; }

 private:
  SpawnChoice choice_;
  std::vector<int> markerPoints_;
  std::function<bool(int, int, int)> requestSpawn_;
  bool requested_ = false;
  bool dirty_ = false;
};

}  // namespace obf2::hud
