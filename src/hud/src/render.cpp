#include "obf2/hud/render.h"

#include "obf2/core/math.h"

#include <algorithm>
#include <cmath>

namespace obf2::hud {
namespace {

// The normal points along the light source: the overlay pipeline does not use it,
// but the vertex is shared by every mesh, so the field has to be filled in.
mesh::Vec3 flatNormal() { return mesh::Vec3{0.0f, 1.0f, 0.0f}; }

// A rectangle in NDC with a colour that goes through the texture's alpha.
// `uMin`/`uMax` make it possible to show only part of the picture — that is how a
// bar's fill is drawn.
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

  // Rotation around the node's middle — `setPictureNodeRotateVariable`.
  // The picture itself rotates, so we rotate the corners in pixels rather than in
  // NDC: otherwise on a non-square screen a circle would become an oval.
  //
  // The sign: on screen Y grows downwards, so a positive angle has to turn
  // **counter-clockwise**. Otherwise the compass would show a cardinal point from
  // the opposite side: looking east, "E" would end up at the bottom rather than
  // the top.
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

// A circle instead of a rectangle — the map's thumbnail is drawn with this. The
// frame map_Frame.tga does not cover the corners (it is transparent there), so
// the node itself has to clip the picture.
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

// Whether to show a node. Besides a plain variable (setNodeShowVariable) a node
// may carry a chain of conditions from setNodeLogicShowVariable — each compares a
// variable against a value, and the action says how to join the result:
//
//   EQUAL HudState 0        show when HudState equals 0
//   NOT   DisconnectMessageActive 1   ... when it does NOT equal
//   AND   ServerIsFavourite 1         ... and additionally
//   OR    PauseMessageActive 1        ... or
//
// An unknown variable gives 0 — which is exactly why `EQUAL HudState 0` is true
// by default while `AND ServerIsFavourite 1` is not.
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
      // Without setNodeShowVariable the first condition gives the answer: there is
      // nothing to join it to.
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

// Whether a node is on screen. The show condition sets the **target**, the
// animator only the progress towards it: while a node is travelling or fading it
// is still visible. For a node the animator does not know, the condition answers.
bool nodeShown(const Node& node, const Context& context) {
  const ShowState show = nodeShowState(node, context);
  if (!show.known) return nodeVisible(node, context);
  return show.progress > 0.0f;
}

ScreenRect nodeRect(const Node& node, const Screen& screen, const Context* context) {
  // The scale is the same on both axes — by the height. Stretching by the width
  // would turn round things oval.
  const float scale = static_cast<float>(screen.height) / kReferenceHeight;
  const float spare = static_cast<float>(screen.width) - kReferenceWidth * scale;
  const float padX = screen.anchor == Anchor::Center  ? spare * 0.5f
                     : screen.anchor == Anchor::Right ? spare
                                                      : 0.0f;
  // We take absX/absY rather than x/y: in the game a node's coordinates are
  // counted from its parent, and without the sum over the ancestors everything
  // scatters over the screen. The setNodeOffset shift is already in that sum
  // (Builder::finish). The move effect's offset is in the base 800x600 too, so it scales the same.
  float shiftX = 0.0f, shiftY = 0.0f;
  if (context != nullptr && context->showState) {
    const ShowState show = context->showState(node);
    shiftX = show.offsetX;
    shiftY = show.offsetY;
  }
  // `setNodePosVariable <axis> <variable>` — a node's offset from a variable. In
  // the data this spreads the sight's four rays by the weapon's dispersion
  // (`HudElementsGenericWeapon.con`). An unknown variable gives zero — the node
  // stays where the data puts it.
  if (context != nullptr && context->variableValue) {
    if (!node.positionVariableX.empty()) shiftX += context->variableValue(node.positionVariableX);
    if (!node.positionVariableY.empty()) shiftY += context->variableValue(node.positionVariableY);
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

  // A bar: we show not the whole picture but a part of it by the variable's value.
  if (node.type == NodeType::Bar) {
    float value = 1.0f;
    if (!node.valueVariable.empty() && context.variableValue) {
      value = context.variableValue(node.valueVariable);
    }
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);

    const std::string& texture = node.barTextureFull.empty() ? node.texture : node.barTextureFull;
    if (!texture.empty() && value > 0.0f) {
      // Direction 3 in the data means a bar that grows right to left (the right
      // half of the screen is the enemy team).
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

  // The map: the node has no texture of its own — the level provides it.
  if (node.type == NodeType::Map || node.type == NodeType::MiniMap) {
    if (!context.mapTexture.empty()) {
      // The thumbnail in combat is round, while the big one on the spawn screen is
      // square. The game shows not the whole level picture but a square around the
      // combat area — the bounds arrive in the context.
      auto geometry = node.mapView == MapView::Mini
                          ? disc(rect, screen, context.mapTexture, context.mapU0, context.mapV0,
                                 context.mapU1, context.mapV1)
                          : quad(rect, screen, context.mapTexture, context.mapU0, context.mapU1,
                                 context.mapV0, context.mapV1);
      pieces.push_back(DrawPiece{std::move(geometry), context.mapTexture, &node, node.color});
    }

    // The capture points' markers. The data has no separate nodes for them — the
    // map draws them itself, taking the caption's font and colour from its own node
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
        // The caption stands below the icon and centred on it.
        layout.x = cx - font::textWidth(*face, text, layout.scale) * 0.5f;
        // The caption's offset was measured separately from the icon: in the game
        // it is simply a distance from the point's centre and does not depend on the icon's size.
        layout.y = cy + context.mapLabelOffset * scaleY;
        auto geometry = font::buildText(*face, text, layout, atlas);
        if (!geometry.indices.empty()) {
          pieces.push_back(DrawPiece{std::move(geometry), atlas, &node, node.cpFontColor});
        }
      }

      // The spawn point selection circles — as separate textures.
      for (const Context::SpawnMarker& spawn : context.spawnMarkers) {
        const float u = (spawn.worldX + half) / context.mapWorldSize;
        const float v = (half - spawn.worldZ) / context.mapWorldSize;
        if (u < context.mapU0 || u > context.mapU1) continue;
        if (v < context.mapV0 || v > context.mapV1) continue;
        const float cx = rect.x + (u - context.mapU0) / uSpan * rect.width;
        const float cy = rect.y + (v - context.mapV0) / vSpan * rect.height;
        const float size = context.spawnMarkerSize * scaleY;
        const std::string texture =
            // The paths are strings from the binary (0x930724 and 0x93065c). The
            // extension there is .tga even though the archive holds .dds; the
            // substitution is done by our texture lookup, as for the rest of the HUD.
            spawn.selected ? "Ingame/Minimap/Icons/spawn_Selected.tga"
                           : "Ingame/Minimap/Icons/spawn_UnSelected.tga";
        const ScreenRect box{cx - size * 0.5f, cy - size * 0.5f, size, size};
        pieces.push_back(DrawPiece{quad(box, screen, texture), texture, &node, Color{}});
      }
    }
    return pieces;
  }

  // A list: the background and the border are solid colours, not textures. We draw
  // the border over the whole node and the background inside its insets. The rows
  // will appear once there is somewhere to take the players from; the plate itself
  // is needed already, because without it the scoreboard has a hole instead of a list.
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

  // Pictures and buttons — a rectangle with a texture.
  if (node.type == NodeType::Picture || node.type == NodeType::Button) {
    std::string texture = node.texture;
    if (texture.empty() && !node.textureVariable.empty() && context.variableText) {
      texture = std::string(context.variableText(node.textureVariable));
    }
    if (!texture.empty()) {
      // `setPictureNodeRotateVariable` — an angle in radians. In the data only the
      // minimap's compass has one (`MapCompass` -> `MinimapDelayedMapAngle`,
      // HudElementsMap.con), and the map drives it: the map node's field +0x760,
      // registered at `BF2.exe`, 0x780a49.
      // A rotation may be constant (`setPictureNodeRotation`, in degrees — that is
      // how three of the sight's four rays are turned) or from a variable
      // (`setPictureNodeRotateVariable`, in radians — the compass).
      constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
      float angle = node.rotation * kToRadians;
      if (!node.rotateVariable.empty() && context.variableValue) {
        angle += context.variableValue(node.rotateVariable);
      }
      pieces.push_back(
          DrawPiece{quad(rect, screen, texture, 0.0f, 1.0f, 0.0f, 1.0f, angle), texture, &node,
                    node.color});
    }
  }

  // Text: first a direct value, then a variable, then a localisation key.
  if (node.type == NodeType::Text || node.type == NodeType::Button) {
    std::string text = node.text;
    if (text.empty() && !node.textVariable.empty() && context.variableText) {
      text = std::string(context.variableText(node.textVariable));
    }
    if (text.empty()) return pieces;
    if (context.localize) text = std::string(context.localize(text));
    if (text.empty()) return pieces;

    // The point size is set by the font itself, not by the node's height:
    // `setTextNodeStyle` points at a particular `.dif`, and the size stands in its
    // name (hudFontLocalBold_9, StandardTextBold_15, vehicleHudFont_6). Until now
    // we stretched any string to its frame's height — which made the scoreboard's
    // captions two or three times larger than the original's.
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
    // Only the conversion from the base 800x600 into the window is left.
    layout.scale = scaleY;

    // The alignment is set by setTextNodeStyle's second argument. Both ends are
    // verified by the original's frame dump: a message in the middle of the screen
    // has 0 and stands centred ((800-301.3)/2 = 249.35 against 249.5 in the dump),
    // while a kit's caption has 2 and starts right at the frame's edge (34 in the
    // data against 33.5 in the dump).
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

  // `setNodeRGBVariables <red> <green> <blue>` — a node's colour from variables.
  // In the data the ticket numbers hang on this (`FriendlyTeamTopRed` and its
  // neighbours) along with the capture points' bars.
  //
  // The rule is the same as for the alpha: **we do not know the variable, we do
  // not touch the colour**. Otherwise the ticket numbers would go black, because
  // an unknown variable is zero. Who writes these variables in the game is **a source not found**.
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

  // The alpha effect multiplies the alpha of everything the node drew.
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
    // A node with a show variable is drawn only when that variable is on.
    if (!nodeShown(*node, context)) continue;
    for (auto& piece : buildNode(*node, font, fontAtlas, screen, context)) {
      pieces.push_back(std::move(piece));
    }
  }
  return pieces;
}

namespace {

// A node with its alpha on a variable: while that variable is unknown to us, the
// node is not drawn. We do the same with `setNodeShowVariable`, and so does the
// game: `MenuBackgroundAlpha` is the **menu's** background, in combat it is zero,
// and the wide plates under the health and ammo bars are simply not visible there.
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

