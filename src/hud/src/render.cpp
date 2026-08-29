#include "obf2/hud/render.h"

#include "obf2/core/math.h"

#include <algorithm>
#include <cmath>

namespace obf2::hud {
namespace {

// Нормаль уздовж джерела світла: накладний пайплайн її не використовує,
// але вершина спільна для всіх мешів, тож поле має бути заповнене.
mesh::Vec3 flatNormal() { return mesh::Vec3{0.0f, 1.0f, 0.0f}; }

// Прямокутник у NDC із кольором, який іде через альфу текстури.
// `uMin`/`uMax` дають змогу показати лише частину картинки — так малюється
// заповнення смуги.
mesh::RenderMesh quad(const ScreenRect& rect, const Screen& screen, const std::string& texture,
                      float uMin = 0.0f, float uMax = 1.0f) {
  auto toNdcX = [&](float pixels) {
    return pixels / static_cast<float>(screen.width) * 2.0f - 1.0f;
  };
  auto toNdcY = [&](float pixels) {
    return 1.0f - pixels / static_cast<float>(screen.height) * 2.0f;
  };

  mesh::RenderMesh out;
  const mesh::Vec3 normal = flatNormal();
  const float x0 = toNdcX(rect.x);
  const float x1 = toNdcX(rect.x + rect.width);
  const float y0 = toNdcY(rect.y);
  const float y1 = toNdcY(rect.y + rect.height);

  out.vertices = {
      mesh::Vertex{{x0, y0, 0.0f}, normal, {uMin, 0.0f}},
      mesh::Vertex{{x1, y0, 0.0f}, normal, {uMax, 0.0f}},
      mesh::Vertex{{x0, y1, 0.0f}, normal, {uMin, 1.0f}},
      mesh::Vertex{{x1, y1, 0.0f}, normal, {uMax, 1.0f}},
  };
  out.indices = {0, 2, 1, 1, 2, 3};

  mesh::DrawRange range;
  range.indexCount = 6;
  if (!texture.empty()) range.maps.push_back(texture);
  out.ranges.push_back(std::move(range));
  return out;
}

}  // namespace

// Чи показувати вузол. Крім простої змінної (setNodeShowVariable) вузол
// може нести ланцюжок умов із setNodeLogicShowVariable — кожна порівнює
// змінну зі значенням, а дія каже, як приєднати результат:
//
//   EQUAL HudState 0        показувати, коли HudState дорівнює 0
//   NOT   DisconnectMessageActive 1   ... коли НЕ дорівнює
//   AND   ServerIsFavourite 1         ... і додатково
//   OR    PauseMessageActive 1        ... або
//
// Невідома змінна дає 0 — саме тому `EQUAL HudState 0` типово істинне,
// а `AND ServerIsFavourite 1` — ні.
bool nodeVisible(const Node& node, const Context& context) {
  bool result = true;
  bool have = false;
  if (!node.showVariable.empty() && context.isVisible) {
    result = context.isVisible(node.showVariable);
    have = true;
  }
  for (const ShowTest& test : node.showTests) {
    const float value = context.variableValue ? context.variableValue(test.variable) : 0.0f;
    bool term = std::abs(value - test.value) < 0.0001f;
    if (test.op == "NOT" || test.op == "not") term = !term;
    if (!have) {
      // Без setNodeShowVariable перша умова і задає відповідь: приєднувати
      // її нема до чого.
      result = term;
      have = true;
      continue;
    }
    if (test.op == "OR" || test.op == "or") {
      result = result || term;
    } else {
      result = result && term;
    }
  }
  return result;
}


mesh::RenderMesh buildRect(const ScreenRect& rect, const Screen& screen,
                           const std::string& texture) {
  return quad(rect, screen, texture);
}

ScreenRect nodeRect(const Node& node, const Screen& screen) {
  // Масштаб однаковий по обох осях — по висоті. Розтягування по ширині
  // робило б з круглого овальне.
  const float scale = static_cast<float>(screen.height) / kReferenceHeight;
  const float spare = static_cast<float>(screen.width) - kReferenceWidth * scale;
  const float padX = screen.anchor == Anchor::Center  ? spare * 0.5f
                     : screen.anchor == Anchor::Right ? spare
                                                      : 0.0f;
  // Беремо absX/absY, а не x/y: у грі координати вузла відлічуються від
  // батька, і без суми по предках усе розсипається по екрану. Зсув
  // setNodeOffset уже входить у цю суму (Builder::finish).
  return ScreenRect{(node.absX + screen.originX) * scale + padX,
                    (node.absY + screen.originY) * scale, node.width * scale,
                    node.height * scale};
}

std::vector<DrawPiece> buildNode(const Node& node, const font::Font& font,
                                 const std::string& fontAtlas, const Screen& screen,
                                 const Context& context) {
  std::vector<DrawPiece> pieces;
  if (node.width <= 0.0f || node.height <= 0.0f) return pieces;

  const float scaleY = static_cast<float>(screen.height) / kReferenceHeight;
  const ScreenRect rect = nodeRect(node, screen);

  // Смуга: показуємо не всю картинку, а її частину за значенням змінної.
  if (node.type == NodeType::Bar) {
    float value = 1.0f;
    if (!node.valueVariable.empty() && context.variableValue) {
      value = context.variableValue(node.valueVariable);
    }
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);

    const std::string& texture = node.barTextureFull.empty() ? node.texture : node.barTextureFull;
    if (!texture.empty() && value > 0.0f) {
      // Напрям 3 у даних означає смугу, що росте справа наліво (права
      // половина екрана — команда противника).
      ScreenRect part = rect;
      float uMin = 0.0f, uMax = value;
      if (node.barDirection == 3) {
        part.x = rect.x + rect.width * (1.0f - value);
        uMin = 1.0f - value;
        uMax = 1.0f;
      }
      part.width = rect.width * value;
      pieces.push_back(DrawPiece{quad(part, screen, texture, uMin, uMax), texture, &node, node.color});
    }
    return pieces;
  }

