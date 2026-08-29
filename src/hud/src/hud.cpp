#include <cctype>
#include <cstdlib>
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

namespace {

std::string lowered(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

void Builder::finish() {
  // Батька шукаємо за іменем: у даних гри він тільки так і вказаний.
  std::map<std::string, int> byName;
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    byName.emplace(lowered(nodes_[i].name), static_cast<int>(i));
  }
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const auto found = byName.find(lowered(nodes_[i].group));
    // Ділянка (Global, BottomLeftStatic…) вузлом не є, тож не знайдеться
    // — це і означає корінь. Сам на себе вузол теж не батько.
    nodes_[i].parent =
        (found != byName.end() && found->second != static_cast<int>(i)) ? found->second : -1;
  }
  // Абсолютне положення — сума по предках. Лічильник кроків рятує від
  // кільця: дані гри його не мають, але мод може.
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    float x = 0.0f;
    float y = 0.0f;
    std::string area = nodes_[i].group;
    int at = static_cast<int>(i);
    for (int step = 0; at >= 0 && step < 64; ++step) {
      const Node& current = nodes_[static_cast<std::size_t>(at)];
      // setNodeOffset зсуває вузол разом із його дітьми, тож входить
      // у суму нарівні з x/y.
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

void useMapView(Node& node, MapView view) {
  const MapRect& rect = view == MapView::Maxi        ? node.mapMaxi
                        : view == MapView::Commander ? node.mapCommander
                                                     : node.mapMini;
  if (!rect.set) return;
  node.x = rect.x;
  node.y = rect.y;
  node.width = rect.width;
  node.height = rect.height;
}

void Builder::feed(const con::Command& command) {
  if (command.path.empty()) return;
  if (command.lowerPath.rfind("hudbuilder.", 0) != 0) return;

  const std::string_view method = std::string_view(command.lowerPath).substr(11);

  // --- створення вузлів ---
  // Скільки аргументів стоїть між іменем і прямокутником — знято з даних
  // гри (tools/hud_audit.py):
  //
  //   createPictureNode  <група> <ім'я> <x> <y> <ш> <в>
  //   createBarNode      <група> <ім'я> <напрям> <x> <y> <ш> <в>
  //   createCompassNode  <група> <ім'я> <вид> <x> <y> <ш> <в> <прапорець> <прапорець>
  //   createOccupiedNode <група> <ім'я> <?> <x> <y> <ш> <в>
  //   createSliderNode   <група> <ім'я> <мін> <макс> <значення> <крок>   — без прямокутника
  //   createMapNode      <група> <ім'я>                                  — теж без
  //
  // Компас через це й виїжджав на пів екрана: ми читали його прямокутник
  // на одну позицію раніше, і замість 186x32 виходило 165x186.
  auto create = [&](NodeType type) {
    Node node;
    node.type = type;
    const bool shifted = type == NodeType::Bar || type == NodeType::Compass ||
                         type == NodeType::Occupied;
    const int skip = shifted ? 1 : 0;
    if (type == NodeType::Bar) node.barDirection = command.argInt(2).value_or(0);
    // Повзунок і карта прямокутника не несуть: у повзунка там межі й крок.
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
  if (method == "setnodeshowvariable") {
    node->showVariable = std::string(command.argStr(0));
    return;
  }
  // `setNodeLogicShowVariable NOT DisconnectMessageActive 1` — це дія,
  // змінна і значення, а не одне ім'я. У даних лише чотири дії, і всі з
  // трьома аргументами.
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
  // Пара «x/y» одним словом — так записані всі розміри карти.
  auto pair = [&](std::size_t i, float& first, float& second) {
    if (i >= command.args.size()) return false;
    const std::string& text = command.args[i];
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos) {
      // Трапляється й розділена форма: «197 197».
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
    // Поки не сказано інакше, карта — мініатюра в кутку: це її вигляд
    // під час звичайного бою. Екран появи перемикає на велику сам.
    if (&rect == &node->mapMini) useMapView(*node, MapView::Mini);
  };
  auto mapSize = [&](MapRect& rect) {
    if (!pair(0, rect.width, rect.height)) return;
    rect.set = true;
    if (&rect == &node->mapMini) useMapView(*node, MapView::Mini);
  };
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
