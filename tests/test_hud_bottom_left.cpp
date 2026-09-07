// Ліва кутова ділянка: машина станів із BF2.exe, 0x78b600.
#include <cmath>

#include "obf2/hud/bottom_left.h"
#include "check.h"

using namespace obf2;

namespace {

// Прокрутити секунди по кроку такту.
void run(hud::BottomLeftPanel& panel, hud::BottomLeftMode mode, float seconds) {
  const float step = 1.0f / 30.0f;
  for (float t = 0.0f; t < seconds; t += step) panel.update(mode, 0.8f, step);
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

  panel.update(hud::BottomLeftMode::Hidden, 0.8f, 1.0f / 30.0f);
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

  panel.update(hud::BottomLeftMode::Health, 1.0f, 1.0f / 30.0f);
  CHECK(std::abs(panel.healthFadedAlpha - 1.0f) < 0.01f);
}

TEST_MAIN({
  testOnFootStopsAtFootPosition();
  testVehicleGoesFurther();
  testHiddenWaitsForAlpha();
  testFadedFollowsBackgroundAlpha();
});
