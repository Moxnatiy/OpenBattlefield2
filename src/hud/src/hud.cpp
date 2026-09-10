#include <cctype>
#include <cstdlib>
#include "obf2/hud/hud.h"

#include <algorithm>

namespace obf2::hud {
namespace {

// createXxxNode <group> <name> <x> <y> <width> <height>
bool readRect(const con::Command& command, Node& node, int skip = 0) {
  if (command.args.size() < 2) return false;
  node.group = command.args[0];
  node.name = command.args[1];
  // A bar has one more argument before the rectangle — a number whose meaning is
  // not established (`Node::barKind`) — so the coordinates are shifted by one.
  node.x = command.argFloat(2 + skip).value_or(0.0f);
  node.y = command.argFloat(3 + skip).value_or(0.0f);
  node.width = command.argFloat(4 + skip).value_or(0.0f);
  node.height = command.argFloat(5 + skip).value_or(0.0f);
  return true;
}

}  // namespace

std::string_view nodeTypeName(NodeType type) {
  switch (type) {
    case NodeType::Picture: return "picture";
    case NodeType::Text: return "text";
    case NodeType::Button: return "button";
    case NodeType::Split: return "split";
    case NodeType::Bar: return "bar";
    case NodeType::ObjectMarker: return "marker";
    case NodeType::Compass: return "compass";
    case NodeType::TransformList: return "list";
    case NodeType::List: return "listbox";
    case NodeType::Edit: return "edit";
    case NodeType::Hover: return "hover";
    case NodeType::Occupied: return "occupied";
    case NodeType::Slider: return "slider";
    case NodeType::ObjectSelection: return "selection";
    case NodeType::MiniMap: return "minimap";
    case NodeType::Map: return "map";
    case NodeType::Other: return "other";
  }
  return "?";
}

Node* Builder::active() {
  // The active node is either the one chosen through setActiveObject or the last
  // one created.
  if (activeIndex_ >= 0 && activeIndex_ < static_cast<int>(nodes_.size())) {
    return &nodes_[static_cast<std::size_t>(activeIndex_)];
  }
  return nodes_.empty() ? nullptr : &nodes_.back();
}

namespace {

std::string lowered(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

void Builder::finish() {
  // The parent is looked up by name: in the game's data it is only ever given that way.
  std::map<std::string, int> byName;
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    byName.emplace(lowered(nodes_[i].name), static_cast<int>(i));
  }
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const auto found = byName.find(lowered(nodes_[i].group));
    // A region (Global, BottomLeftStatic…) is not a node, so it will not be found
    // — and that is what a root means. A node is not its own parent either.
    nodes_[i].parent =
        (found != byName.end() && found->second != static_cast<int>(i)) ? found->second : -1;
  }
  // The absolute position is the sum over the ancestors. A step counter saves us
  // from a cycle: the game's data has none, but a mod may.
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    float x = 0.0f;
    float y = 0.0f;
    std::string area = nodes_[i].group;
    int at = static_cast<int>(i);
    for (int step = 0; at >= 0 && step < 64; ++step) {
      const Node& current = nodes_[static_cast<std::size_t>(at)];
      // setNodeOffset shifts a node together with its children, so it enters the
      // sum alongside x/y.
      x += current.x + current.offsetX;
      y += current.y + current.offsetY;
      area = current.group;
      at = current.parent;
    }
    nodes_[i].absX = x;
    nodes_[i].absY = y;
    nodes_[i].area = area;
  }
}

void Builder::setMapView(MapView view) {
  for (Node& node : nodes_) {
    if (node.type == NodeType::Map || node.type == NodeType::MiniMap) {
      useMapView(node, view);
    }
  }
  // The node's rectangle changed — the resolved coordinates have to be recomputed.
  finish();
}

void Builder::setMapRect(float x, float y, float width, float height, MapView shape) {
  for (Node& node : nodes_) {
    if (node.type != NodeType::Map && node.type != NodeType::MiniMap) continue;
    node.x = x;
    node.y = y;
    node.width = width;
    node.height = height;
    node.mapView = shape;
  }
  finish();
}

