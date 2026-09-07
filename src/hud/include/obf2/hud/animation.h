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
// Напрям руху взято з коду, а не з міркувань:
// `dice::meme::MoveEffect::picturePaint` (`MemeDll.dll`, 0x10001b27)
// рахує зсув як **(-cos a, +sin a) * довжина * (1 - хід)**.
//
// Раніше тут стояло `(+cos a, -sin a)`, виведене з міркування «панель
// голосування має виїжджати знизу». Знаки виявилися протилежними, тобто
// всі елементи прилітали не з того боку.
//
// Хід показу веде `dice::meme::CullNode::iterateUpdate` (`MemeDll.dll`,
// 0x10004a57), і він **рівномірний**: `хід += dt / «In time»` при показі
// і `хід -= dt / «Out time»` при сховуванні. Поля `In time`/`Out time`
// належать самому `CullNode` — саме їх пишуть `setNodeInTime` і
// `setNodeOutTime`.
#include <string>
#include <unordered_map>

#include "obf2/hud/hud.h"

namespace obf2::hud {

// --- кутові ділянки HUD ------------------------------------------------
//
// Це окрема від вузлів річ: ділянки їздять не за `setNodeInTime`, а за
// змінними графа `Menu/Ingame`. Прочитати їх можна командою
// `tools/meme_read.py Ingame --find BottomRight`:
//
//   SetVariableSineAction {Speed: 600}
//     Variable: FloatData «BottomRight/BottomRight_XPos»    503
//     Data:     ToggleData «BottomRight/BottomRight_NextPos»
//                 Data 1: «BottomRight_newXPos»             503
//                 Data 2: «BottomRight_oldXPos»             201
//
// Сховане положення — 503, і воно з файлу.
//
// **Висунуте — 336.5, і воно з виміру, а не з файлу.** 201 у файлі — це
// початкове значення змінної, яку гра переписує під час роботи (так само,
// як ліворуч переписує `BottomLeft_nextXPos`). Знімок кадру оригіналу
// (`Ctrl+Shift+D`, docs/research/03-frame-dump.md) дає 336.5, і на ньому
// сходяться три різні вузли:
//
//   BottomRightBar  301 -> 637.5     ShotSelect 449 -> 785.5
//   безіменний 16x10 431 -> 767.5
//
// Значення стале в усіх трьох знятих кадрах, тобто це не проміжок
// анімації. З 201 плашка набоїв сидить на 135 пікселів лівіше, ніж в
// оригіналі — це вже перевірялося.
inline constexpr float kBottomRightHiddenX = 503.0f;
inline constexpr float kBottomRightShownX = 336.5f;

// **Ліворуч положень три, а не два.** Їх задає конструктор об'єкта HUD
// (`BF2.exe`, 0x78c560), полями +0x178, +0x17c, +0x180:
//
//   0xc3938000 = -295   сховане
//   0xc3090000 = -137   пішки
//   0x42580000 =   54   у техніці
//
// Поточне й цільове положення — сусідні поля +0x184 (`BottomLeft_XPos`)
// і +0x188 (`BottomLeft_nextXPos`), зареєстровані як змінні графа
// (0x7895f5 і 0x78963f). Вибирає між трьома функція 0x78b600.
//
// Раніше тут стояв заповнювач -1, і саме через нього плашка під
// здоров'ям тягнулася на всю ширину, ніби гравець у техніці: вузол
// `BottomLeftBar` (400 завширшки, зсув -103) при -1 доходив до x = 296,
// тоді як пішки має доходити до 160.
inline constexpr float kBottomLeftHiddenX = -295.0f;
inline constexpr float kBottomLeftFootX = -137.0f;
inline constexpr float kBottomLeftVehicleX = 54.0f;

// Швидкість руху ділянок — з того самого файлу (`SetVariableSineAction`).
// Саму криву ми ще не реверсили: клас зветься Sine, тобто хід, найпевніше,
// згладжений, а ми поки їдемо рівно на цій швидкості.
inline constexpr float kCornerMoveSpeed = 600.0f;

// Прозорість тих самих ділянок веде **інша** дія — `SetVariableSoftAction`
// зі швидкістю 10 (чотири штуки: BottomLeft_alpha1/2, BottomRight_alpha).
// Ми її поки не відтворюємо: формула «Soft» не знайдена.
inline constexpr float kCornerAlphaSpeed = 10.0f;

// Стан переходу одного вузла.
struct ShowState {
  // Чи аніматор узагалі чув про цей вузол. Ні — значить, за нього
  // відповідає звичайна умова показу, а не хід переходу.
  bool known = false;
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
