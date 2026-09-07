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
                      float uMin = 0.0f, float uMax = 1.0f, float vMin = 0.0f, float vMax = 1.0f,
                      float angle = 0.0f) {
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
      mesh::Vertex{{x0, y0, 0.0f}, normal, {uMin, vMin}},
      mesh::Vertex{{x1, y0, 0.0f}, normal, {uMax, vMin}},
      mesh::Vertex{{x0, y1, 0.0f}, normal, {uMin, vMax}},
      mesh::Vertex{{x1, y1, 0.0f}, normal, {uMax, vMax}},
  };

  // Обертання навколо середини вузла — `setPictureNodeRotateVariable`.
  // Крутиться сама картинка, тож повертаємо кути в пікселях, а не в NDC:
  // інакше на неквадратному екрані круг став би овалом.
  //
  // Знак: на екрані Y росте вниз, тож додатний кут має крутити **проти**
  // годинникової стрілки. Інакше компас показував би сторону світу з
  // протилежного боку: при погляді на схід «E» опинялося б унизу, а не
  // вгорі.
  if (angle != 0.0f) {
    const float cx = rect.x + rect.width * 0.5f;
    const float cy = rect.y + rect.height * 0.5f;
    const float sin = std::sin(angle);
    const float cos = std::cos(angle);
    const float px[4] = {rect.x, rect.x + rect.width, rect.x, rect.x + rect.width};
    const float py[4] = {rect.y, rect.y, rect.y + rect.height, rect.y + rect.height};
    for (int i = 0; i < 4; ++i) {
      const float dx = px[i] - cx;
      const float dy = py[i] - cy;
      mesh::Vertex& vertex = out.vertices[static_cast<std::size_t>(i)];
      vertex.position.x = toNdcX(cx + dx * cos + dy * sin);
      vertex.position.y = toNdcY(cy - dx * sin + dy * cos);
    }
  }
  out.indices = {0, 2, 1, 1, 2, 3};

  mesh::DrawRange range;
  range.indexCount = 6;
  if (!texture.empty()) range.maps.push_back(texture);
  out.ranges.push_back(std::move(range));
  return out;
}

