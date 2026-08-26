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
    case NodeType::Other: return "other";
  }
  return "?";
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
  };

  if (method == "createpicturenode") { create(NodeType::Picture); return; }
  if (method == "createtextnode") { create(NodeType::Text); return; }
  if (method == "createbuttonnode") { create(NodeType::Button); return; }
  if (method == "createsplitnode") { create(NodeType::Split); return; }
  if (method == "createbarnode") { create(NodeType::Bar); return; }
  if (method == "createobjectmarkernode") { create(NodeType::ObjectMarker); return; }
  if (method.rfind("create", 0) == 0) { create(NodeType::Other); return; }

  // --- усе інше застосовується до останнього створеного вузла ---
  Node* node = active();
  if (node == nullptr) {
    ++unknown_;
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

  ++unknown_;
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
