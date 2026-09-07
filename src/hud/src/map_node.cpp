#include "obf2/hud/map_node.h"

#include <cmath>

namespace obf2::hud {
namespace {

// Згладжування рушія: `FUN_00402ee0(dt * -6.0, -10, 10)` затискає
// показник, далі йде e^x (ROUND/f2xm1/fscale), і виходить
//   значення += (1 - e^(-6*dt)) * (ціль - значення)
// (`BF2.exe`, 0x77d1a4..0x77d1c1 — той самий блок повторено для кожного
// поля, що анімується).
void approach(float& value, float target, float dt, float snapDistance, bool snapNow) {
  if (snapNow || std::fabs(value - target) < snapDistance) {
    value = target;
    return;
  }
  float exponent = dt * -kMapApproachRate;
  if (exponent < -10.0f) exponent = -10.0f;
  if (exponent > 10.0f) exponent = 10.0f;
  value += (1.0f - std::exp(exponent)) * (target - value);
}

}  // namespace

void MapNode::takeRects(const Node& node) {
  const auto take = [](const MapRect& rect, MapPoint& position, MapPoint& size) {
    if (!rect.set) return;
    position = MapPoint{rect.x, rect.y};
    size = MapPoint{rect.width, rect.height};
  };
  take(node.mapMini, mini_, miniSize_);
  take(node.mapMaxi, maxi_, maxiSize_);
  take(node.mapCommander, commander_, commanderSize_);
  applyState(state_);
  snap();
}

void MapNode::applyState(int state) {
  state_ = state;
  switch (state) {
    case kMapStateIngame:
      // 0x777e3d: ціль — мініатюра в кутку.
      targetPosition_ = mini_;
      targetSize_ = miniSize_;
      commanderMode_ = false;
      break;

    case kMapStateSpawn:
    case kMapStateBigMap:
      // 0x777f8a і 0x778089: обидва стани беруть один і той самий
      // великий прямокутник. Різняться вони лише прозорістю: на екрані
      // появи вона стала (1.0 і 0.5 для значків), а на великій карті
      // береться з налаштувань `MenuMapAlpha`/`MenuMapIconAlpha`.
      // Прозорість ми поки не ведемо — див. «не виміряно» нижче.
      targetPosition_ = maxi_;
      targetSize_ = maxiSize_;
      commanderMode_ = false;
      break;

    case kMapStateSquadLeaderMenu:
    case kMapStateCommanderMenu:
      // 0x778299 і 0x778281. В оригіналі ці два стани ще й дивляться на
      // попередній стан (0x1e4): якщо ми вже прийшли з відповідного
      // меню, ціль не чіпають.
      targetPosition_ = maxi_;
      targetSize_ = maxiSize_;
      commanderMode_ = false;
      break;

    case kMapStateCommander:
      // 0x7781b0: лише якщо режим командира ще не ввімкнено.
      if (!commanderMode_) {
        commanderMode_ = true;
        targetPosition_ = commander_;
        targetSize_ = commanderSize_;
      }
      break;

    default:
      // Решта станів ціль карти не змінює (0x777e11, гілка
      // 9/0xc/0xd/0xe/0x10/0x13/0x14/0x15/0x1a/0x1b/0x1c) — зокрема й
      // стан 19 `MapMenuShow`: швидке меню масштабу лягає **поверх**
      // карти, а саму карту не рухає.
      break;
  }
}

void MapNode::snap() { snapNext_ = true; }

void MapNode::update(float dt) {
  const bool snapNow = snapNext_;
  snapNext_ = false;

  // Порядок і зсув пів екрана — з 0x77d13c..0x77d2b0: до цілі, яку
  // задав стан, додається (400, 300), бо координати карти в даних
  // відлічені від центра екрана.
  approach(position_.y, targetPosition_.y + kReferenceHeight * 0.5f, dt, kMapSnapDistance,
           snapNow);
  approach(position_.x, targetPosition_.x + kReferenceWidth * 0.5f, dt, kMapSnapDistance, snapNow);
  approach(size_.y, targetSize_.y, dt, kMapSnapDistance, snapNow);
  approach(size_.x, targetSize_.x, dt, kMapSnapDistance, snapNow);

  // 0x77d3a0: обидві змінні показу **виводяться з поточного розміру**, а
  // не з номера стану. Доки розмір у дорозі, не ввімкнена жодна — і саме
  // тому в оригіналі рамка мінікарти встигає згаснути, перш ніж
  // проступить рамка великої.
  if (state_ == kMapStateCommander) {
    if (size_.x <= commanderSize_.x - kMapSizeTolerance ||
        size_.y <= commanderSize_.y - kMapSizeTolerance) {
      settled_ = false;
      fullSize_ = false;
      minSize_ = false;
    } else {
      settled_ = true;
      fullSize_ = true;
      minSize_ = false;
    }
    return;
  }

  if (size_.x <= maxiSize_.x - kMapSizeTolerance ||
      size_.y <= maxiSize_.y - kMapSizeTolerance) {
    if (miniSize_.x + kMapSizeTolerance <= size_.x ||
        miniSize_.y + kMapSizeTolerance <= size_.y) {
      settled_ = false;
      fullSize_ = false;
      minSize_ = false;
    } else {
      settled_ = true;
      fullSize_ = false;
      minSize_ = true;
    }
  } else {
    settled_ = true;
    fullSize_ = true;
    minSize_ = false;
  }
}

}  // namespace obf2::hud
