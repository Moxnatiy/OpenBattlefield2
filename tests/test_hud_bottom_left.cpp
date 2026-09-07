// Ліва кутова ділянка: машина станів із BF2.exe, 0x78b600.
#include <cmath>

#include "obf2/hud/bottom_left.h"
#include "obf2/meme/graph.h"
#include "check.h"

using namespace obf2;

namespace {

// Один такт: машина станів вибирає цілі, а рухає значення граф. Тут
// граф підмінено тією самою дією, яку він і виконує
// (`SetVariableSine`/`Soft` зі швидкостями з файлу).
void tick(hud::BottomLeftPanel& panel, hud::BottomLeftMode mode, float background) {
  const float step = 1.0f / 30.0f;
  panel.update(mode, background);
  meme::approachVariable(panel.x, panel.targetX, hud::kCornerMoveSpeed, 0.0f, step);
  meme::approachVariable(panel.healthAlpha, panel.targetHealthAlpha, hud::kCornerAlphaSpeed, 0.0f,
                         step);
  meme::approachVariable(panel.vehicleAlpha, panel.targetVehicleAlpha, hud::kCornerAlphaSpeed,
                         0.0f, step);
}

// Прокрутити секунди по кроку такту.
void run(hud::BottomLeftPanel& panel, hud::BottomLeftMode mode, float seconds) {
  const float step = 1.0f / 30.0f;
  for (float t = 0.0f; t < seconds; t += step) tick(panel, mode, 0.8f);
}

}  // namespace

// Пішки ділянка виїжджає на -137 і показує смуги здоров'я, а смуги
// техніки лишаються згаслими. Саме на цьому ми й ловилися: із чужим
// положенням плашка тягнулася на всю ширину, ніби гравець у техніці.
void testOnFootStopsAtFootPosition() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Health, 2.0f);

  CHECK(std::abs(panel.x - hud::kBottomLeftFootX) < 0.5f);
  CHECK(std::abs(panel.healthAlpha - 1.0f) < 0.01f);
  CHECK(std::abs(panel.vehicleAlpha) < 0.01f);
}

// У техніці ділянка їде далі — на 54, і аж там проступають смуги
// техніки. Але тільки після того, як проступили смуги здоров'я: у
// 0x78b600 це видно з умови `HealthAlpha == 1.0`.
void testVehicleGoesFurther() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Vehicle, 3.0f);

  CHECK(std::abs(panel.x - hud::kBottomLeftVehicleX) < 0.5f);
  CHECK(std::abs(panel.healthAlpha - 1.0f) < 0.01f);
  CHECK(std::abs(panel.vehicleAlpha - 1.0f) < 0.01f);
}

// Ховається ділянка не одразу: спершу гаснуть смуги, і лише тоді вона
// їде за край.
void testHiddenWaitsForAlpha() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Health, 2.0f);

  tick(panel, hud::BottomLeftMode::Hidden, 0.8f);
  CHECK(panel.x > hud::kBottomLeftHiddenX);  // ще не поїхала

  run(panel, hud::BottomLeftMode::Hidden, 3.0f);
  CHECK(std::abs(panel.x - hud::kBottomLeftHiddenX) < 0.5f);
  CHECK(std::abs(panel.healthAlpha) < 0.01f);
}

// Пригашена прозорість — це прозорість мінус те, що з'їдає прозорість
// плашок із профілю: clamp(Alpha - (1 - основа), 0, 1).
void testFadedFollowsBackgroundAlpha() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Health, 2.0f);
  CHECK(std::abs(panel.healthFadedAlpha - 0.8f) < 0.01f);

  tick(panel, hud::BottomLeftMode::Health, 1.0f);
  CHECK(std::abs(panel.healthFadedAlpha - 1.0f) < 0.01f);
}

// Дія графа: без гальмівної ділянки — рівномірно, з нею — крок згасає
// синусоїдою, і до цілі не перескакує.
void testGraphActionCurve() {
  float value = 0.0f;
  meme::approachVariable(value, 100.0f, 600.0f, 0.0f, 1.0f / 30.0f);
  CHECK(std::abs(value - 20.0f) < 0.01f);

  // Ціль ближче за крок — стаємо рівно на неї, не далі.
  value = 99.0f;
  meme::approachVariable(value, 100.0f, 600.0f, 0.0f, 1.0f / 30.0f);
  CHECK(std::abs(value - 100.0f) < 0.001f);

  // З гальмуванням: на півдорозі гальмівної ділянки крок менший за
  // повний, бо sin(pi/4) < 1.
  value = 95.0f;
  meme::approachVariable(value, 100.0f, 600.0f, 10.0f, 1.0f / 30.0f);
  CHECK(value > 95.0f);
  CHECK(value < 95.0f + 20.0f);

  // Назад працює так само.
  value = 100.0f;
  meme::approachVariable(value, 0.0f, 600.0f, 0.0f, 1.0f / 30.0f);
  CHECK(std::abs(value - 80.0f) < 0.01f);
}

TEST_MAIN({
  testGraphActionCurve();
  testOnFootStopsAtFootPosition();
  testVehicleGoesFurther();
  testHiddenWaitsForAlpha();
  testFadedFollowsBackgroundAlpha();
});
