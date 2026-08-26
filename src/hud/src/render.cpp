#include "obf2/hud/render.h"

#include "obf2/core/math.h"

namespace obf2::hud {
namespace {

// Нормаль уздовж джерела світла: накладний пайплайн її не використовує,
// але вершина спільна для всіх мешів, тож поле має бути заповнене.
mesh::Vec3 flatNormal() { return mesh::Vec3{0.0f, 1.0f, 0.0f}; }

// Прямокутник у NDC із кольором, який іде через альфу текстури.
mesh::RenderMesh quad(const ScreenRect& rect, const Screen& screen, const std::string& texture) {
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
      mesh::Vertex{{x0, y0, 0.0f}, normal, {0.0f, 0.0f}},
      mesh::Vertex{{x1, y0, 0.0f}, normal, {1.0f, 0.0f}},
      mesh::Vertex{{x0, y1, 0.0f}, normal, {0.0f, 1.0f}},
      mesh::Vertex{{x1, y1, 0.0f}, normal, {1.0f, 1.0f}},
  };
  out.indices = {0, 2, 1, 1, 2, 3};

  mesh::DrawRange range;
  range.indexCount = 6;
  if (!texture.empty()) range.maps.push_back(texture);
  out.ranges.push_back(std::move(range));
  return out;
}

}  // namespace

mesh::RenderMesh buildRect(const ScreenRect& rect, const Screen& screen,
                           const std::string& texture) {
  return quad(rect, screen, texture);
}

ScreenRect nodeRect(const Node& node, const Screen& screen) {
  // Розтягуємо базові 800x600 на весь екран. Пропорції при цьому пливуть,
  // як і в оригіналі на широких моніторах.
  const float scaleX = static_cast<float>(screen.width) / kReferenceWidth;
  const float scaleY = static_cast<float>(screen.height) / kReferenceHeight;
  return ScreenRect{node.x * scaleX, node.y * scaleY, node.width * scaleX, node.height * scaleY};
}

std::vector<DrawPiece> buildNode(const Node& node, const font::Font& font,
                                 const std::string& fontAtlas, const Screen& screen,
                                 const Context& context) {
  std::vector<DrawPiece> pieces;
  if (node.width <= 0.0f || node.height <= 0.0f) return pieces;

  const float scaleY = static_cast<float>(screen.height) / kReferenceHeight;
  const ScreenRect rect = nodeRect(node, screen);

  // Картинки, кнопки й смуги — прямокутник із текстурою.
  if (node.type == NodeType::Picture || node.type == NodeType::Button ||
      node.type == NodeType::Bar) {
    std::string texture = node.texture;
    if (texture.empty() && !node.textureVariable.empty() && context.variableText) {
      texture = std::string(context.variableText(node.textureVariable));
    }
    if (!texture.empty()) {
      pieces.push_back(DrawPiece{quad(rect, screen, texture), texture, &node});
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

    font::TextLayout layout;
    layout.screenWidth = screen.width;
    layout.screenHeight = screen.height;
    layout.x = rect.x;
    layout.y = rect.y;
    // Кегль підганяємо під висоту вузла: у даних вона і є розміром рядка.
    layout.scale = font.size > 0.0f ? (node.height * scaleY) / font.size : 1.0f;

    auto geometry = font::buildText(font, text, layout, fontAtlas);
    if (!geometry.indices.empty()) {
      pieces.push_back(DrawPiece{std::move(geometry), fontAtlas, &node});
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
    if (!node->showVariable.empty() && context.isVisible && !context.isVisible(node->showVariable)) {
      continue;
    }
    for (auto& piece : buildNode(*node, font, fontAtlas, screen, context)) {
      pieces.push_back(std::move(piece));
    }
  }
  return pieces;
}

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
      if (!node->showVariable.empty() && context.isVisible &&
          !context.isVisible(node->showVariable)) {
        continue;
      }
      if (node->type == NodeType::Split) {
        // Вузол-«розгалуження» сам нічого не малює: він підставляє групу,
        // назва якої збігається з його іменем.
        self(self, node->name, depth + 1);
        continue;
      }
      for (auto& piece : buildNode(*node, font, fontAtlas, screen, context)) {
        pieces.push_back(std::move(piece));
      }
    }
  };
  walk(walk, rootGroup, 0);
  return pieces;
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