  // Список: тло і рамка — суцільні кольори, а не текстури. Малюємо
  // рамку на весь вузол, а тло — всередині її відступів. Рядки з'являться
  // тоді, коли буде звідки взяти гравців; сама плашка потрібна вже зараз,
  // бо без неї на табло замість списку діра.
  if (node.type == NodeType::List) {
    static const std::string kFill = "#ffffff";
    if (node.hasListBorder) {
      pieces.push_back(DrawPiece{quad(rect, screen, kFill), kFill, &node, node.listBorderColor});
    }
    if (node.hasListBackground) {
      ScreenRect inner = rect;
      inner.x += node.listBorder[0] * scaleY;
      inner.width -= (node.listBorder[0] + node.listBorder[1]) * scaleY;
      inner.y += node.listBorder[2] * scaleY;
      inner.height -= (node.listBorder[2] + node.listBorder[3]) * scaleY;
      if (inner.width > 0.0f && inner.height > 0.0f) {
        pieces.push_back(DrawPiece{quad(inner, screen, kFill), kFill, &node, node.listBackground});
      }
    }
    return pieces;
  }

  // Картинки й кнопки — прямокутник із текстурою.
  if (node.type == NodeType::Picture || node.type == NodeType::Button) {
    std::string texture = node.texture;
    if (texture.empty() && !node.textureVariable.empty() && context.variableText) {
      texture = std::string(context.variableText(node.textureVariable));
    }
    if (!texture.empty()) {
      pieces.push_back(DrawPiece{quad(rect, screen, texture), texture, &node, node.color});
    }
  }

  // Текст: спершу пряме значення, потім змінна, потім ключ локалізації.
  if (node.type == NodeType::Text || node.type == NodeType::Button) {
    std::string text = node.text;
    if (text.empty() && !node.textVariable.empty() && context.variableText) {
      text = std::string(context.variableText(node.textVariable));
    }
    if (text.empty()) return pieces;
    if (context.localize) text = std::string(context.localize(text));
    if (text.empty()) return pieces;

    // Кегль задає сам шрифт, а не висота вузла: `setTextNodeStyle` вказує
    // на конкретний `.dif`, і розмір стоїть у його назві
    // (hudFontLocalBold_9, StandardTextBold_15, vehicleHudFont_6). Доти ми
    // розтягували будь-який рядок під висоту його рамки — від того написи
    // на табло виходили вдвічі-втричі більші за оригінал.
    const font::Font* face = &font;
    std::string atlas = fontAtlas;
    if (!node.style.empty() && context.fontFor) {
      const FontRef chosen = context.fontFor(node.style);
      if (chosen.font != nullptr) {
        face = chosen.font;
        atlas = chosen.atlas;
      }
    }

    font::TextLayout layout;
    layout.screenWidth = screen.width;
    layout.screenHeight = screen.height;
    layout.x = rect.x;
    layout.y = rect.y;
    // Лишається тільки перерахунок з базових 800x600 у вікно.
    layout.scale = scaleY;

    auto geometry = font::buildText(*face, text, layout, atlas);
    if (!geometry.indices.empty()) {
      pieces.push_back(DrawPiece{std::move(geometry), atlas, &node, node.color});
    }
  }
  return pieces;
}

std::vector<DrawPiece> buildGroup(const Builder& builder, std::string_view group,
                                  const font::Font& font, const std::string& fontAtlas,
                                  const Screen& screen, const Context& context) {
  std::vector<DrawPiece> pieces;
  for (const Node* node : builder.group(group)) {
    // Вузол зі змінною показу малюємо лише тоді, коли вона ввімкнена.
    if (!nodeVisible(*node, context)) {
      continue;
    }
    for (auto& piece : buildNode(*node, font, fontAtlas, screen, context)) {
      pieces.push_back(std::move(piece));
    }
  }
  return pieces;
}

