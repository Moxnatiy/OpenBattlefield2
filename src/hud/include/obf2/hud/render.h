#pragma once
// Building the interface's geometry from the node tree.
//
// The nodes are described in 800x600 coordinates — visible from the data itself:
// `hudManager.setCommPos 150 150` together with `setCommSize 490 300` gives
// exactly 640x450, and `setCommMousePos 400 300` is the centre of an 800x600
// screen. The fonts' directory is called `800/` too.
//
// The result is ordinary RenderMesh in NDC coordinates, drawn by the same
// overlay pipeline as the text.
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

// What a layer hugs when the screen is wider than 4:3.
enum class Anchor {
  Center,  // the base rectangle in the middle
  Left,
  Right,
};

struct Screen {
  int width = 1280;
  int height = 720;
  // The scale is the same on both axes: otherwise round things become oval — that
  // is clearly visible on the minimap's frame, which is exactly 192x192. The free
  // space at the sides is handed out by the layer's anchor; that is precisely why
  // the game has separate corner layers.
  Anchor anchor = Anchor::Center;
  // The layer's offset in the base 800x600. The HUD's corner layers are described
  // from their own anchor rather than from the screen's edge, so without it they
  // land in the top left corner (see docs/formats/hud-meme.md).
  float originX = 0.0f;
  float originY = 0.0f;
};

// One piece of the interface, ready to draw.
struct DrawPiece {
  mesh::RenderMesh geometry;
  std::string texture;  // empty for text — there it is the font's atlas
  const Node* node = nullptr;
  // This particular piece's tint. Mostly it is the node's colour, but a list
  // draws its own background in its own colour (`setListNodeBackgroundColor`), so
  // one colour per node is not enough.
  Color tint;
  // An empty marker piece: the node is drawn by someone else, every frame (see
  // `Context::skipNode`). Its place in the list stays reserved, otherwise the live
  // node would land on top of everything — the map over its own frame.
  bool live = false;
};

// How to resolve what a node does not hold itself:
//   * the caption behind a localisation key;
//   * the value of an interface variable (`setNodeShowVariable` and the like).
// The font to draw a node with. In the game's data `setTextNodeStyle` is a path
// to a `.dif`, not an abstract style, and the point size is baked into the name:
// hudFontLocalBold_9, StandardTextBold_15, vehicleHudFont_6.
struct FontRef {
  const font::Font* font = nullptr;
  std::string atlas;
};

struct Context {
  std::function<std::string_view(std::string_view key)> localize;
  // A node's font by its style. An empty result means we keep the general one.
  std::function<FontRef(std::string_view style)> fontFor;
  std::function<bool(std::string_view variable)> isVisible;
  // A node's show/hide progress (see obf2/hud/animation.h). Empty means the node
  // is drawn straight in its final state.
  std::function<ShowState(const Node&)> showState;
  // A node somebody draws themselves, every frame. Its geometry is not put into
  // the shared set — otherwise its own imprint with a stale value would remain
  // under the live node (two compasses, two captions).
  // Such a node's children are built as usual.
  std::function<bool(const Node&)> skipNode;
  std::function<std::string_view(std::string_view variable)> variableText;
  // A bar's fill, 0..1 (`setBarNodeValueVariable`).
  std::function<float(std::string_view variable)> variableValue;
  // A node's alpha (`setNodeAlphaVariable`). nullopt means we know nothing about
  // that variable, and the node stays visible: most of them are smooth fades, and
  // by default they are on.
  std::function<std::optional<float>(std::string_view variable)> variableAlpha;
  // The level's map picture. Its path is given not by the HUD but by the level
  // itself — BF2.exe has the template `Levels/%s/Hud/Minimap/ingameMap.tga` for it.
  std::string mapTexture;
  // Which piece of that picture to show: the game draws not the whole level map
  // but a square around the combat area. Taken from the original's frame dump —
  // see docs/research/03-frame-dump.md. The whole picture by default.
  float mapU0 = 0.0f, mapV0 = 0.0f, mapU1 = 1.0f, mapV1 = 1.0f;

  // The red hatch over the ground outside the combat area (obf2/hud/combat_area.h).
  // Unlike the picture it is **not** cut at the map's edge: it covers the node's
  // whole square, so on Strike at Karkand it also hatches the sixty-four pixels
  // on the left where the level's map has nothing to show. Empty means no
  // overlay — a level with no combat area, or a screen that does not want one.
  std::string combatAreaTexture;