// Круг замість прямокутника — цим малюється мініатюра карти. Рамка
// map_Frame.tga кутів не закриває (там прозорість), тож обрізати
// картинку має сам вузол.
mesh::RenderMesh disc(const ScreenRect& rect, const Screen& screen, const std::string& texture,
                      float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
                      int segments = 48) {
  auto toNdcX = [&](float pixels) {
    return pixels / static_cast<float>(screen.width) * 2.0f - 1.0f;
  };
  auto toNdcY = [&](float pixels) {
    return 1.0f - pixels / static_cast<float>(screen.height) * 2.0f;
  };

  mesh::RenderMesh out;
  const mesh::Vec3 normal = flatNormal();
  const float cx = rect.x + rect.width * 0.5f;
  const float cy = rect.y + rect.height * 0.5f;
  const float rx = rect.width * 0.5f;
  const float ry = rect.height * 0.5f;

  const float uMid = (u0 + u1) * 0.5f;
  const float vMid = (v0 + v1) * 0.5f;
  const float uHalf = (u1 - u0) * 0.5f;
  const float vHalf = (v1 - v0) * 0.5f;
  out.vertices.push_back(mesh::Vertex{{toNdcX(cx), toNdcY(cy), 0.0f}, normal, {uMid, vMid}});
  for (int i = 0; i <= segments; ++i) {
    const float angle =
        2.0f * 3.14159265358979f * static_cast<float>(i) / static_cast<float>(segments);
    const float ox = std::cos(angle);
    const float oy = std::sin(angle);
    out.vertices.push_back(mesh::Vertex{{toNdcX(cx + ox * rx), toNdcY(cy + oy * ry), 0.0f},
                                        normal,
                                        {uMid + ox * uHalf, vMid + oy * vHalf}});
  }
  for (int i = 1; i <= segments; ++i) {
    out.indices.push_back(0);
    out.indices.push_back(static_cast<std::uint32_t>(i));
    out.indices.push_back(static_cast<std::uint32_t>(i + 1));
  }

  mesh::DrawRange range;
  range.indexCount = static_cast<std::uint32_t>(out.indices.size());
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

ShowState nodeShowState(const Node& node, const Context& context) {
  if (!context.showState) return ShowState{};
  return context.showState(node);
}

// Чи вузол на екрані. Умова показу задає **ціль**, аніматор — лише хід
// до неї: поки вузол їде або згасає, він ще видимий. Про вузол, якого
// аніматор не знає, відповідає сама умова.
bool nodeShown(const Node& node, const Context& context) {
  const ShowState show = nodeShowState(node, context);
  if (!show.known) return nodeVisible(node, context);
  return show.progress > 0.0f;
}

ScreenRect nodeRect(const Node& node, const Screen& screen, const Context* context) {
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
  // Зсув ефекту руху — теж у базових 800x600, тож масштабується так само.
  float shiftX = 0.0f, shiftY = 0.0f;
  if (context != nullptr && context->showState) {
    const ShowState show = context->showState(node);
    shiftX = show.offsetX;
    shiftY = show.offsetY;
  }
  return ScreenRect{(node.absX + shiftX + screen.originX) * scale + padX,
                    (node.absY + shiftY + screen.originY) * scale, node.width * scale,
                    node.height * scale};
}

namespace {

std::vector<DrawPiece> buildNodeGeometry(const Node& node, const font::Font& font,
                                         const std::string& fontAtlas, const Screen& screen,
                                         const Context& context) {
  std::vector<DrawPiece> pieces;
  if (node.width <= 0.0f || node.height <= 0.0f) return pieces;

  const float scaleY = static_cast<float>(screen.height) / kReferenceHeight;
  const ScreenRect rect = nodeRect(node, screen, &context);

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

  // Карта: власної текстури вузол не має — її дає рівень.
  if (node.type == NodeType::Map || node.type == NodeType::MiniMap) {
    if (!context.mapTexture.empty()) {
      // Мініатюра в бою кругла, а велика на екрані появи — квадратна.
      // Гра показує не всю картинку рівня, а квадрат навколо бойової
      // зони — межі приходять у контексті.
      auto geometry = node.mapView == MapView::Mini
                          ? disc(rect, screen, context.mapTexture, context.mapU0, context.mapV0,
                                 context.mapU1, context.mapV1)
                          : quad(rect, screen, context.mapTexture, context.mapU0, context.mapU1,
                                 context.mapV0, context.mapV1);
      pieces.push_back(DrawPiece{std::move(geometry), context.mapTexture, &node, node.color});
    }

    // Позначки точок захоплення. Окремих вузлів для них у даних немає —
    // карта малює їх сама, а шрифт і колір підпису бере зі свого вузла
    // (`setCPFont`, `setCPFontColor`).
    const float uSpan = context.mapU1 - context.mapU0;
    const float vSpan = context.mapV1 - context.mapV0;
    if (uSpan > 0.0f && vSpan > 0.0f) {
      const float half = context.mapWorldSize * 0.5f;
      const float icon = context.mapMarkerSize * scaleY;
      for (const Context::MapMarker& marker : context.mapMarkers) {
        const float u = (marker.worldX + half) / context.mapWorldSize;
        const float v = (half - marker.worldZ) / context.mapWorldSize;
        if (u < context.mapU0 || u > context.mapU1) continue;
        if (v < context.mapV0 || v > context.mapV1) continue;
        const float cx = rect.x + (u - context.mapU0) / uSpan * rect.width;
        const float cy = rect.y + (v - context.mapV0) / vSpan * rect.height;
        if (!marker.texture.empty()) {
          const ScreenRect box{cx - icon * 0.5f, cy - icon * 0.5f, icon, icon};
          pieces.push_back(DrawPiece{quad(box, screen, marker.texture), marker.texture, &node,
                                     Color{}});
        }
        if (marker.label.empty() || !context.localize) continue;
        const std::string text(context.localize(marker.label));
        if (text.empty()) continue;
        const font::Font* face = &font;
        std::string atlas = fontAtlas;
        if (!node.cpFont.empty() && context.fontFor) {
          const FontRef chosen = context.fontFor(node.cpFont);
          if (chosen.font != nullptr) {
            face = chosen.font;
            atlas = chosen.atlas;
          }
        }
        font::TextLayout layout;
        layout.screenWidth = screen.width;
        layout.screenHeight = screen.height;
        layout.scale = scaleY;
        // Підпис стоїть під значком і по центру нього.
        layout.x = cx - font::textWidth(*face, text, layout.scale) * 0.5f;
        // Зсув підпису — виміряний окремо від значка: у грі це просто
        // відступ від центра точки, і від розміру значка він не залежить.
        layout.y = cy + context.mapLabelOffset * scaleY;
        auto geometry = font::buildText(*face, text, layout, atlas);
        if (!geometry.indices.empty()) {
          pieces.push_back(DrawPiece{std::move(geometry), atlas, &node, node.cpFontColor});
        }
      }

      // Кружечки вибору місця появи — окремими текстурами.
      for (const Context::SpawnMarker& spawn : context.spawnMarkers) {
        const float u = (spawn.worldX + half) / context.mapWorldSize;
        const float v = (half - spawn.worldZ) / context.mapWorldSize;
        if (u < context.mapU0 || u > context.mapU1) continue;
        if (v < context.mapV0 || v > context.mapV1) continue;
        const float cx = rect.x + (u - context.mapU0) / uSpan * rect.width;
        const float cy = rect.y + (v - context.mapV0) / vSpan * rect.height;
        const float size = context.spawnMarkerSize * scaleY;
        const std::string texture =
            // Шляхи — рядки з бінара (0x930724 і 0x93065c). Розширення
            // там .tga, хоча в архіві лежить .dds; підміну робить наш
            // пошук текстури, як і для решти HUD.
            spawn.selected ? "Ingame/Minimap/Icons/spawn_Selected.tga"
                           : "Ingame/Minimap/Icons/spawn_UnSelected.tga";
        const ScreenRect box{cx - size * 0.5f, cy - size * 0.5f, size, size};
        pieces.push_back(DrawPiece{quad(box, screen, texture), texture, &node, Color{}});
      }
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
      // `setPictureNodeRotateVariable` — кут у радіанах. У даних його має
      // лише компас мінікарти (`MapCompass` -> `MinimapDelayedMapAngle`,
      // HudElementsMap.con), і веде його сама карта: поле +0x760 вузла
      // карти, зареєстроване за `BF2.exe`, 0x780a49.
      const float angle = node.rotateVariable.empty() || !context.variableValue
                              ? 0.0f
                              : context.variableValue(node.rotateVariable);
      pieces.push_back(
          DrawPiece{quad(rect, screen, texture, 0.0f, 1.0f, 0.0f, 1.0f, angle), texture, &node,
                    node.color});
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

    // Вирівнювання задає другий аргумент setTextNodeStyle. Обидва кінці
    // перевірені знімком кадру оригіналу: повідомлення посеред екрана
    // має 0 і стоїть по центру ((800-301.3)/2 = 249.35 при 249.5 у
    // дампі), а підпис класу має 2 і починається просто з краю рамки
    // (34 у даних проти 33.5 у дампі).
    const float textPixels = font::textWidth(*face, text, layout.scale);
    if (textPixels > 0.0f && rect.width > textPixels) {
      if (node.textAlign == 0) {
        layout.x = rect.x + (rect.width - textPixels) * 0.5f;
      } else if (node.textAlign == 1) {
        layout.x = rect.x + rect.width - textPixels;
      }
    }

    auto geometry = font::buildText(*face, text, layout, atlas);
    if (!geometry.indices.empty()) {
      pieces.push_back(DrawPiece{std::move(geometry), atlas, &node, node.color});
    }
  }
  return pieces;
}

}  // namespace

std::vector<DrawPiece> buildNode(const Node& node, const font::Font& font,
                                 const std::string& fontAtlas, const Screen& screen,
                                 const Context& context) {
  auto pieces = buildNodeGeometry(node, font, fontAtlas, screen, context);

  // `setNodeRGBVariables <червона> <зелена> <синя>` — колір вузла зі
  // змінних. У даних на цьому висять числа квитків
  // (`FriendlyTeamTopRed` і сусіди) та смуги точок захоплення.
  //
  // Правило те саме, що для прозорості: **не знаємо змінної — не чіпаємо
  // колір**. Інакше числа квитків стали б чорними, бо невідома змінна це
  // нуль. Хто пише ці змінні в грі — **джерело не знайдене**.
  if (node.rgbVariables.size() >= 3 && context.variableAlpha) {
    const auto channel = [&](std::size_t index) {
      return context.variableAlpha(node.rgbVariables[index]);
    };
    const auto red = channel(0);
    const auto green = channel(1);
    const auto blue = channel(2);
    if (red || green || blue) {
      for (DrawPiece& piece : pieces) {
        if (red) piece.tint.r = *red;
        if (green) piece.tint.g = *green;
        if (blue) piece.tint.b = *blue;
      }
    }
  }

  // Alpha-ефект множить прозорість усього, що вузол намалював.
  const float alpha = nodeShowState(node, context).alpha;
  if (alpha < 1.0f) {
    for (DrawPiece& piece : pieces) piece.tint.a *= alpha;
  }
  return pieces;
}

std::vector<DrawPiece> buildGroup(const Builder& builder, std::string_view group,
                                  const font::Font& font, const std::string& fontAtlas,
                                  const Screen& screen, const Context& context) {
  std::vector<DrawPiece> pieces;
  for (const Node* node : builder.group(group)) {
    // Вузол зі змінною показу малюємо лише тоді, коли вона ввімкнена.
    if (!nodeShown(*node, context)) continue;
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
      if (!nodeShown(*node, context)) continue;
      if (node->type == NodeType::Split) {
        // Вузол-«розгалуження» сам нічого не малює: він підставляє групу,
        // назва якої збігається з його іменем.
        self(self, node->name, depth + 1);
        continue;
      }
      if (hiddenByAlpha(*node, context)) continue;
      if (context.skipNode && context.skipNode(*node)) {
        // Живий вузол: геометрію дає той, хто веде значення, а тут
        // лишається мітка — щоб не з'їхав порядок малювання.
        DrawPiece marker;
        marker.node = node;
        marker.tint = node->color;
        marker.live = true;
        pieces.push_back(std::move(marker));
        self(self, node->name, depth + 1);
        continue;
      }
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

std::optional<std::size_t> spawnMarkerAt(const Builder& builder, std::string_view rootGroup,
                                         const Screen& screen, const Context& context,
                                         float mouseX, float mouseY, int maxDepth) {
  const float uSpan = context.mapU1 - context.mapU0;
  const float vSpan = context.mapV1 - context.mapV0;
  if (uSpan <= 0.0f || vSpan <= 0.0f) return std::nullopt;
  const float scaleY = static_cast<float>(screen.height) / kReferenceHeight;
  const float half = context.mapWorldSize * 0.5f;
  const float size = context.spawnMarkerSize * scaleY;

  std::optional<std::size_t> found;
  std::vector<std::string> visited;
  const auto walk = [&](auto&& self, std::string_view group, int depth) -> void {
    if (depth > maxDepth) return;
    for (const std::string& seen : visited) {
      if (seen == group) return;
    }
    visited.emplace_back(group);
    for (const Node* node : builder.group(group)) {
      if (!nodeShown(*node, context)) continue;
      if (node->type == NodeType::Map || node->type == NodeType::MiniMap) {
        const ScreenRect rect = nodeRect(*node, screen, &context);
        for (std::size_t i = 0; i < context.spawnMarkers.size(); ++i) {
          const Context::SpawnMarker& spawn = context.spawnMarkers[i];
          const float u = (spawn.worldX + half) / context.mapWorldSize;
          const float v = (half - spawn.worldZ) / context.mapWorldSize;
          if (u < context.mapU0 || u > context.mapU1) continue;
          if (v < context.mapV0 || v > context.mapV1) continue;
          const float cx = rect.x + (u - context.mapU0) / uSpan * rect.width;
          const float cy = rect.y + (v - context.mapV0) / vSpan * rect.height;
          const ScreenRect box{cx - size * 0.5f, cy - size * 0.5f, size, size};
          if (box.contains(mouseX, mouseY)) found = i;
        }
      }
      self(self, node->name, depth + 1);
    }
  };
  walk(walk, rootGroup, 0);
  return found;
}

void updateAnimator(const Builder& builder, std::string_view rootGroup, Animator& animator,
                    const Context& context, int maxDepth) {
  std::vector<std::string> visited;
  const auto walk = [&](auto&& self, std::string_view group, int depth) -> void {
    if (depth > maxDepth) return;
    for (const std::string& seen : visited) {
      if (seen == group) return;
    }
    visited.emplace_back(group);
    for (const Node* node : builder.group(group)) {
      animator.setVisible(*node, nodeVisible(*node, context) && !hiddenByAlpha(*node, context));
      self(self, node->name, depth + 1);
    }
  };
  walk(walk, rootGroup, 0);
}

const Node* buttonAt(const Builder& builder, std::string_view group, const Screen& screen,
                     float mouseX, float mouseY, const Context* context) {
  // Кнопки екрана лежать глибоко в дереві (SelectKit0 сидить під
  // Kit0NotSelected), тож обходимо його так само, як при малюванні, і
  // з тими самими умовами показу: невидима кнопка миші не ловить.
  const Node* found = nullptr;
  std::vector<std::string> visited;
  const auto walk = [&](auto&& self, std::string_view where, int depth) -> void {
    if (depth > 24) return;
    for (const std::string& seen : visited) {
      if (seen == where) return;
    }
    visited.emplace_back(where);
    for (const Node* node : builder.group(where)) {
      if (context != nullptr && !nodeVisible(*node, *context)) continue;
      if (node->type == NodeType::Button && !node->command.empty()) {
        ScreenRect rect = nodeRect(*node, screen, context);
        if (node->hasMouseArea) {
          // Ділянка миші задана зсувом від самого вузла, а не окремим
          // місцем на екрані.
          const float scale = static_cast<float>(screen.height) / kReferenceHeight;
          rect.x += node->mouseX * scale;
          rect.y += node->mouseY * scale;
          rect.width = node->mouseWidth * scale;
          rect.height = node->mouseHeight * scale;
        }
        // Пізніші вузли намальовані поверх, тож остання влучна і виграє.
        if (rect.contains(mouseX, mouseY)) found = node;
      }
      self(self, node->name, depth + 1);
    }
  };
  walk(walk, group, 0);
  return found;
}

}  // namespace obf2::hud
