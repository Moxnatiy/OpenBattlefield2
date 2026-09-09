#include "obf2/hud/ingame.h"

namespace obf2::hud {
namespace {

// A region from the graph by the name of the variable its X is bound to. With no
// graph we take the fallback numbers; they agree with the file.
struct Placement {
  float y;
  float twinX;
  float twinY;
};

Placement placementOf(const std::vector<meme::Graph::Layer>& layers, const char* variable,
                      float fallbackY, float fallbackTwinX, float fallbackTwinY) {
  for (const meme::Graph::Layer& found : layers) {
    if (found.variable != variable) continue;
    return Placement{found.y, found.hasTwin ? found.twinX : fallbackTwinX,
                     found.hasTwin ? found.twinY : fallbackTwinY};
  }
  return Placement{fallbackY, fallbackTwinX, fallbackTwinY};
}

}  // namespace

std::vector<IngameLayer> ingameLayers(const meme::Graph& graph, float leftX, float rightX) {
  const auto layers = graph.layers();
  const Placement left = placementOf(layers, "BottomLeft/BottomLeft_XPos", 563.0f, -1.0f, 563.0f);
  const Placement right =
      placementOf(layers, "BottomRight/BottomRight_XPos", 497.0f, 401.0f, 563.0f);
  return {
      IngameLayer{"BottomLeftAnimate", leftX, left.y, Anchor::Left},
      IngameLayer{"BottomLeftStatic", left.twinX, left.twinY, Anchor::Left},
      IngameLayer{"BottomRightAnimate", rightX, right.y, Anchor::Right},
      IngameLayer{"BottomRightStatic", right.twinX, right.twinY, Anchor::Right},
  };
}

std::vector<DrawPiece> buildIngame(
    const Builder& builder, const std::vector<IngameLayer>& layers, const font::Font& font,
    const std::string& fontAtlas, const Screen& screen, const Context& context,
    const std::function<void(const IngameLayer&, const std::vector<DrawPiece>&)>& onLayer) {
  auto pieces = buildTree(builder, "Global", font, fontAtlas, screen, context);

  for (const IngameLayer& layer : layers) {
    Screen layerScreen = screen;
    layerScreen.originX = layer.x;
    layerScreen.originY = layer.y;
    layerScreen.anchor = layer.anchor;
    auto layerPieces = buildTree(builder, layer.group, font, fontAtlas, layerScreen, context);
    if (layerPieces.empty()) continue;
    if (onLayer) onLayer(layer, layerPieces);
    for (DrawPiece& piece : layerPieces) pieces.push_back(std::move(piece));
  }
  return pieces;
}

}  // namespace obf2::hud
