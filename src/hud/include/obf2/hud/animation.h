#pragma once
// Поява і зникнення вузлів HUD — те, що робить його живим.
//
// У грі це не окремий код, а той самий граф MemeFile, що й меню. Видно
// це прямо з будівника (`Menu/Bf2HudBuilder.cpp`, за 0x79b2c0 і 0x79db80):
// кожна команда ефекту шукає вже готовий вузол `<ім'я>CullNode` і чіпляє
// до нього новий вузол графа з іменем `<ім'я>MoveEffect` або
// `<ім'я>AlphaShowEffect`. Клас вузла руху зветься
// `dice::meme::Bf2MoveEffect` (RTTI за 0x937a44), поряд із ним лежить
// окремий `dice::meme::Bf2SinMoveEffect` — тобто звичайний рух саме
// рівномірний, а синусоїда це інший, окремий клас.
//
// Тому тут: cull-вузол дає нам «показувати чи ні», а ефект розтягує цей
// перехід у часі — за `setNodeInTime` секунд туди і `setNodeOutTime`
// назад.
//
// Напрям руху перевірений даними, а не вгаданий: панель голосування за
// карту (`HudElementsLevelsList.con`) стоїть на y = 377..383 і має
// `addNodeMoveShowEffect -1.57 376`. Кут -pi/2 при екранній осі y вниз
// має дати старт **під** екраном (y ~ 753), а не над ним, отже зсув —
// це (cos a, -sin a) * distance. Смуга здоров'я ліворуч має 3.14 53
// (приїжджає зліва), витривалість праворуч — 0 53 (справа), що з тим
// самим правилом сходиться.
#include <string>
#include <unordered_map>

#include "obf2/hud/hud.h"

namespace obf2::hud {

// Стан переходу одного вузла.
struct ShowState {
  float progress = 1.0f;  // 0 — сховано, 1 — на місці
  float alpha = 1.0f;     // множник прозорості від alpha-ефекту
  float offsetX = 0.0f;   // зсув від move-ефекту, у базових 800x600
  float offsetY = 0.0f;
};

class Animator {
 public:
  // Просунути час. `visible` — це відповідь cull-вузла для кожного вузла.
  void advance(float dt);

  // Ціль для вузла на цьому кадрі. Викликати до `advance` не обов'язково:
  // вузол, про який ми ще не чули, з'являється відразу в кінцевому стані,
  // якщо він видимий, і в нульовому, якщо ні.
  void setVisible(const Node& node, bool visible);

  ShowState state(const Node& node) const;

  // Чи є вузол, що зараз посеред переходу. Поки так — екран доводиться
  // перебудовувати щокадру.
  bool animating() const { return animating_; }

 private:
  struct Entry {
    float progress = 0.0f;
    bool visible = false;
    float inTime = 0.0f;
    float outTime = 0.0f;
  };
  std::unordered_map<std::string, Entry> entries_;
  bool animating_ = false;
};

}  // namespace obf2::hud
