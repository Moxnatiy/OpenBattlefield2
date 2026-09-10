#pragma once
// The game's interface: `hudBuilder.*` in `Menu_client.zip/HUD/`.
//
// This is a **full declarative interface system**, not Flash: 1145 files and
// 25 060 commands describe the whole in-game HUD, the scoreboard, the spawn
// screen, the level list and the server details. Flash is left only in the main menu
// (`External/FlashMenu/`, 5 `.swf`).
//
// A node is declared like this:
//
//   hudBuilder.createPictureNode IngameHud WarningIcon 701 292 32 32
//   hudBuilder.setPictureNodeTexture Ingame/GeneralIcons/.../icon.tga
//   hudBuilder.setNodeShowVariable WarningIconShow
//   hudBuilder.setNodeInTime 0.2
//
// After that every `set*` applies to the **last node created** — the same
// principle as in ObjectTemplate.
#include <cstdint>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/con/interpreter.h"

namespace obf2::hud {

enum class NodeType {
  Picture,
  Text,
  Button,
  Split,
  Bar,
  ObjectMarker,
  Compass,
  TransformList,
  // Then come the nodes we parse but do not draw yet. The type has to be known
  // anyway: without it the rectangle is read from the wrong arguments.
  List,
  Edit,
  Hover,
  Occupied,
  Slider,
  ObjectSelection,
  MiniMap,
  Map,
  Other,
};

// A node's show and hide effect.
enum class ShowEffect {
  Alpha,  // fades in
  Move,   // drives in
  Blend,
};

// One effect together with its numbers. In the game's data it looks like this:
//
//   hudBuilder.setNodeInTime 0.3
//   hudBuilder.setNodeOutTime 0.15
//   hudBuilder.addNodeAlphaShowEffect
//   hudBuilder.addNodeMoveShowEffect -1.57 376
//
// So a node appears over inTime seconds and disappears over outTime, while the
// effects say **how**: fade in and/or arrive from a distance.
// The drive-in has two numbers — the direction in radians and the distance in the base 800x600.
struct ShowEffectInfo {
  ShowEffect kind = ShowEffect::Alpha;
  float angle = 0.0f;
  float distance = 0.0f;
};

std::string_view nodeTypeName(NodeType type);

struct Color {
  float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// Which presentation the map is shown in. The thumbnail in the corner is ordinary
// combat, the big one the spawn screen and full-screen map, the commander's separate.
enum class MapView { Mini, Maxi, Commander };

// One of the map's presentations: position and size.
// Gives the map node the chosen presentation's rectangle: the map has no
// rectangle of its own at creation.
struct Node;
void useMapView(Node& node, MapView view);

// One show condition from setNodeLogicShowVariable.
struct ShowTest {
  std::string op;        // NOT | EQUAL | AND | OR
  std::string variable;
  float value = 1.0f;
};

struct MapRect {
  bool set = false;
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
};

struct Node {
  NodeType type = NodeType::Other;
  // Every command's first argument is the **parent node**, not a plain group
  // label. The developers' own Readme.txt says so in
  // HUD/HudSetup: "When you place nodes in either of these areas they
  // will gain relative coordinates from the area you placed the node
  // in". So the HUD is a tree, and x/y are counted from the parent's top left
  // corner. A parent may be a region (Global, TopLeft, BottomLeftStatic…) or any
  // other node: in the game's data the tree reaches eight levels, and only six
  // nodes hang directly on the regions.
  std::string group;
  std::string name;

  // Coordinates in the game's virtual screen (see kReferenceWidth/Height).
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

  std::string texture;          // setPictureNodeTexture / setButtonNodeTexture (state 1)
  std::string hoverTexture;     // setButtonNodeTexture 2 — under the cursor
  std::string textureVariable;  // setPictureNodeVariableTexture
  std::string text;             // setTextNodeString
  std::string textVariable;     // setTextNodeStringVariable
  std::string style;            // setTextNodeStyle
  // setTextNodeStyle's second argument is the line's alignment within the node's
  // frame. The data holds only three values: 0 (142 times), 1 (97) and 2 (103).
  // The original's frame dump shows: a message in the middle of the screen has 0
  // and stands centred, while a kit's caption has 2 and hugs the left.
  int textAlign = 0;
  std::string showVariable;     // the show condition — setNodeShowVariable
  // setNodeLogicShowVariable always has the form `action variable value`:
  // NOT (118), EQUAL (92), AND (44), OR (31). It is not a variable's name, as we
  // used to read it, but a separate condition joined to showVariable. Because of
  // that mistake whole branches of the HUD were never shown.
  std::vector<ShowTest> showTests;
  std::string alphaVariable;
  std::string command;          // the first action on a click
  // Every one of a button's commands with its event: 0 is a click, 1 a hover.
  std::vector<std::pair<int, std::string>> commands;

  Color color;
  float inTime = 0.0f;
  float outTime = 0.0f;