void useMapView(Node& node, MapView view) {
  const MapRect& rect = view == MapView::Maxi        ? node.mapMaxi
                        : view == MapView::Commander ? node.mapCommander
                                                     : node.mapMini;
  if (!rect.set) return;
  // The map is the only node whose coordinates are counted **from the screen's
  // centre** rather than from the parent. Hence the negatives. It agrees three times over:
  //
  //   mini      197/-300 197x197 -> (597, 0),   and the MapFrame frame (596, 0) 200x212
  //   maxi     -122/-273 512x512 -> (278, 27)
  //   commander -161/-281 561x561 -> (239, 19), the right edge exactly 800
  //
  // So the minimap lands in its frame with a one-pixel margin, while the
  // commander's runs into the screen's edge. Until now we took these numbers as
  // they were, and the map went past the top edge.
  node.x = kReferenceWidth * 0.5f + rect.x;
  node.y = kReferenceHeight * 0.5f + rect.y;
  node.width = rect.width;
  node.height = rect.height;
  node.mapView = view;
}

void Builder::feed(const con::Command& command) {
  if (command.path.empty()) return;
  if (command.lowerPath.rfind("hudbuilder.", 0) != 0) return;

  const std::string_view method = std::string_view(command.lowerPath).substr(11);

  // --- creating nodes ---
  // How many arguments stand between the name and the rectangle — taken from the
  // game's data (tools/hud_audit.py):
  //
  //   createPictureNode  <group> <name> <x> <y> <w> <h>
  //   createBarNode      <group> <name> <kind> <x> <y> <w> <h>
  //   createCompassNode  <group> <name> <kind> <x> <y> <w> <h> <flag> <flag>
  //   createOccupiedNode <group> <name> <?> <x> <y> <w> <h>
  //   createSliderNode   <group> <name> <min> <max> <value> <step>   — no rectangle
  //   createMapNode      <group> <name>                             — none either
  //
  // That is exactly why the compass drove out half a screen: we read its
  // rectangle one position early, and instead of 186x32 got 165x186.
  if (method == "newlayer") {
    ++layer_;
    return;
  }

  auto create = [&](NodeType type) {
    Node node;
    node.type = type;
    node.layer = layer_;
    const bool shifted = type == NodeType::Bar || type == NodeType::Compass ||
                         type == NodeType::Occupied;
    const int skip = shifted ? 1 : 0;
    if (type == NodeType::Bar) node.barKind = command.argInt(2).value_or(0);
    // createListNode <parent> <name> <x> <y> <w> <h> <row height> <?>
    if (type == NodeType::List) node.listRowHeight = command.argFloat(6).value_or(0.0f);
    // A slider and the map carry no rectangle: a slider has its bounds and step there.
    if (type == NodeType::Slider || type == NodeType::Map) {
      if (command.args.size() >= 2) {
        node.group = command.args[0];
        node.name = command.args[1];
        nodes_.push_back(std::move(node));
        activeIndex_ = static_cast<int>(nodes_.size()) - 1;
      }
      return;
    }
    if (!readRect(command, node, skip)) return;
    nodes_.push_back(std::move(node));
    activeIndex_ = static_cast<int>(nodes_.size()) - 1;
  };

  if (method == "createpicturenode") { create(NodeType::Picture); return; }
  if (method == "createtextnode") { create(NodeType::Text); return; }
  if (method == "createbuttonnode") { create(NodeType::Button); return; }
  if (method == "createsplitnode") { create(NodeType::Split); return; }
  if (method == "createbarnode") { create(NodeType::Bar); return; }
  if (method == "createobjectmarkernode") { create(NodeType::ObjectMarker); return; }
  if (method == "createcompassnode") { create(NodeType::Compass); return; }
  // DICE also has the typo "Transfom" — it is the same command.
  if (method == "createtransformlistnode" || method == "createtransfomlistnode") {
    create(NodeType::TransformList);
    return;
  }
  // Nodes we do not draw yet, but with a type of their own: otherwise they all
  // clump into "other" and the tree does not show what is missing.
  if (method == "createtransformnode") { create(NodeType::TransformList); return; }
  if (method == "createlistnode") { create(NodeType::List); return; }
  if (method == "createeditnode") { create(NodeType::Edit); return; }
  if (method == "createhovernode") { create(NodeType::Hover); return; }
  if (method == "createoccupiednode") { create(NodeType::Occupied); return; }
  if (method == "createslidernode") { create(NodeType::Slider); return; }
  if (method == "createobjectselectionnode") { create(NodeType::ObjectSelection); return; }
  if (method == "createminimapnode") { create(NodeType::MiniMap); return; }
  if (method == "createmapnode") { create(NodeType::Map); return; }
  if (method.rfind("create", 0) == 0) { create(NodeType::Other); return; }

  // setActiveObject switches which node the following commands go to.
  // Without it the properties would settle on the last one created — and part of
  // the interface would be assembled wrongly.
  if (method == "setactiveobject") {
    // setActiveObject <group> <name>. The name is the last argument: in the
    // one-argument form the group is simply not given.
    const std::string_view name = command.args.empty()
                                      ? std::string_view()
                                      : std::string_view(command.args.back());
    const std::string_view group =
        command.args.size() >= 2 ? std::string_view(command.args.front()) : std::string_view();
    for (std::size_t i = nodes_.size(); i-- > 0;) {
      if (!group.empty() && group != "Global" && nodes_[i].group != group) continue;
      if (nodes_[i].name == name) {
        activeIndex_ = static_cast<int>(i);
        return;
      }
    }
    ++unknown_;
    ++unknownByName_["setactiveobject (no such node)"];
    return;
  }

  // --- everything else applies to the active node ---
  Node* node = active();
  if (node == nullptr) {
    ++unknown_;
    ++unknownByName_[std::string(method)];
    return;
  }

  // A node's rectangle can be changed after creation too — used where one
  // description is fitted to several places.
  if (method == "setnodepos") {
    node->x = command.argFloat(0).value_or(node->x);
    node->y = command.argFloat(1).value_or(node->y);
    return;
  }
  if (method == "setnodesize") {
    node->width = command.argFloat(0).value_or(node->width);
    node->height = command.argFloat(1).value_or(node->height);
    return;
  }
  if (method == "setbarnodesnapdir") {
    node->barSnapDir = command.argInt(0).value_or(0);
    return;
  }
  if (method == "setpicturenoderotation") {
    node->rotation = command.argFloat(0).value_or(0.0f);
    return;
  }

  if (method == "setbarnodetexture") {
    // setBarNodeTexture <0|1> <file>: texture zero is the empty bar, the first the
    // full one. We draw the full one, clipped by the value.
    const int slot = command.argInt(0).value_or(0);
    std::string path(command.argStr(1));
    if (slot == 0) node->barTextureEmpty = path;
    else node->barTextureFull = path;
    if (node->texture.empty() || slot == 1) node->texture = std::move(path);
    return;
  }
  if (method == "setpicturenodetexture" || method == "setobjectmarkernodetexture") {
    node->texture = std::string(command.argStr(0));
    return;
  }
  // A button has a **state** before the path:
  //
  //   setButtonNodeTexture 1 Ingame/Respawn/kit_selected.tga   at rest
  //   setButtonNodeTexture 2 Ingame/GeneralIcons/empty.tga     under the cursor
  //
  // We used to take the first argument — and the button's texture became the
  // number "1" or "2", which of course does not exist.
  if (method == "setbuttonnodetexture") {
    const int state = command.argInt(0).value_or(1);
    std::string path(command.argStr(1));
    if (path.empty()) {
      // The short form, without a state, occurs too.
      path = std::string(command.argStr(0));
      node->texture = path;
      return;
    }
    if (state == 2) {
      node->hoverTexture = std::move(path);
    } else {
      node->texture = std::move(path);
    }
    return;
  }
  if (method == "setpicturenodevariabletexture") {
    node->textureVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "settextnodestring") {
    node->text = std::string(command.argStr(0));
    return;
  }
  if (method == "settextnodestringvariable") {
    node->textVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "settextnodestyle") {
    node->textAlign = command.argInt(1).value_or(0);
    node->style = std::string(command.argStr(0));
    return;
  }
  if (method == "setnodeshowvariable") {
    node->showVariable = std::string(command.argStr(0));
    return;
  }
  // `setNodeLogicShowVariable NOT DisconnectMessageActive 1` is an action, a
  // variable and a value, not one name. The data holds only four actions, all
  // with three arguments.
  if (method == "setnodelogicshowvariable") {
    if (command.args.size() >= 3) {
      ShowTest test;
      test.op = std::string(command.argStr(0));
      test.variable = std::string(command.argStr(1));
      test.value = command.argFloat(2).value_or(1.0f);
      node->showTests.push_back(std::move(test));
    }
    return;
  }
  if (method == "setnodealphavariable") {
    node->alphaVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "setbuttonnodeconcmd") {
    // A button runs console commands — that is how the interface drives the game.
    // The form is `setButtonNodeConCmd "<command>" <event>`, where event 0 is a
    // click and 1 a hover. One button has several of them:
    //
    //   setButtonNodeConCmd "spawnManager.setPlayerKit 1" 0
    //   setButtonNodeConCmd "sound.playSound kitSelect"   0
    //   setButtonNodeConCmd "sound.playSound kitOver"     1
    //
    // Until now we kept the last — and the kit selection button "ran" the hover
    // sound instead of the selection.
    if (command.args.empty()) return;
    const std::size_t last = command.args.size() - 1;
    int event = 0;
    std::size_t upto = command.args.size();
    if (last > 0 && command.args[last].size() <= 2 &&
        command.args[last].find_first_not_of("0123456789") == std::string::npos) {
      event = std::atoi(command.args[last].c_str());
      upto = last;
    }
    std::string joined;
    for (std::size_t i = 0; i < upto; ++i) {
      if (!joined.empty()) joined += " ";
      joined += command.args[i];
    }
    if (joined.empty()) return;
    node->commands.emplace_back(event, joined);
    // `command` stays the first click action — the button counts as active by it.
    // Events 0 and 3 belong to a click.
    if ((event == 0 || event == 3) && node->command.empty()) node->command = joined;
    return;
  }
  if (method == "setnodecolor") {
    // In the files a colour occurs both as 0..1 and as 0..255.
    float values[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool overOne = false;
    for (std::size_t i = 0; i < 4; ++i) {
      if (const auto value = command.argFloat(i)) {
        values[i] = *value;
        if (*value > 1.0f) overOne = true;
      }
    }
    const float scale = overOne ? 1.0f / 255.0f : 1.0f;
    node->color = Color{values[0] * scale, values[1] * scale, values[2] * scale,
                        overOne ? values[3] * scale : values[3]};
    return;
  }
  if (method == "setnodeintime") {
    node->inTime = command.argFloat(0).value_or(0.0f);
    return;
  }
  if (method == "setnodeouttime") {
    node->outTime = command.argFloat(0).value_or(0.0f);
    return;
  }

  // --- show effects ---
  if (method == "addnodealphashoweffect") {
    node->showEffects.push_back(ShowEffectInfo{ShowEffect::Alpha, 0.0f, 0.0f});
    return;
  }
  // `addNodeMoveShowEffect <direction> <distance>`. DICE also has the typo
  // "addNoDemoveShowEffect" (10 times), but lower-cased it coincides with the
  // correct name, so no separate branch is needed.
  if (method == "addnodemoveshoweffect") {
    node->showEffects.push_back(ShowEffectInfo{ShowEffect::Move,
                                               command.argFloat(0).value_or(0.0f),
                                               command.argFloat(1).value_or(0.0f)});
    return;
  }
  if (method == "addnodeblendeffect") {
    node->showEffects.push_back(ShowEffectInfo{ShowEffect::Blend, 0.0f, 0.0f});
    return;
  }

  // --- transform lists: a node becomes another's child ---
  if (method == "addtransformlistnode" || method == "addtransfomlistnode") {
    node->children.emplace_back(command.argStr(0));
    return;
  }
  // setTranformListNodeOffset <x> <y> — the step between neighbouring items of a
  // list. Without it every item lands on top of the others.
  if (method == "settranformlistnodeoffset" || method == "settransformlistnodeoffset") {
    node->childOffsetX = command.argFloat(0).value_or(0.0f);
    node->childOffsetY = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "settranformlistnodeposvariable" || method == "setnodeposvariable") {
    // The command's name in the data is exactly this, with the typo in "tranform".
    // The arguments: the **axis** (0 = X, 1 = Y) and the variable's name.
    const int axis = command.argInt(0).value_or(0);
    std::string name(command.argStr(1));
    if (axis == 0) {
      node->positionVariableX = std::move(name);
    } else {
      node->positionVariableY = std::move(name);
    }
    return;
  }

  // --- buttons ---
  if (method == "setbuttonnodealtconcmd") {
    std::string joined;
    for (const std::string& argument : command.args) {
      if (!joined.empty()) joined += " ";
      joined += argument;
    }
    node->altCommand = joined;
    return;
  }
  if (method == "setbuttonnodemousearea") {
    node->hasMouseArea = true;
    node->mouseX = command.argFloat(0).value_or(0.0f);
    node->mouseY = command.argFloat(1).value_or(0.0f);
    node->mouseWidth = command.argFloat(2).value_or(0.0f);
    node->mouseHeight = command.argFloat(3).value_or(0.0f);
    return;
  }
  if (method == "setbuttonnodedebug") return;  // for the editor only

  // --- bars, markers, the compass ---
  if (method == "setbarnodevaluevariable") {
    node->valueVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "setobjectmarkernodetexturesize") {
    node->textureWidth = command.argFloat(0).value_or(0.0f);
    node->textureHeight = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "setcompassnodetexture") {
    node->texture = std::string(command.argStr(0));
    return;
  }
  // The pair "x/y" as one word — that is how every map size is written.
  auto pair = [&](std::size_t i, float& first, float& second) {
    if (i >= command.args.size()) return false;
    const std::string& text = command.args[i];
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos) {
      // The split form occurs too: "197 197".
      first = command.argFloat(i).value_or(0.0f);
      second = command.argFloat(i + 1).value_or(0.0f);
      return true;
    }
    first = std::strtof(text.substr(0, slash).c_str(), nullptr);
    second = std::strtof(text.substr(slash + 1).c_str(), nullptr);
    return true;
  };
  auto mapPos = [&](MapRect& rect) {
    if (!pair(0, rect.x, rect.y)) return;
    rect.set = true;
    // Until told otherwise the map is the thumbnail in the corner: that is how it
    // looks during ordinary combat. The spawn screen switches to the big one itself.
    if (&rect == &node->mapMini) useMapView(*node, MapView::Mini);
  };
  auto mapSize = [&](MapRect& rect) {
    if (!pair(0, rect.width, rect.height)) return;
    rect.set = true;
    if (&rect == &node->mapMini) useMapView(*node, MapView::Mini);
  };
  auto readColor = [&](Color& out) {
    out.r = command.argFloat(0).value_or(1.0f);
    out.g = command.argFloat(1).value_or(1.0f);
    out.b = command.argFloat(2).value_or(1.0f);
    out.a = command.argFloat(3).value_or(1.0f);
  };
  // --- the rest of hudBuilder's commands -----------------------------
  //
  // The forms are taken from the game's data (the argument count in brackets).
  // A few of DICE's names have typos — "Transfom" and "Tranform" instead of
  // "Transform"; they are the same commands, so the handler is the same too.
  if (method == "setlistnodeselectcolor") { readColor(node->listSelectColor); return; }
  if (method == "setlistnodescrollbarcolor") { readColor(node->listScrollbarColor); return; }
  if (method == "setlistnodescrollbarbackgroundcolor") {
    readColor(node->listScrollbarBackground);
    return;
  }
  if (method == "setlistnodescrollbar") {
    node->listScrollbarWidth = command.argFloat(0).value_or(0.0f);
    node->listScrollbarGap = command.argFloat(1).value_or(0.0f);
    node->hasListScrollbar = true;
    return;
  }
  if (method == "setlistnodedata") {
    node->listData = command.argInt(0).value_or(-1);
    return;
  }
  if (method == "setlistnoderowspacing") {
    node->listRowSpacing = command.argFloat(0).value_or(0.0f);
    return;
  }
  if (method == "setlistnodeoutline") {
    node->listOutline = command.argInt(0).value_or(0) != 0;
    return;
  }
  if (method == "setlistnodeconcmd") {
    node->listCommands.emplace_back(command.argInt(0).value_or(0),
                                    std::string(command.argStr(1)));
    return;
  }
  if (method == "seteditnodefont") { node->editFont = std::string(command.argStr(0)); return; }
  if (method == "seteditnodedata") { node->editData = command.argInt(0).value_or(-1); return; }
  if (method == "seteditnodestring") { node->editString = command.argInt(0).value_or(-1); return; }
  if (method == "seteditnodemaxlength") {
    node->editMaxLength = command.argInt(0).value_or(0);
    return;
  }
  if (method == "seteditnodecolor") {
    readColor(node->editColor);
    node->hasEditColor = true;
    return;
  }
  if (method == "setobjectmarkernodelockontype") {
    node->markerLockOnType = command.argInt(0).value_or(0);
    return;
  }
  if (method == "setobjectmarkernodeweapon") {
    node->markerWeapon = command.argInt(0).value_or(0);
    return;
  }
  if (method == "setobjectmarkernodelocktext") {
    node->markerLockText = std::string(command.argStr(1));
    return;
  }
  if (method == "setobjectmarkernodelocktextoffset") {
    node->markerLockTextOffset[0] = command.argFloat(0).value_or(0.0f);
    node->markerLockTextOffset[1] = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "setoccupiednodedata") {
    node->occupiedData = command.argInt(0).value_or(-1);
    return;
  }
  if (method == "setoccupiednodeposvariable") {
    const std::size_t slot = static_cast<std::size_t>(command.argInt(0).value_or(0));
    if (node->occupiedPosVariables.size() <= slot) node->occupiedPosVariables.resize(slot + 1);
    node->occupiedPosVariables[slot] = std::string(command.argStr(1));
    return;
  }
  if (method == "setcompassnodesnapoffset") {
    for (int i = 0; i < 4; ++i) {
      node->compassSnapOffset[i] = command.argFloat(static_cast<std::size_t>(i)).value_or(0.0f);
    }
    return;
  }
  if (method == "setcompassnodesnaptexture") {
    const std::size_t slot = static_cast<std::size_t>(command.argInt(0).value_or(0));
    if (node->compassSnapTextures.size() <= slot) node->compassSnapTextures.resize(slot + 1);
    node->compassSnapTextures[slot] = std::string(command.argStr(1));
    return;
  }
  if (method == "sethoverinmiddlepos") {
    node->hoverMiddle[0] = command.argFloat(0).value_or(0.0f);
    node->hoverMiddle[1] = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "sethovermaxvalue") {
    node->hoverMaxValue = command.argFloat(0).value_or(0.0f);
    return;
  }
  if (method == "sethoverwidthlength") {
    node->hoverWidth = command.argFloat(0).value_or(0.0f);
    node->hoverLength = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "setslidernodechild") { node->sliderChild = std::string(command.argStr(0)); return; }
  if (method == "setslidernodedata") { node->sliderData = std::string(command.argStr(0)); return; }
  if (method == "settextnodeoutline") {
    node->outlineFont = std::string(command.argStr(0));
    return;
  }
  if (method == "settextnodeoutlineoffset") {
    node->outlineOffset[0] = command.argFloat(0).value_or(0.0f);
    node->outlineOffset[1] = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "setobjectselectionnodepointersize") {
    node->pointerSize[0] = command.argFloat(0).value_or(0.0f);
    node->pointerSize[1] = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "setzoomicons") { node->zoomIcons = command.argInt(0).value_or(0); return; }
  if (method == "setcpfont") { node->cpFont = std::string(command.argStr(0)); return; }
  if (method == "setcpfontcolor") { readColor(node->cpFontColor); return; }

  if (method == "setlistnodebackgroundcolor") {
    readColor(node->listBackground);
    node->hasListBackground = true;
    return;
  }
  if (method == "setlistnodebordercolor") {
    readColor(node->listBorderColor);
    node->hasListBorder = true;
    return;
  }
  if (method == "setlistnodeborder") {
    for (int i = 0; i < 4; ++i) {
      node->listBorder[i] = command.argFloat(static_cast<std::size_t>(i)).value_or(0.0f);
    }
    return;
  }
  if (method == "setmaxipos") { mapPos(node->mapMaxi); return; }
  if (method == "setmaxisize") { mapSize(node->mapMaxi); return; }
  if (method == "setminipos") { mapPos(node->mapMini); return; }
  if (method == "setminisize") { mapSize(node->mapMini); return; }
  if (method == "setcommanderpos") { mapPos(node->mapCommander); return; }
  if (method == "setcommandersize") { mapSize(node->mapCommander); return; }

  if (method == "setcompassnodeoffset" || method == "setnodeoffset") {
    node->offsetX = command.argFloat(0).value_or(0.0f);
    node->offsetY = command.argFloat(1).value_or(0.0f);
    return;
  }

  // --- geometry and a picture's variables ---
  if (method == "setpicturenodecenterpoint") {
    node->centerX = command.argFloat(0).value_or(0.0f);
    node->centerY = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "setpicturenoderotatevariable") {
    node->rotateVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "addobjectmarkernodelocktextnode") {
    // The engine creates a node of its own for the caption, named
    // "<caption>TextNode" (visible in BF2_r.exe: exactly that string is appended
    // to the name). The description then switches to it through setActiveObject,
    // so without such a node the following commands would settle in the wrong place.
    const std::string label(command.argStr(0));
    node->lockTextNode = label;
    Node child;
    child.type = NodeType::Text;
    child.group = node->group;
    child.name = label + "TextNode";
    nodes_.push_back(std::move(child));
    return;
  }
  if (method == "setobjectmarkernodeobjects") {
    node->markerObjects.clear();
    for (const std::string& argument : command.args) node->markerObjects.push_back(argument);
    return;
  }
  if (method == "setcompassnodevaluevariable") {
    node->valueVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "setcompassnodetexturesize") {
    node->textureWidth = command.argFloat(0).value_or(0.0f);
    node->textureHeight = command.argFloat(1).value_or(0.0f);
    return;
  }
  if (method == "setcompassnodeborder" || method == "setbarnodeborder") {
    node->borderSize = command.argFloat(0).value_or(0.0f);
    return;
  }
  if (method == "setbarnodesnap") {
    node->barSnap = command.argFloat(0).value_or(0.0f);
    return;
  }
  if (method == "setlistnodefont" || method == "settextnodefont" ||
      method == "setbuttonnodefont") {
    node->font = std::string(command.argStr(0));
    return;
  }
  if (method == "setpicturenodebordercolor") {
    float values[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool overOne = false;
    for (std::size_t i = 0; i < 4; ++i) {
      if (const auto value = command.argFloat(i)) {
        values[i] = *value;
        if (*value > 1.0f) overOne = true;
      }
    }
    const float scale = overOne ? 1.0f / 255.0f : 1.0f;
    node->borderColor = Color{values[0] * scale, values[1] * scale, values[2] * scale,
                              overOne ? values[3] * scale : values[3]};
    return;
  }

  if (method == "setnodergbvariables") {
    node->rgbVariables.clear();
    for (const std::string& argument : command.args) node->rgbVariables.push_back(argument);
    return;
  }

  // The commands we recognise but do not draw yet: the arguments are put into the
  // node as they are. The table was made from the game's data — tools/hud_audit.py.
  static const std::map<std::string_view, int> recorded = {
#define HUD_RECORDED(name, count) {name, count},
#include "hud_recorded.inc"
#undef HUD_RECORDED
  };
  if (const auto found = recorded.find(method); found != recorded.end()) {
    node->extra[std::string(method)] = command.args;
    // The argument count is cross-checked against the game's data. A mismatch
    // means the command is called differently than we thought — worth knowing.
    if (static_cast<int>(command.args.size()) != found->second) {
      ++unknownByName_[std::string(method) + " (unexpected argument count)"];
    }
    return;
  }

  ++unknown_;
  ++unknownByName_[std::string(method)];
}

std::vector<const Node*> Builder::group(std::string_view name) const {
  std::vector<const Node*> found;
  for (const Node& node : nodes_) {
    if (node.group == name) found.push_back(&node);
  }
  return found;
}

std::vector<std::string> Builder::groups() const {
  std::vector<std::string> names;
  for (const Node& node : nodes_) {
    if (std::find(names.begin(), names.end(), node.group) == names.end()) {
      names.push_back(node.group);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace obf2::hud
