#pragma once
// Побудова геометрії інтерфейсу з дерева вузлів.
//
// Вузли описані в координатах 800x600 — це видно з самих даних:
// `hudManager.setCommPos 150 150` разом із `setCommSize 490 300` дає рівно
// 640x450, а `setCommMousePos 400 300` — центр екрана 800x600. Тека шрифтів
// теж зветься `800/`.
//
// Результат — звичайні RenderMesh у координатах NDC, які малює той самий
// накладний пайплайн, що й текст.
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/font/text.h"
#include "obf2/hud/animation.h"
#include "obf2/hud/hud.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::hud {

// До чого притулений шар, коли екран ширший за 4:3.
enum class Anchor {
  Center,  // базовий прямокутник посередині
  Left,
  Right,
};

struct Screen {
  int width = 1280;
  int height = 720;
  // Масштаб однаковий по обох осях: інакше кругле стає овальним — це
  // добре видно на рамці мінікарти, вона рівно 192x192. Вільне місце по
  // боках роздається за прив'язкою шару; саме для цього в грі й існують
  // окремі кутові шари.
  Anchor anchor = Anchor::Center;
  // Зсув шару в базових 800x600. Кутові шари HUD описані від власного
  // якоря, а не від краю екрана, тож без нього вони лягають у лівий
  // верхній кут (див. docs/formats/hud-meme.md).
  float originX = 0.0f;
  float originY = 0.0f;
};

// Один готовий до малювання шматок інтерфейсу.
struct DrawPiece {
  mesh::RenderMesh geometry;
  std::string texture;  // порожньо для тексту — там атлас шрифту
  const Node* node = nullptr;
  // Відтінок саме цього шматка. Здебільшого це колір вузла, але список
  // малює своє тло власним кольором (`setListNodeBackgroundColor`), тож
  // одного кольору на вузол не досить.
  Color tint;
  // Порожній шматок-мітка: вузол малює хтось інший, щокадру (див.
  // `Context::skipNode`). Місце в списку лишається за ним, інакше живий
  // вузол ліг би поверх усього — карта поверх власної рамки.
  bool live = false;
};

// Як розв'язати те, що вузол не тримає в собі:
//   * підпис за ключем локалізації;
//   * значення змінної інтерфейсу (`setNodeShowVariable` тощо).
// Шрифт, яким малювати вузол. `setTextNodeStyle` у даних гри — це шлях
// до `.dif`, а не абстрактний стиль, і кегль зашитий у самій назві:
// hudFontLocalBold_9, StandardTextBold_15, vehicleHudFont_6.
struct FontRef {
  const font::Font* font = nullptr;
  std::string atlas;
};

struct Context {
  std::function<std::string_view(std::string_view key)> localize;
  // Шрифт вузла за його стилем. Порожній результат — лишаємо загальний.
  std::function<FontRef(std::string_view style)> fontFor;
  std::function<bool(std::string_view variable)> isVisible;
  // Хід появи/зникання вузла (див. obf2/hud/animation.h). Порожньо —
  // вузол малюється відразу в кінцевому стані.
  std::function<ShowState(const Node&)> showState;
  // Вузол, який хтось малює сам, щокадру. Його геометрію в спільний
  // набір не кладемо — інакше під живим вузлом лишався б його ж
  // відбиток із застарілим значенням (два компаси, два підписи).
  // Діти такого вузла будуються як звичайно.
  std::function<bool(const Node&)> skipNode;
  std::function<std::string_view(std::string_view variable)> variableText;
  // Заповнення смуги 0..1 (`setBarNodeValueVariable`).
  std::function<float(std::string_view variable)> variableValue;
  // Прозорість вузла (`setNodeAlphaVariable`). nullopt — про таку змінну
  // ми нічого не знаємо, і вузол лишається видимим: більшість із них —
  // це плавні згасання, і за замовчуванням вони ввімкнені.
  std::function<std::optional<float>(std::string_view variable)> variableAlpha;
  // Картинка карти рівня. Шлях до неї задає не HUD, а сам рівень —
  // у BF2.exe для цього є шаблон `Levels/%s/Hud/Minimap/ingameMap.tga`.
  std::string mapTexture;
  // Який шматок цієї картинки показувати: гра малює не всю карту рівня,
  // а квадрат навколо бойової зони. Знято з дампу кадру оригіналу —
  // див. docs/research/03-frame-dump.md. За замовчуванням уся картинка.
  float mapU0 = 0.0f, mapV0 = 0.0f, mapU1 = 1.0f, mapV1 = 1.0f;

