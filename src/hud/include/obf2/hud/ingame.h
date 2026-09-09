#pragma once
// Assembling the combat HUD: the `Global` root plus the corner regions.
//
// The game has seven regions, and the nodes in them get their coordinates **from
// the region's top left corner** — the developers wrote that themselves in
// `Menu/HUD/HudSetup/Readme.txt`. Where those corners are is said not in the code but in
// `Menu/Ingame` (obf2/meme/graph.h, `Graph::layers`):
//
//   BottomLeftAnimate   BfTransformNode 400x64   X<-BottomLeft_XPos   Y=563
//     Next node -> TransformNode  X=-1  Y=563  400x64   (Static)
//   BottomRightAnimate  BfTransformNode 600x100  X<-BottomRight_XPos  Y=497
//     Next node -> TransformNode  X=401 Y=563  400x64   (Static)
//
// The X of the "moving" regions is a variable, and the file holds the hidden
// position (-295 and 503): with it the contents are entirely beyond the screen's edge.
//
// The combat HUD has to be reassembled rather than baked once and for all: in
// the game its variables are written not by the level's start but by per-frame
// work — 0x78d0f0 takes the current player and either turns on
// `PlayerHealthShow` (0x78d154) or clears the whole set (0x78d2d9).
#include <functional>
#include <string>
#include <vector>

#include "obf2/hud/render.h"
#include "obf2/meme/graph.h"

namespace obf2::hud {

struct IngameLayer {
  std::string group;
  float x = 0.0f;
  float y = 0.0f;
  // Which edge the region hugs on a wide screen. **This is ours**: the game is
  // 800x600 and has no such question.
  Anchor anchor = Anchor::Left;
};

// The regions by the graph. `leftX`/`rightX` are the current values of the
// variables the graph drives; the other numbers come from the file, and the
// fallbacks (for when there is no graph) agree with it.
std::vector<IngameLayer> ingameLayers(const meme::Graph& graph, float leftX, float rightX);

// Assemble the combat HUD: `Global` first, then every region by its own root —
// nothing in the data leads to them from `Global`.
//
// `onLayer` is called for every non-empty region — for reports (`--hud-rects`)
// and so that printing need not be dragged in here.
std::vector<DrawPiece> buildIngame(const Builder& builder, const std::vector<IngameLayer>& layers,
                                   const font::Font& font, const std::string& fontAtlas,
                                   const Screen& screen, const Context& context,
                                   const std::function<void(const IngameLayer&,
                                                            const std::vector<DrawPiece>&)>&
                                       onLayer = {});

}  // namespace obf2::hud
