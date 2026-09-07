#include "obf2/hud/bottom_left.h"

#include <algorithm>
#include <cmath>

namespace obf2::hud {
namespace {

// Обидві дії графа — це `approachVariable` (obf2/hud/animation.h, знята
// з `MemeDll.dll` 0x10001050 і 0x10004d2c). У `Menu/Ingame` гальмівна
// ділянка нульова, тож підхід рівномірний.
void approach(float& value, float target, float speed, float dt) {
  approachVariable(value, target, speed, 0.0f, dt);
}

}  // namespace

void BottomLeftPanel::update(BottomLeftMode mode, float menuBackgroundAlpha, float dt) {
  // Порядок такий самий, як у 0x78b600: спершу цілі за поточним станом,
  // потім рух до них, і аж наприкінці пригашені прозорості.
  switch (mode) {
    case BottomLeftMode::Hidden:
      // Ховаємось лише коли смуги техніки вже згасли; доти ділянка
      // стоїть на положенні «пішки».
      if (vehicleAlpha == 0.0f) {
        targetX = kBottomLeftFootX;
        // В оригіналі тут `XPos <= пішки`, а не `<`: інакше ділянка, що
        // вже доїхала, ніколи не почала б гаснути. Порядок саме такий —
        // спершу згасити смуги, і лише коли вони згасли, їхати за край.
        if (x <= kBottomLeftFootX) {
          if (healthAlpha == 0.0f) targetX = kBottomLeftHiddenX;
          else targetHealthAlpha = 0.0f;
        }
      }
      break;

    case BottomLeftMode::Health:
      if (x < kBottomLeftFootX) {
        targetX = kBottomLeftFootX;
      } else {
        targetHealthAlpha = 1.0f;
        if (vehicleAlpha == 0.0f) targetX = kBottomLeftFootX;
        else targetVehicleAlpha = 0.0f;
      }
      break;

    case BottomLeftMode::Vehicle:
      // `пішки <= XPos` в оригіналі, тобто доїхали до положення пішки.
      if (x < kBottomLeftFootX) {
        targetX = kBottomLeftFootX;
      } else {
        targetHealthAlpha = 1.0f;
        if (healthAlpha == 1.0f) {
          // Смуги здоров'я вже проступили — тепер ділянка їде далі, під
          // смуги техніки, і аж там вони проступають.
          if (x < kBottomLeftVehicleX) targetX = kBottomLeftVehicleX;
          else targetVehicleAlpha = 1.0f;
        }
      }
      break;
  }

  approach(x, targetX, kCornerMoveSpeed, dt);
  approach(healthAlpha, targetHealthAlpha, kCornerAlphaSpeed, dt);
  approach(vehicleAlpha, targetVehicleAlpha, kCornerAlphaSpeed, dt);

  // `Faded = clamp(Alpha - (1 - основа), 0, 1)`, де основа — прозорість
  // плашок із профілю гравця. Дослівно з кінця 0x78b600.
  const float lost = 1.0f - menuBackgroundAlpha;
  healthFadedAlpha = std::clamp(healthAlpha - lost, 0.0f, 1.0f);
  vehicleFadedAlpha = std::clamp(vehicleAlpha - lost, 0.0f, 1.0f);
}

}  // namespace obf2::hud
