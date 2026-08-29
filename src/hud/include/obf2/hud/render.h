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

ScreenRect nodeRect(const Node& node, const Screen& screen);

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

// Кнопка під курсором, або nullptr. Шукаємо з кінця: пізніші вузли
// намальовані поверх, тому й ловлять мишу першими.
const Node* buttonAt(const Builder& builder, std::string_view group, const Screen& screen,
                     float mouseX, float mouseY);

}  // namespace obf2::hud
