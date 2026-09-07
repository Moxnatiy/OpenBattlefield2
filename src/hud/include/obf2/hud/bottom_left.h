#pragma once
// Ліва кутова ділянка HUD: куди вона їде і як проступають її дві
// половини — смуги здоров'я й смуги техніки.
//
// Це не наша вигадка й не одна змінна показу, а маленька машина станів у
// клієнті: `BF2.exe`, 0x78b600 (вибір цілі, щокадру) і 0x78b870 (вибір
// режиму). Поля об'єкта HUD, які вона веде, зареєстровані як змінні
// графа в 0x789480 — саме через них ділянка й рухається.
//
//   +0x170  режим: 0 сховати, 1 здоров'я (пішки), 2 техніка
//   +0x178  -295  сховане положення        \ усі три задає
//   +0x17c  -137  положення пішки          | конструктор HUD
//   +0x180    54  положення в техніці      / 0x78c560
//   +0x184  `BottomLeft_XPos`      поточне
//   +0x188  `BottomLeft_nextXPos`  цільове
//   +0x18c  `BottomLeft_alpha1` = `BottomLeftHealthAlpha`
//   +0x190  `BottomLeft_alpha2` = `BottomLeftVehicleAlpha`
//   +0x194  `BottomLeftHealthFadedAlpha`
//   +0x198  `BottomLeftVehicleFadedAlpha`
//   +0x19c  `BottomLeft_nextAlpha1`  ціль для +0x18c
//   +0x1a0  `BottomLeft_nextAlpha2`  ціль для +0x190
//
// Хто що рухає: **цілі** пише ця машина, а самі значення веде граф
// `Menu/Ingame` — `SetVariableSineAction {Speed 600}` для положення і
// `SetVariableSoftAction {Speed 10}` для прозоростей. Обидві дії —
// рівномірний підхід до цілі (docs/functions/hud-animation.md).
#include "obf2/hud/animation.h"

namespace obf2::hud {

// Режим ділянки. Ставить його 0x78b870: типово «пішки», а «техніка» —
// коли керований об'єкт гравця не є його солдатом.
enum class BottomLeftMode { Hidden, Health, Vehicle };

struct BottomLeftPanel {
  // Те, що веде граф.
  float x = kBottomLeftHiddenX;
  float healthAlpha = 0.0f;
  float vehicleAlpha = 0.0f;

  // Цілі, які пише машина станів.
  float targetX = kBottomLeftHiddenX;
  float targetHealthAlpha = 0.0f;
  float targetVehicleAlpha = 0.0f;

  // Пригашені варіанти — їх рахує та сама функція наприкінці.
  float healthFadedAlpha = 0.0f;
  float vehicleFadedAlpha = 0.0f;

  // Один крок. `menuBackgroundAlpha` — прозорість плашок із профілю
  // гравця; `dt` у секундах.
  void update(BottomLeftMode mode, float menuBackgroundAlpha, float dt);
};

}  // namespace obf2::hud