  // The markers on the map: the capture points. The map node draws them itself —
  // the data has no separate nodes for them, only the font and the caption's
  // colour (`setCPFont`, `setCPFontColor` on the map node itself).
  struct MapMarker {
    float worldX = 0.0f;
    float worldZ = 0.0f;
    std::string texture;
    std::string label;  // a localisation key
  };
  std::vector<MapMarker> mapMarkers;
  // The spawn point selection circles. The game has separate textures for them —
  // Minimap/Icons/spawn_UnSelected and spawn_Selected (plus the Inactive and
  // Squad variants). They are drawn with the same coordinate conversion as the
  // flags.
  struct SpawnMarker {
    float worldX = 0.0f;
    float worldZ = 0.0f;
    bool selected = false;
  };
  std::vector<SpawnMarker> spawnMarkers;
  // The spawn circle's size — **measured from the binary**: in the icon drawing
  // function (0x77f7c3 and 0x77f7ca) the width and the height are written with
  // the constant 0x41800000, that is 16.0. The texture choice is right there too:
  // an array of eight pointers at 0x950..0x96c, base 0x960 for the selected one
  // and 0x950 for the unselected, while the index comes from another flag
  // (whether the point is active).
  float spawnMarkerSize = 16.0f;
  // The level's world size in metres — we convert the markers' coordinates into
  // fractions of the picture with it.
  float mapWorldSize = 2048.0f;
  // The caption's offset below a point's centre, in the base 800x600. **Measured**
  // from the original's frame dump: the captions' batch has 306 vertices, that is
  // 51 letters — exactly as many as in the names of Dalian_plant's four points
  // without spaces, and its bounds are 361.5, 278.2 sized 246.0x123.8. The top
  // point (Reactor Towers) stands at y = 270.7, so the caption's top is 7.5 lower;
  // minus the font's top bearing that is 5.5.
  float mapLabelOffset = 5.5f;

  // The capture point icon's size is **32x32**, and that is measured now rather
  // than taken from the texture's size.
  //
  // The icon's record is assembled by `BF2.exe`, 0x7755a0: the width and the
  // height are written from one and the same register at 0x7755ea
  // (`MOV EDX, 0x42000000` = 32.0), then `[EAX+0x18]` and `[EAX+0x1c]`. The rest
  // of the record is visible there too: the colour 1,1,1 (0x7755bb..0x7755e1),
  // the alpha from the map node's own field +0x6fc (0x7755b1) — the one animated
  // with the map — and the texture from the table +0x904 (own team) or +0x910 (neutral).
  //
  // It used to hold 33 — the size of `miniMap_CP.tga` itself, marked as "not
  // measured". The icon preparation function 0x74fb70 indeed holds no constants:
  // it only registers the textures' paths.
  //
  // What we still do not know: **which space** those 32 are in. We put them in
  // screen pixels, but the icon's record may measure in the map's space — the icon
  // would then be smaller on the minimap than on the big map. In the combat frame
  // dump every icon is merged into one batch (call 233, 626 22 105x175), and a
  // single one's size cannot be got out of it.
  float mapMarkerSize = 32.0f;
};

// One node's geometry — a picture, a bar and/or a caption.
std::vector<DrawPiece> buildNode(const Node& node, const font::Font& font,
                                 const std::string& fontAtlas, const Screen& screen,
                                 const Context& context);

// Builds the geometry for one group of nodes.
std::vector<DrawPiece> buildGroup(const Builder& builder, std::string_view group,
                                  const font::Font& font, const std::string& fontAtlas,
                                  const Screen& screen, const Context& context);

// The same, but expanding `split` nodes: in the game's HUD they are not drawn
// themselves but substitute a whole group of the same name
// (`hudBuilder.createSplitNode GlobalHud IngameHud`). That is how the whole
// interface is assembled: Global -> GlobalHud -> IngameHud -> dozens of sub-groups.
std::vector<DrawPiece> buildTree(const Builder& builder, std::string_view rootGroup,
                                 const font::Font& font, const std::string& fontAtlas,
                                 const Screen& screen, const Context& context, int maxDepth = 8);

// A node's rectangle in screen pixels — needed for mouse hit testing.
struct ScreenRect {
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
  bool contains(float px, float py) const {
    return px >= x && py >= y && px <= x + width && py <= y + height;
  }
};

ScreenRect nodeRect(const Node& node, const Screen& screen, const Context* context = nullptr);

// A whole subtree's bounds in the base 800x600 — needed to hug a corner layer to
// the right edge. An empty tree gives nullopt.
struct Bounds {
  float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};
std::optional<Bounds> treeBounds(const Builder& builder, std::string_view rootGroup,
                                 const Context& context, int maxDepth = 8);

// A ready rectangle in NDC coordinates — by the same path as the text.
// Needed for highlighting the button under the cursor: the geometry is baked
// ahead of time for every button, so only the drawing is left in the frame.
mesh::RenderMesh buildRect(const ScreenRect& rect, const Screen& screen,
                           const std::string& texture);

// Tell the animator which nodes of a subtree have to be visible right now.
// We walk everything, the hidden included: a node that is disappearing has its own progress too.
void updateAnimator(const Builder& builder, std::string_view rootGroup, Animator& animator,
                    const Context& context, int maxDepth = 8);

// The spawn circle under the cursor — an index into `context.spawnMarkers`.
// The map has no separate nodes for them, so it catches the mouse itself.
std::optional<std::size_t> spawnMarkerAt(const Builder& builder, std::string_view rootGroup,
                                         const Screen& screen, const Context& context,
                                         float mouseX, float mouseY, int maxDepth = 8);

// The button under the cursor, or nullptr. We search from the end: later nodes
// are drawn on top, so they catch the mouse first.
const Node* buttonAt(const Builder& builder, std::string_view group, const Screen& screen,
                     float mouseX, float mouseY, const Context* context = nullptr);

}  // namespace obf2::hud
