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

void Builder::setMapView(MapView view) {
  for (Node& node : nodes_) {
    if (node.type == NodeType::Map || node.type == NodeType::MiniMap) {
      useMapView(node, view);
    }
  }
  // Прямокутник вузла змінився — зведені координати треба перерахувати.
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
  // Карта — єдиний вузол, чиї координати відлічені **від центра екрана**,
  // а не від батька. Тому в даних вони від'ємні. Сходиться відразу тричі:
  //
  //   mini      197/-300 197x197 -> (597, 0),   а рамка MapFrame (596, 0) 200x212
  //   maxi     -122/-273 512x512 -> (278, 27)
  //   commander -161/-281 561x561 -> (239, 19), правий край рівно 800
  //
  // Тобто мінікарта лягає в свою рамку з полем в один піксель, а
  // командирська впирається в край екрана. Доти ми брали ці числа як є, і
  // карта йшла за верхній край.
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
    if (type == NodeType::Bar) node.barDirection = command.argInt(2).value_or(0);
    // createListNode <батько> <ім'я> <x> <y> <ш> <в> <висота рядка> <?>
    if (type == NodeType::List) node.listRowHeight = command.argFloat(6).value_or(0.0f);
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
  // У DICE трапляється й опечатка «Transfom» — це та сама команда.
  if (method == "createtransformlistnode" || method == "createtransfomlistnode") {
    create(NodeType::TransformList);
    return;
  }
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
  if (method == "setpicturenodetexture" || method == "setobjectmarkernodetexture") {
    node->texture = std::string(command.argStr(0));
    return;
  }
  // У кнопки перед шляхом стоїть **стан**:
  //
  //   setButtonNodeTexture 1 Ingame/Respawn/kit_selected.tga   спокій
  //   setButtonNodeTexture 2 Ingame/GeneralIcons/empty.tga     під курсором
  //
  // Ми ж брали перший аргумент — і текстурою кнопки ставало саме число
  // «1» або «2», якого, звісно, немає.
  if (method == "setbuttonnodetexture") {
    const int state = command.argInt(0).value_or(1);
    std::string path(command.argStr(1));
    if (path.empty()) {
      // Трапляється й коротка форма, без стану.
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
    // Кнопка виконує консольні команди — так інтерфейс і керує грою.
    // Форма: `setButtonNodeConCmd "<команда>" <подія>`, де подія 0 це
    // натискання, а 1 — наведення. На одній кнопці їх кілька:
    //
    //   setButtonNodeConCmd "spawnManager.setPlayerKit 1" 0
    //   setButtonNodeConCmd "sound.playSound kitSelect"   0
    //   setButtonNodeConCmd "sound.playSound kitOver"     1
    //
    // Доти ми лишали останню — і кнопка вибору класу «виконувала» звук
    // наведення замість вибору.
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
    // `command` лишається першою дією натискання — за нею кнопка й
    // вважається дієвою. Натисканню належать події 0 і 3.
    if ((event == 0 || event == 3) && node->command.empty()) node->command = joined;
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
    node->showEffects.push_back(ShowEffectInfo{ShowEffect::Alpha, 0.0f, 0.0f});
    return;
  }
  // `addNodeMoveShowEffect <напрям> <відстань>`. У DICE трапляється й
  // опечатка «addNoDemoveShowEffect» (10 разів), але в нижньому регістрі
  // вона збігається з правильною назвою, тож окремої гілки не треба.
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

  // --- списки трансформацій: вузол стає нащадком іншого ---
  if (method == "addtransformlistnode" || method == "addtransfomlistnode") {
    node->children.emplace_back(command.argStr(0));
    return;
  }
  // setTranformListNodeOffset <x> <y> — крок між сусідніми пунктами
  // списку. Без нього всі пункти лягають один на одного.
  if (method == "settranformlistnodeoffset" || method == "settransformlistnodeoffset") {
    node->childOffsetX = command.argFloat(0).value_or(0.0f);
    node->childOffsetY = command.argFloat(1).value_or(0.0f);
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
  auto readColor = [&](Color& out) {
    out.r = command.argFloat(0).value_or(1.0f);
    out.g = command.argFloat(1).value_or(1.0f);
    out.b = command.argFloat(2).value_or(1.0f);
    out.a = command.argFloat(3).value_or(1.0f);
  };
  // --- решта команд hudBuilder ---------------------------------------
  //
  // Форми зняті з даних гри (кількість аргументів у дужках). Кілька імен
  // у DICE з друкарськими помилками — «Transfom» і «Tranform» замість
  // «Transform»; це ті самі команди, тож і обробник той самий.
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