  std::string altCommand;      // setButtonNodeAltConCmd — the right button's action
  std::string valueVariable;   // setBarNodeValueVariable — the bar's fill
  // A bar has an extra number of its own before the rectangle, and two textures:
  // empty (0) and full (1). What the number means is **not established**. It was
  // read as the growth direction until the frame dump said otherwise: the kit's
  // sprint bar is a `3` and it grows from the left like a `2`. The data pairs it
  // with the artwork — the map's `EnemyCPs` is a `3` with
  // `flags_Captured_Right.tga` and a mirrored `setBarNodeBorder` — but there is no
  // dump of the battle HUD to check that against, so nothing is read out of it.
  int barKind = 0;
  std::string barTextureEmpty;
  std::string barTextureFull;
  // `setNodePosVariable <axis> <variable>`: **the first argument is the axis**
  // (0 = X, 1 = Y), not a name. We used to take it for the name, so the field
  // held "0" or "1" and no such variable was ever found.
  // In the data this moves the sight: four rays of
  // `vsp_CrossHair_single.tga` spread apart by the weapon's dispersion
  // (`HudElementsGenericWeapon.con`).
  std::string positionVariableX;
  std::string positionVariableY;
  std::string rotateVariable;    // setPictureNodeRotateVariable
  std::vector<std::string> rgbVariables;  // setNodeRGBVariables

  float offsetX = 0.0f, offsetY = 0.0f;        // setNodeOffset
  float centerX = 0.0f, centerY = 0.0f;        // setPictureNodeCenterPoint
  float textureWidth = 0.0f, textureHeight = 0.0f;  // setObjectMarkerNodeTextureSize

  // The rectangle in which a button catches the mouse; when unset, the whole node.
  bool hasMouseArea = false;
  float mouseX = 0.0f, mouseY = 0.0f, mouseWidth = 0.0f, mouseHeight = 0.0f;

  std::string font;              // setListNodeFont / setTextNodeFont
  Color borderColor;             // setPictureNodeBorderColor
  float borderSize = 0.0f;       // setCompassNodeBorder / setBarNodeBorder
  // setBarNodeSnap — the width of one step of a bar's fill, in HUD units, not a
  // flag. The data uses three values: 1 (24 bars), 4 (24) and 20 (the seven kit
  // rows). A bar 59 wide with a step of 20 therefore has three steps, and that is
  // what the original draws: on Strike at Karkand the sprint bar of a kit whose
  // `sprintStaminaDissipationFactor` is 0.2 comes out 58.5 px wide and of one
  // whose factor is 0.6 comes out 39.0 — exactly two thirds (the frame dump,
  // docs/research/spawn-screen-named.md). Snapping to twentieths is ruled out by
  // the same measurement: 39/58.5 is not a multiple of 1/20.
  float barSnap = 0.0f;          // setBarNodeSnap
  // The objects a marker points at, and its caption node.
  std::vector<std::string> markerObjects;
  std::string lockTextNode;

  int barSnapDir = 0;      // setBarNodeSnapDir — which way the bar "snaps"
  float rotation = 0.0f;   // setPictureNodeRotation

  // Filled in by Builder::finish(): the parent's index in nodes() (-1 means the
  // parent is a region), the name of the root region and the position from its
  // top left corner, accumulated over every ancestor.
  int parent = -1;
  std::string area;
  float absX = 0.0f, absY = 0.0f;

  // A list (the scoreboard, squad selection). Its background and border are not
  // textures but solid colours — which is exactly why a list without them looked
  // like empty space:
  //
  //   createListNode Scoreboard FriendlyScoreList 10 75 389 462 19 1
  //   setListNodeBackgroundColor 0.745 0.729 0.58 0.9
  //   setListNodeBorder 20 22 3 3
  //   setListNodeBorderColor 0.482 0.474 0.388 1
  //
  // The second-to-last number in createListNode is the row height.
  bool hasListBackground = false;
  Color listBackground;
  bool hasListBorder = false;
  Color listBorderColor;
  float listBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // left, right, top, bottom
  float listRowHeight = 0.0f;
  Color listSelectColor;                  // setListNodeSelectColor r g b a
  bool hasListScrollbar = false;          // setListNodeScrollbar <width> <gap>
  float listScrollbarWidth = 0.0f;
  float listScrollbarGap = 0.0f;
  Color listScrollbarColor;
  Color listScrollbarBackground;
  int listData = -1;             // setListNodeData — the row source's number
  float listRowSpacing = 0.0f;   // setListNodeRowSpacing
  bool listOutline = false;      // setListNodeOutline
  // setListNodeConCmd <number> "<command>" — what to run on a click.
  std::vector<std::pair<int, std::string>> listCommands;

  // An input field (chat, a squad's name).
  std::string editFont;      // setEditNodeFont <path> <number>
  int editData = -1;         // setEditNodeData
  int editString = -1;       // setEditNodeString
  int editMaxLength = 0;     // setEditNodeMaxLength
  Color editColor;           // setEditNodeColor r g b a
  bool hasEditColor = false;

  // An object marker (target lock in a vehicle).
  int markerLockOnType = 0;                          // setObjectMarkerNodeLockOnType
  int markerWeapon = 0;                              // setObjectMarkerNodeWeapon
  std::string markerLockText;                        // setObjectMarkerNodeLockText <n> <node>
  float markerLockTextOffset[2] = {0.0f, 0.0f};      // setObjectMarkerNodeLockTextOffset