  // Позначки на карті: точки захоплення. Вузол карти малює їх сам —
  // у даних для них немає окремих вузлів, лише шрифт і колір підпису
  // (`setCPFont`, `setCPFontColor` на самому вузлі карти).
  struct MapMarker {
    float worldX = 0.0f;
    float worldZ = 0.0f;
    std::string texture;
    std::string label;  // ключ локалізації
  };
  std::vector<MapMarker> mapMarkers;
  // Кружечки вибору місця появи. Текстури для них у грі окремі —
  // Minimap/Icons/spawn_UnSelected і spawn_Selected (плюс варіанти
  // Inactive і Squad). Малюються тим самим переведенням координат, що
  // й прапорці.
  struct SpawnMarker {
    float worldX = 0.0f;
    float worldZ = 0.0f;
    bool selected = false;
  };
  std::vector<SpawnMarker> spawnMarkers;
  // Розмір кружечка місця появи — **виміряний із бінара**: у функції
  // малювання значка (0x77f7c3 і 0x77f7ca) ширина й висота пишуться
  // сталою 0x41800000, тобто 16.0. Там же поруч і вибір текстури:
  // масив із восьми вказівників за 0x950..0x96c, база 0x960 для
  // вибраного і 0x950 для невибраного, а індекс дає ще один прапорець
  // (активна точка чи ні).
  float spawnMarkerSize = 16.0f;
  // Розмір світу рівня в метрах — ним переводимо координати позначок
  // у частки картинки.
  float mapWorldSize = 2048.0f;
  // Зсув підпису вниз від центра точки, у базових 800x600. **Виміряний**
  // зі знімка кадру оригіналу: партія підписів має 306 вершин, тобто 51
  // літера — рівно стільки, скільки в назвах чотирьох точок Dalian_plant
  // без пробілів, а її габарит 361.5, 278.2 розміром 246.0x123.8. Верхня
  // точка (Reactor Towers) стоїть на y = 270.7, отже верх підпису на
  // 7.5 нижче; за відрахуванням верхнього відступу шрифту це 5.5.
  float mapLabelOffset = 5.5f;

  // Розмір значка точки захоплення — **32x32**, і це вже виміряно, а не
  // взято з розміру текстури.
  //
  // Складає запис значка `BF2.exe`, 0x7755a0: ширину й висоту пише один
  // і той самий регістр за 0x7755ea (`MOV EDX, 0x42000000` = 32.0),
  // далі `[EAX+0x18]` і `[EAX+0x1c]`. Там-таки видно й решту запису:
  // колір 1,1,1 (0x7755bb..0x7755e1), прозорість із поля +0x6fc самого
  // вузла карти (0x7755b1) — тієї, що анімується разом із картою, — і
  // текстура з таблиці +0x904 (своя команда) чи +0x910 (нейтральна).
  //
  // Доти тут стояло 33 — розмір самої `miniMap_CP.tga`, позначений як
  // «не виміряно». Функція підготовки значків 0x74fb70 справді сталих не
  // містить: вона лише реєструє шляхи до текстур.
  //
  // Чого ще не знаємо: **у якому просторі** ці 32. Ми кладемо їх у
  // пікселях екрана, але запис значка може міряти й у просторі карти —
  // тоді на мінікарті значок був би меншим, ніж на великій. У знімку
  // бойового кадру всі значки зведені в одну партію (виклик 233,
  // 626 22 105x175), і поштучно з неї розміру не дістати.
  float mapMarkerSize = 32.0f;
};

// Геометрія одного вузла — картинка, смуга і/або підпис.
std::vector<DrawPiece> buildNode(const Node& node, const font::Font& font,
                                 const std::string& fontAtlas, const Screen& screen,
                                 const Context& context);

// Будує геометрію для однієї групи вузлів.
std::vector<DrawPiece> buildGroup(const Builder& builder, std::string_view group,
                                  const font::Font& font, const std::string& fontAtlas,
                                  const Screen& screen, const Context& context);

// Те саме, але з розкриттям вузлів типу `split`: у HUD гри вони не малюються
// самі, а підставляють цілу групу з такою ж назвою
// (`hudBuilder.createSplitNode GlobalHud IngameHud`). Так увесь інтерфейс і
// зібраний: Global -> GlobalHud -> IngameHud -> десятки під-груп.
std::vector<DrawPiece> buildTree(const Builder& builder, std::string_view rootGroup,
                                 const font::Font& font, const std::string& fontAtlas,
                                 const Screen& screen, const Context& context, int maxDepth = 8);

// Прямокутник вузла в пікселях екрана — потрібен для влучання мишею.
struct ScreenRect {
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
  bool contains(float px, float py) const {
    return px >= x && py >= y && px <= x + width && py <= y + height;
  }
};

ScreenRect nodeRect(const Node& node, const Screen& screen, const Context* context = nullptr);

// Габарити цілого піддерева в базових 800x600 — потрібні, щоб притулити
// кутовий шар до потрібного краю. Порожнє дерево дає nullopt.
struct Bounds {
  float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};
std::optional<Bounds> treeBounds(const Builder& builder, std::string_view rootGroup,
                                 const Context& context, int maxDepth = 8);

// Готовий прямокутник у координатах NDC — тим самим шляхом, що й текст.
// Потрібен для підсвітки кнопки під курсором: геометрію печемо наперед на
// кожну кнопку, тож у кадрі лишається сам малюнок.
mesh::RenderMesh buildRect(const ScreenRect& rect, const Screen& screen,
                           const std::string& texture);

// Повідомити аніматору, які вузли піддерева зараз мають бути видні.
// Обходимо все, зокрема й приховане: вузол, що зникає, теж має свій хід.
void updateAnimator(const Builder& builder, std::string_view rootGroup, Animator& animator,
                    const Context& context, int maxDepth = 8);

// Кружечок місця появи під курсором — номер у `context.spawnMarkers`.
// Карта не має для них окремих вузлів, тож і мишу ловить сама.
std::optional<std::size_t> spawnMarkerAt(const Builder& builder, std::string_view rootGroup,
                                         const Screen& screen, const Context& context,
                                         float mouseX, float mouseY, int maxDepth = 8);

// Кнопка під курсором, або nullptr. Шукаємо з кінця: пізніші вузли
// намальовані поверх, тому й ловлять мишу першими.
const Node* buttonAt(const Builder& builder, std::string_view group, const Screen& screen,
                     float mouseX, float mouseY, const Context* context = nullptr);

}  // namespace obf2::hud
