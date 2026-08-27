#include "obf2/hud/hud.h"

#include <algorithm>

namespace obf2::hud {
namespace {

// createXxxNode <група> <ім'я> <x> <y> <ширина> <висота>
bool readRect(const con::Command& command, Node& node, int skip = 0) {
  if (command.args.size() < 2) return false;
  node.group = command.args[0];
  node.name = command.args[1];
  // У смуги перед прямокутником стоїть ще один аргумент — напрям росту,
  // тож координати зсунуті на одну позицію.
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
    const int skip = type == NodeType::Bar ? 1 : 0;
    if (type == NodeType::Bar) node.barDirection = command.argInt(2).value_or(0);
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
  if (method == "createtransformlistnode") { create(NodeType::TransformList); return; }
  // Вузли, які ми поки не малюємо, але тип у них свій: інакше вони всі
  // злипаються в «інше» і по дереву не видно, чого бракує.
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

  // setActiveObject перемикає, до якого вузла йдуть наступні команди.
  // Без нього властивості осідали б на останньому створеному — і частина
  // інтерфейсу збиралася б неправильно.
  if (method == "setactiveobject") {
    // setActiveObject <група> <ім'я>. Ім'я — останній аргумент: у формі з
    // одним аргументом група просто не вказана.
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

  // Прямокутник вузла можна змінити й після створення — цим користуються
  // там, де один опис підганяють під кілька місць.
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
    // setBarNodeTexture <0|1> <файл>: нульова текстура — порожня смуга,
    // перша — повна. Малюємо повну, обрізану за значенням.
    const int slot = command.argInt(0).value_or(0);
    std::string path(command.argStr(1));
    if (slot == 0) node->barTextureEmpty = path;
    else node->barTextureFull = path;
    if (node->texture.empty() || slot == 1) node->texture = std::move(path);
    return;
  }
  if (method == "setpicturenodetexture" || method == "setbuttonnodetexture" ||
      method == "setobjectmarkernodetexture") {
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
    // Рушій заводить під підпис власний вузол з іменем «<підпис>TextNode»
    // (видно в BF2_r.exe: до імені дописується саме цей рядок). Далі опис
    // на нього перемикається через setActiveObject, тож без такого вузла
    // наступні команди осідали б не там.
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

  // Команди, які ми впізнаємо, але ще не малюємо: аргументи кладемо у
  // вузол як є. Таблиця створена з даних гри — tools/hud_audit.py.
  static const std::map<std::string_view, int> recorded = {
#define HUD_RECORDED(name, count) {name, count},
#include "hud_recorded.inc"
#undef HUD_RECORDED
  };
  if (const auto found = recorded.find(method); found != recorded.end()) {
    node->extra[std::string(method)] = command.args;
    // Кількість аргументів звірена з даними гри. Розбіжність означає, що
    // команду викликають інакше, ніж ми думали, — про це варто знати.
    if (static_cast<int>(command.args.size()) != found->second) {
      ++unknownByName_[std::string(method) + " (несподівано аргументів)"];
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
