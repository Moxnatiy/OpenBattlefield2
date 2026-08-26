#include "obf2/hud/hud.h"

#include <algorithm>

namespace obf2::hud {
namespace {

// createXxxNode <група> <ім'я> <x> <y> <ширина> <висота>
bool readRect(const con::Command& command, Node& node) {
  if (command.args.size() < 2) return false;
  node.group = command.args[0];
  node.name = command.args[1];
  node.x = command.argFloat(2).value_or(0.0f);
  node.y = command.argFloat(3).value_or(0.0f);
  node.width = command.argFloat(4).value_or(0.0f);
  node.height = command.argFloat(5).value_or(0.0f);
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
    case NodeType::Other: return "other";
  }
  return "?";
}

Node* Builder::active() {
  // Активним є або той, що вибрали через setActiveObject, або останній
  // створений.
  if (activeIndex_ >= 0 && activeIndex_ < static_cast<int>(nodes_.size())) {
    return &nodes_[static_cast<std::size_t>(activeIndex_)];
  }
  return nodes_.empty() ? nullptr : &nodes_.back();
}

void Builder::feed(const con::Command& command) {
  if (command.path.empty()) return;
  if (command.lowerPath.rfind("hudbuilder.", 0) != 0) return;

  const std::string_view method = std::string_view(command.lowerPath).substr(11);

  // --- створення вузлів ---
  auto create = [&](NodeType type) {
    Node node;
    node.type = type;
    if (!readRect(command, node)) return;
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
  if (method == "createtransformlistnode") { create(NodeType::TransformList); return; }
  if (method.rfind("create", 0) == 0) { create(NodeType::Other); return; }

  // setActiveObject перемикає, до якого вузла йдуть наступні команди.
  // Без нього властивості осідали б на останньому створеному — і частина
  // інтерфейсу збиралася б неправильно.
  if (method == "setactiveobject") {
    const std::string_view name = command.argStr(0);
    for (std::size_t i = nodes_.size(); i-- > 0;) {
      if (nodes_[i].name == name) {
        activeIndex_ = static_cast<int>(i);
        return;
      }
    }
    ++unknown_;
    ++unknownByName_["setactiveobject (немає такого вузла)"];
    return;
  }

  // --- усе інше застосовується до активного вузла ---
  Node* node = active();
  if (node == nullptr) {
    ++unknown_;
    ++unknownByName_[std::string(method)];
    return;
  }

  if (method == "setpicturenodetexture" || method == "setbuttonnodetexture" ||
      method == "setbarnodetexture" || method == "setobjectmarkernodetexture") {
    node->texture = std::string(command.argStr(0));
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
    node->style = std::string(command.argStr(0));
    return;
  }
  if (method == "setnodeshowvariable" || method == "setnodelogicshowvariable") {
    node->showVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "setnodealphavariable") {
    node->alphaVariable = std::string(command.argStr(0));
    return;
  }
  if (method == "setbuttonnodeconcmd") {
    // Кнопка виконує консольну команду — так інтерфейс і керує грою.
    std::string joined;
    for (const std::string& argument : command.args) {
      if (!joined.empty()) joined += " ";
      joined += argument;
    }
    node->command = joined;
    return;
  }
  if (method == "setnodecolor") {
    // Колір у файлах трапляється і як 0..1, і як 0..255.
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

  // --- ефекти появи ---
  if (method == "addnodealphashoweffect") {
    node->showEffects.push_back(ShowEffect::Alpha);
    return;
  }
  if (method == "addnodemoveshoweffect") {
    node->showEffects.push_back(ShowEffect::Move);
    return;
  }
  if (method == "addnodeblendeffect") {
    node->showEffects.push_back(ShowEffect::Blend);
    return;
  }

  // --- списки трансформацій: вузол стає нащадком іншого ---
  if (method == "addtransformlistnode") {
    node->children.emplace_back(command.argStr(0));
    return;
  }
  if (method == "settranformlistnodeposvariable" || method == "setnodeposvariable") {
    // Ім'я команди в іграх саме таке, з опискою в "tranform".
    node->positionVariable = std::string(command.argStr(0));
    return;
  }

  // --- кнопки ---
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
  if (method == "setbuttonnodedebug") return;  // лише для редактора

  // --- смуги, маркери, компас ---
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
  if (method == "setcompassnodeoffset" || method == "setnodeoffset") {
    node->offsetX = command.argFloat(0).value_or(0.0f);
    node->offsetY = command.argFloat(1).value_or(0.0f);
    return;
  }

  // --- геометрія й змінні картинки ---
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
    node->lockTextNode = std::string(command.argStr(0));
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
    node->snap = command.argBool(0).value_or(false);
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