  // The seats in a vehicle: setOccupiedNodeData <number>, while the coordinate
  // pairs come from variables — setOccupiedNodePosVariable <number> <variable>.
  int occupiedData = -1;
  std::vector<std::string> occupiedPosVariables;

  // The compass: what the marks "snap" to and what draws them.
  float compassSnapOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  std::vector<std::string> compassSnapTextures;

  // A hover node (the tooltip under the cursor).
  float hoverMiddle[2] = {0.0f, 0.0f};
  float hoverMaxValue = 0.0f;
  float hoverWidth = 0.0f;
  float hoverLength = 0.0f;

  // A slider: which node travels and which variable it changes.
  std::string sliderChild;
  std::string sliderData;

  // Text outlining: a separate font and the offset it is drawn underneath with.
  std::string outlineFont;                     // setTextNodeOutLine
  float outlineOffset[2] = {0.0f, 0.0f};       // setTextNodeOutLineOffset

  // Object selection: the pointer's size.
  float pointerSize[2] = {0.0f, 0.0f};

  // The step between the items of a transform list (setTranformListNodeOffset).
  float childOffsetX = 0.0f, childOffsetY = 0.0f;

  // The drawing layer. `hudBuilder.newLayer` starts the next one: everything
  // created after it lands on top of the previous, regardless of its place in the
  // tree. In the game's data it occurs once — before the map.
  int layer = 0;

  // The map: the zoom icons and the font of the points' captions.
  int zoomIcons = 0;
  std::string cpFont;
  Color cpFontColor;

  // The map gets no rectangle of its own at creation — the game gives it three
  // different presentations with separate commands (HudElementsMap.con):
  //
  //   setMaxiPos -122/-273   setMaxiSize 512/512     the spawn screen
  //   setMiniPos 197/-300    setMiniSize 197/197     the corner during combat
  //   setCommanderPos …      setCommanderSize 561/561  commander mode
  //
  // The pair is written as **one word** separated by a slash, not as two
  // arguments. A negative number means counting from the right or bottom edge —
  // the same as in the rest of the HUD.
  MapRect mapMaxi;
  MapRect mapMini;
  MapRect mapCommander;
  // Which presentation is current. The thumbnail in the game is round while the
  // big one on the spawn screen is square; the frame map_Frame.tga does not cover
  // the corners (it is transparent there), so the circle has to come from the node.
  MapView mapView = MapView::Mini;

  // The commands we already recognise but do not draw yet: their arguments lie
  // here as they are. That way they are not lost silently, and the list shows
  // what the renderer is missing rather than the parser. The table is hud_recorded.inc.
  std::map<std::string, std::vector<std::string>> extra;

  std::vector<ShowEffectInfo> showEffects;
  // The names of the nodes added to this transform list.
  std::vector<std::string> children;
};

// BF2's HUD is laid out in 800x600 coordinates and stretched over the screen.
//
// That is visible from the data itself rather than assumed: `hudManager.setCommPos
// 150 150` together with `setCommSize 490 300` gives exactly 640x450, and
// `setCommMousePos 400 300` is the centre of 800x600. The fonts' directory is `800/` too.
inline constexpr float kReferenceWidth = 800.0f;
inline constexpr float kReferenceHeight = 600.0f;

class Builder {
 public:
  // Attached as a command handler to con::Interpreter.
  void feed(const con::Command& command);

  // Call it after every command has been fed in: this is where the tree is
  // linked and Node::parent, Node::area and Node::absX/absY appear. Without this
  // step the coordinates stay relative — and the HUD falls apart.
  void finish();

  // Switches every map node to the given presentation. The game has one map,
  // shown in different ways: in combat a thumbnail in the corner, on the spawn
  // screen the big one (setMaxiPos/setMaxiSize), the commander's its own.
  void setMapView(MapView view);

  // The same, but with the rectangle the map's animation computed
  // (obf2/hud/map_node.h). The coordinates are already in screen space: half a
  // screen is added by the animation itself, as the engine does at 0x77d2b0.
  // `shape` only picks the outline: the thumbnail in the game is round, the big
  // map square. **Not established** where the engine gets the round mask from;
  // we take the outline from `MapMinSize` — that is, "the map stands at its
  // small size".
  void setMapRect(float x, float y, float width, float height, MapView shape);

  const std::vector<Node>& nodes() const { return nodes_; }
  std::vector<const Node*> group(std::string_view name) const;
  std::vector<std::string> groups() const;

  long long unknownCommands() const { return unknown_; }
  const std::map<std::string, int>& unknownByName() const { return unknownByName_; }

 private:
  Node* active();

  std::vector<Node> nodes_;
  std::map<std::string, int> unknownByName_;
  int activeIndex_ = -1;  // -1 = the last one created
  int layer_ = 0;         // the current layer, moved on by hudBuilder.newLayer
  long long unknown_ = 0;
};

}  // namespace obf2::hud
