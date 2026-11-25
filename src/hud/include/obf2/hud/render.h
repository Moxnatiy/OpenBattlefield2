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
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/font/text.h"
#include "obf2/hud/animation.h"
#include "obf2/hud/hud.h"
#include "obf2/hud/list_data.h"
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

// One live icon on the map, gathered from the session's world each frame
// (obf2/app/map_contacts.h, docs/functions/hud-map.md): a player, a vehicle, an
// order, a commander asset or a spot.
struct MapContact {
  enum class Kind { Self, Squad, Team, Spotted, Order, PendingOrder, CommanderAsset, Neutral };
  float worldX = 0.0f;
  float worldZ = 0.0f;
  Kind kind = Kind::Team;
  unsigned order = 0;  // an order's id: 4 move, 5 attack, 6 defend, 7 repair, 8 destroy
  bool commander = false;  // an order from the commander
  enum class Asset { UAV, Artillery, Supplies, Vehicle };
  Asset asset = Asset::UAV;
  // The template's own icon (a vehicle's `VehicleHud.miniMapIcon`, a strategic
  // object's `StrategicObject.intactIcon`, a squad leader's number); empty for
  // the kind's own icon.
  std::string texture{};
  // The icon's side in base 800x600 units. 16 is the vehicle icon's measured
  // size, 19 the strategic icon's (docs/functions/hud-map.md); a soldier's 16 is
  // the size the map's player icons always had here, not measured.
  float size = 16.0f;
  // Turned counter-clockwise on screen by this many radians.
  float angle = 0.0f;
  // A spot at a reported position only (layer 0x1e, 0x7820e0): drawn while
  // `Context::mapSpotsVisible` is on.
  bool blinks = false;
  // A spotted enemy drawn with its own icon in the enemy colour (0x7788b0).
  bool enemy = false;
  // A kind-1 mark (a teammate's radio message): yellow while
  // `Context::mapSpotsVisible` is on (0x77a9a0; (1, 1, 0) from 0x97e170).
  bool radioBlink = false;
  // A kind-2 mark (under the UAV beam): the local team's entry of table
  // 0xa1cc28, (1, 0, 0) for either team (0x97dfc0, 0x97e150), its alpha `alpha`.
  bool swept = false;
  // The network object drawn, 0 for none.
  std::uint16_t object = 0;
  // A second texture over the icon, 16 px, turned by `overlayHeading` (the
  // object's atan2(forward x, forward z)) against the map's north: the
  // specialist's `mini_MedicArrow` (0x783bc4, 0x783ddd).
  std::string overlayTexture{};
  float overlayHeading = 0.0f;
  // Above 0: the icon's side in world metres, not pixels (the UAV circle,
  // 2 × getUAVRadius, 0x77ff2d). `alpha` scales its tint.
  float worldSize = 0.0f;
  float alpha = 1.0f;
  // A colour that replaces the kind's: the commander's white and the heading
  // arrow's black (`colorMapIcon`, OpenBF2 Menu/Bf2MenuMapNode.cpp).
  std::optional<Color> tint{};

  bool operator==(const MapContact&) const = default;
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
  // An edit node's text: string N (`setEditNodeString N`) of the HUD
  // manager's string table (`BF2.exe` 0x7b13a0 writes it, 0x7b1410 reads it).
  // Null, or null for a number, and the box draws empty.
  std::function<const std::string*(int string)> editText;
  // A bar's fill, 0..1 (`setBarNodeValueVariable`).
  std::function<float(std::string_view variable)> variableValue;
  // A number some code has written, or nullopt when nobody has: what a node's
  // position variable (`setNodePosVariable`) reads. Unlike `variableValue` an
  // unknown variable is not zero here — a coordinate of zero would throw the
  // node into the corner.
  std::function<std::optional<float>(std::string_view variable)> variableNumber;
  // A node's alpha (`setNodeAlphaVariable`). nullopt means we know nothing about
  // that variable, and the node stays visible: most of them are smooth fades, and
  // by default they are on.
  std::function<std::optional<float>(std::string_view variable)> variableAlpha;
  // The rows of a list node: `setListNodeData <n>` names the list (0x7af170).
  // Empty, or null for a number, and the node draws its plate alone.
  std::function<const ListData*(int data)> listData;
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
  // The square the hatch was rasterized over, in the picture's coordinates — the
  // map's window at zoom exponent 0. When the map is zoomed in (`MiniMap.setZoom`,
  // the N key) the window is a part of it, and the hatch is drawn from that part
  // so that it stays on the ground it marks.
  float combatAreaU0 = 0.0f, combatAreaV0 = 0.0f, combatAreaU1 = 1.0f, combatAreaV1 = 1.0f;

  // The markers on the map: the capture points. The map node draws them itself —
  // the data has no separate nodes for them, only the font and the caption's
  // colour (`setCPFont`, `setCPFontColor` on the map node itself).
  struct MapMarker {
    float worldX = 0.0f;
    float worldZ = 0.0f;
    std::string texture;
    std::string label;  // a localisation key
    // Zero means the map's own capture-point size. The other two things the map
    // shows come at their own sizes, both measured on the original's spawn
    // screen: a vehicle spawner's icon is 16x16 and a strategic object's is
    // 19x19 (docs/functions/hud-map.md).
    float size = 0.0f;
    // A live icon's colour and turn (the map's player and vehicle icons); the
    // level's markers keep white, and a strategic icon turns with its object
    // (0x77e500).
    Color color;
    float rotation = 0.0f;  // radians, counter-clockwise on screen
    // Drawn at the map node's icon alpha, `mapIconAlpha` below: the strategic
    // holders (layer 0x10, 0x77e500), the vehicles and their spawners (0x783f90)
    // and the soldiers (0x783440, 0x782480). The flags (layer 3, 0x77e720) do
    // not read it.
    bool atIconAlpha = false;
  };
  // The map node's icon alpha, +0x6fc, which the strategic (0x77e500), vehicle
  // (0x783f90) and soldier (0x783440, 0x782480) layers draw with. It eases (0x77c330, 1 − e^(−10·dt))
  // toward +0x704: 1.0 from the constructor (0x7818aa), 0.5 on the spawn screen
  // (0x777fe8). The spawn screen is baked once, so it takes the settled value.
  // In game the target is the setting `MenuMapIconAlpha` (0x777eef), not ported:
  // the constructor's 1.0 stands.
  float mapIconAlpha = 1.0f;
  std::vector<MapMarker> mapMarkers;
  // The live icons (obf2/app/map_contacts.h), drawn over the markers.
  std::vector<MapContact> minimapContacts;
  // The map node's blink +0xa18 (`MapNode::spotsVisible`), which gates layer
  // 0x1e's position-only spots and a kind-1 mark's yellow.
  bool mapSpotsVisible = true;
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

// The list node under the cursor and the row and column hit in it, the way the
// list node picks them (OpenBF2 Menu/BfNewListBoxNode.cpp, the press: the row by
// whole row pitches down from the top border, the column the last visible one
// whose x is not right of the cursor). The row counts from the first row drawn.
struct ListHit {
  const Node* node = nullptr;
  int row = -1;
  int column = -1;
};
std::optional<ListHit> listAt(const Builder& builder, std::string_view group, const Screen& screen,
                              float mouseX, float mouseY, const Context& context);

// The button under the cursor, or nullptr. We search from the end: later nodes
// are drawn on top, so they catch the mouse first.
const Node* buttonAt(const Builder& builder, std::string_view group, const Screen& screen,
                     float mouseX, float mouseY, const Context* context = nullptr);

}  // namespace obf2::hud
