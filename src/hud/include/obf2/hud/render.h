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

struct Screen {
  int width = 1280;
  int height = 720;
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
};

// Як розв'язати те, що вузол не тримає в собі:
//   * підпис за ключем локалізації;
//   * значення змінної інтерфейсу (`setNodeShowVariable` тощо).
struct Context {
  std::function<std::string_view(std::string_view key)> localize;
  std::function<bool(std::string_view variable)> isVisible;
  std::function<std::string_view(std::string_view variable)> variableText;
  // Заповнення смуги 0..1 (`setBarNodeValueVariable`).
  std::function<float(std::string_view variable)> variableValue;
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