  // A depth-first walk in declaration order: later nodes land on top, so the walk
  // order is the drawing order.
  const auto walk = [&](auto&& self, std::string_view group, int depth) -> void {
    if (depth > maxDepth) return;
    for (const std::string& seen : visited) {
      if (seen == group) return;  // a guard against a cycle in the data
    }
    visited.emplace_back(group);

    for (const Node* node : builder.group(group)) {
      if (!nodeShown(*node, context)) continue;
      if (node->type == NodeType::Split) {
        // A "split" node draws nothing itself: it substitutes the group whose name
        // matches its own.
        self(self, node->name, depth + 1);
        continue;
      }
      if (hiddenByAlpha(*node, context)) continue;
      if (context.skipNode && context.skipNode(*node)) {
        // A live node: the geometry comes from whoever drives the value, and a
        // marker is left here — so the drawing order does not slide.
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
      // The parent may be **any** node, not only a Split: in the game's data 30
      // more transform nodes and two pictures have children. While we descended
      // only through Splits, their subtrees were not drawn.
      self(self, node->name, depth + 1);
    }
  };
  walk(walk, rootGroup, 0);
  // `hudBuilder.newLayer` starts a new layer: everything created after it lands on
  // top of the previous, regardless of its place in the tree. The order within a
  // layer is the walk order, so the sort has to be stable.
  std::stable_sort(pieces.begin(), pieces.end(), [](const DrawPiece& a, const DrawPiece& b) {
    const int left = a.node != nullptr ? a.node->layer : 0;
    const int right = b.node != nullptr ? b.node->layer : 0;
    return left < right;
  });
  return pieces;
}

std::optional<Bounds> treeBounds(const Builder& builder, std::string_view rootGroup,
                                const Context& context, int maxDepth) {
  // We walk the same path as buildTree: what has to be counted is exactly what
  // really reaches the screen, otherwise disabled nodes would drag the bounds out.
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
  // The screen's buttons lie deep in the tree (SelectKit0 sits under
  // Kit0NotSelected), so we walk it the same way as when drawing, and with the
  // same show conditions: an invisible button catches no mouse.
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
          // The mouse region is given as an offset from the node itself rather than
          // as a separate place on screen.
          const float scale = static_cast<float>(screen.height) / kReferenceHeight;
          rect.x += node->mouseX * scale;
          rect.y += node->mouseY * scale;
          rect.width = node->mouseWidth * scale;
          rect.height = node->mouseHeight * scale;
        }
        // Later nodes are drawn on top, so the last hit wins.
        if (rect.contains(mouseX, mouseY)) found = node;
      }
      self(self, node->name, depth + 1);
    }
  };
  walk(walk, group, 0);
  return found;
}

}  // namespace obf2::hud