namespace {

// Вузол із прозорістю на змінній: поки та змінна нам невідома, вузол не
// малюємо. Так само ми чинимо з `setNodeShowVariable`, і так само чинить
// гра: `MenuBackgroundAlpha` — це тло **меню**, у бою воно нульове, і
// широкі плашки під смугами здоров'я та набоїв там просто не видно.
bool hiddenByAlpha(const Node& node, const Context& context) {
  if (node.alphaVariable.empty() || !context.variableAlpha) return false;
  const auto alpha = context.variableAlpha(node.alphaVariable);
  return alpha && *alpha <= 0.0f;
}

}  // namespace

std::vector<DrawPiece> buildTree(const Builder& builder, std::string_view rootGroup,
                                 const font::Font& font, const std::string& fontAtlas,
                                 const Screen& screen, const Context& context, int maxDepth) {
  std::vector<DrawPiece> pieces;
  std::vector<std::string> visited;

  // Обхід у глибину в порядку оголошення: пізніші вузли лягають зверху,
  // тож порядок обходу і є порядком малювання.
  const auto walk = [&](auto&& self, std::string_view group, int depth) -> void {
    if (depth > maxDepth) return;
    for (const std::string& seen : visited) {
      if (seen == group) return;  // захист від кільця у даних
    }
    visited.emplace_back(group);

    for (const Node* node : builder.group(group)) {
      if (!nodeVisible(*node, context)) continue;
      if (node->type == NodeType::Split) {
        // Вузол-«розгалуження» сам нічого не малює: він підставляє групу,
        // назва якої збігається з його іменем.
        self(self, node->name, depth + 1);
        continue;
      }
      if (hiddenByAlpha(*node, context)) continue;
      for (auto& piece : buildNode(*node, font, fontAtlas, screen, context)) {
        pieces.push_back(std::move(piece));
      }
      // Батьком може бути **будь-який** вузол, а не лише Split: у даних
      // гри дітей мають ще 30 вузлів-перетворень і дві картинки. Поки ми
      // спускалися тільки крізь Split, їхні піддерева не малювалися.
      self(self, node->name, depth + 1);
    }
  };
  walk(walk, rootGroup, 0);
  // `hudBuilder.newLayer` починає новий шар: усе, створене після нього,
  // лягає поверх попереднього незалежно від місця в дереві. Порядок
  // усередині шару — це порядок обходу, тож сортування має бути стійким.
  std::stable_sort(pieces.begin(), pieces.end(), [](const DrawPiece& a, const DrawPiece& b) {
    const int left = a.node != nullptr ? a.node->layer : 0;
    const int right = b.node != nullptr ? b.node->layer : 0;
    return left < right;
  });
  return pieces;
}

std::optional<Bounds> treeBounds(const Builder& builder, std::string_view rootGroup,
                                const Context& context, int maxDepth) {
  // Ходимо тим самим шляхом, що й buildTree: рахувати треба саме те, що
  // справді потрапить на екран, інакше вимкнені вузли тягли б габарити.
  std::optional<Bounds> out;
  std::vector<std::string> visited;
  const auto walk = [&](auto&& self, std::string_view group, int depth) -> void {
    if (depth > maxDepth) return;
    for (const std::string& seen : visited) {
      if (seen == group) return;
    }
    visited.emplace_back(group);
    for (const Node* node : builder.group(group)) {
      if (!nodeVisible(*node, context)) continue;
      if (node->type == NodeType::Split) {
        self(self, node->name, depth + 1);
        continue;
      }
      if (node->width <= 0.0f || node->height <= 0.0f) continue;
      if (hiddenByAlpha(*node, context)) continue;
      if (!out) {
        out = Bounds{node->x, node->y, node->x + node->width, node->y + node->height};
        continue;
      }
      out->minX = std::min(out->minX, node->x);
      out->minY = std::min(out->minY, node->y);
      out->maxX = std::max(out->maxX, node->x + node->width);
      out->maxY = std::max(out->maxY, node->y + node->height);
    }
  };
  walk(walk, rootGroup, 0);
  return out;
}

const Node* buttonAt(const Builder& builder, std::string_view group, const Screen& screen,
                     float mouseX, float mouseY) {
  const auto nodes = builder.group(group);
  // З кінця: пізніші вузли намальовані поверх, тож і мишу ловлять першими.
  for (std::size_t i = nodes.size(); i-- > 0;) {
    const Node* node = nodes[i];
    if (node->type != NodeType::Button || node->command.empty()) continue;

    ScreenRect rect = nodeRect(*node, screen);
    if (node->hasMouseArea) {
      Node area = *node;
      area.x = node->mouseX;
      area.y = node->mouseY;
      area.width = node->mouseWidth;
      area.height = node->mouseHeight;
      rect = nodeRect(area, screen);
    }
    if (rect.contains(mouseX, mouseY)) return node;
  }
  return nullptr;
}

}  // namespace obf2::hud
