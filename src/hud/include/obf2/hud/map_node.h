#pragma once
// Карта: куди вона їде, як росте й звідки беруться `MapFullSize` та
// `MapMinSize`.
//
// Це не прапорець «показати велику карту». У клієнті карта — окремий
// вузол зі своєю парою «ціль / поточне», і **розмір веде анімація**, а
// вже з розміру щокадру виводяться обидві змінні показу. Саме тому в
// оригіналі перехід між мінікартою й великою плавний, а рамки
// (`MinSizeAlpha`, `MapFrameOpen`) перемикаються не миттєво, а коли
// розмір доїхав до свого кінця.
//
// Джерела:
//   `BF2.exe`, 0x777dc0 — стан HUD -> ціль (позиція й розмір);
//   `BF2.exe`, 0x77c330 — крок анімації й виведення прапорців.
//
// Поля вузла карти (`BF2.exe`, зсуви від об'єкта карти):
//
//   +0x68c  `MapFullSize`   виводиться з розміру, 0x77d3f8
//   +0x68d  `MapMinSize`    там-таки
//   +0x68e  режим командира (він же індекс у таблиці масштабів)
//   +0x690  режим екрана появи      ставить 0x777f9a
//   +0x691  режим командира         ставить 0x7781b0
//   +0x692  режим меню (0x11/0x12)  ставить 0x778299
//   +0x694  1 - висота цілі / висота екрана (у командира — навпаки)
//   +0x698  поточний масштаб, +0x6d0 — його номер
//   +0x778  ціль: ширина            +0x77c  ціль: висота
//   +0x780  ціль: X                 +0x784  ціль: Y
//   +0x788  `setMiniPos`   (0x75f319)   +0x798  `setMiniSize`
//   +0x7a8  `setCommanderPos`           +0x7b0  `setCommanderSize`
//   +0x7b8  `setMaxiSize`  (0x75f423)   +0x7c0  `setMaxiPos` (0x75f555)
//   +0x776  чи розмір доїхав (минулого кадру)
//   +0x7f8  «стати одразу», без згладжування
//
// Поточні позиція й розмір лежать не в самому вузлі, а в спільних
// комірках (`FUN_0065ddd0` — позиція, `FUN_00659990` — розмір); до
// позиції додано пів екрана 800x600, як і в `useMapView`.
#include "obf2/hud/hud.h"

namespace obf2::hud {

// Швидкість підходу до цілі — 6.0 (`BF2.exe`, 0x77d1c1 і сусідні:
// `FUN_00402ee0(dt * -6.0, ...)`, далі e^x). Крок такий:
//   значення += (1 - e^(-6*dt)) * (ціль - значення)
inline constexpr float kMapApproachRate = 6.0f;

// Ближче за це — просто стаємо на ціль (0x77d18f для позиції й розміру).
inline constexpr float kMapSnapDistance = 0.1f;

// Допуск, за яким розмір вважається «доїхав» (0x77d3d1: `- 3.0`).
inline constexpr float kMapSizeTolerance = 3.0f;

// Номери станів HUD, які карта розрізняє (`BF2.exe`, 0x777e11 — switch
// по номеру стану; таблиця станів — docs/functions/hud-states.md).
inline constexpr int kMapStateIngame = 0;      // мінікарта в кутку
inline constexpr int kMapStateSpawn = 1;       // екран появи
inline constexpr int kMapStateBigMap = 2;      // велика карта (клавіша M)
inline constexpr int kMapStateCommander = 15;  // 0xf
inline constexpr int kMapStateSquadLeaderMenu = 17;  // 0x11
inline constexpr int kMapStateCommanderMenu = 18;    // 0x12

struct MapPoint {
  float x = 0.0f;
  float y = 0.0f;
};

class MapNode {
 public:
  // Три прямокутники з даних (`HudElementsMap.con`: setMiniPos/Size,
  // setMaxiPos/Size, setCommanderPos/Size). Беремо їх із зібраного
  // дерева, щоб не дублювати розбір.
  void takeRects(const Node& node);

  // Стан HUD змінився. Дослівно 0x777dc0: карта міняє ціль лише на
  // кількох станах, на решті не чіпає нічого.
  void applyState(int state);

  // Крок анімації, `dt` у секундах (0x77c330).
  void update(float dt);

  // Стати на ціль одразу — поле +0x7f8. Вживається, коли карту щойно
  // створили: інакше вона б виїжджала з нуля.
  void snap();

  MapPoint position() const { return position_; }
  MapPoint size() const { return size_; }

  // Виведені з розміру змінні показу (0x77d3f8).
  bool fullSize() const { return fullSize_; }
  bool minSize() const { return minSize_; }
  // +0x776: розмір стоїть на одному зі своїх кінців, а не в дорозі.
  bool settled() const { return settled_; }

 private:
  MapPoint mini_{197.0f, -300.0f};
  MapPoint miniSize_{197.0f, 197.0f};
  MapPoint maxi_{-122.0f, -273.0f};
  MapPoint maxiSize_{512.0f, 512.0f};
  MapPoint commander_{-161.0f, -281.0f};
  MapPoint commanderSize_{561.0f, 561.0f};

  MapPoint targetPosition_{197.0f, -300.0f};
  MapPoint targetSize_{197.0f, 197.0f};

  // Поточне — вже в координатах екрана (ціль + пів екрана), як у
  // `useMapView`.
  MapPoint position_{kReferenceWidth * 0.5f + 197.0f, kReferenceHeight * 0.5f - 300.0f};
  MapPoint size_{197.0f, 197.0f};

  bool commanderMode_ = false;
  bool snapNext_ = true;
  bool fullSize_ = false;
  bool minSize_ = true;
  bool settled_ = true;
  int state_ = kMapStateIngame;
};

}  // namespace obf2::hud
