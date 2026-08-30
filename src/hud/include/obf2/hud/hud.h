#pragma once
// Інтерфейс гри: `hudBuilder.*` у `Menu_client.zip/HUD/`.
//
// Це **повноцінна декларативна система інтерфейсу**, а не Flash: 1145 файлів
// і 25 060 команд описують увесь ігровий HUD, табло, екран появи, список
// рівнів і відомості про сервер. Flash лишається тільки в головному меню
// (`External/FlashMenu/`, 5 `.swf`).
//
// Вузол оголошується так:
//
//   hudBuilder.createPictureNode IngameHud WarningIcon 701 292 32 32
//   hudBuilder.setPictureNodeTexture Ingame/GeneralIcons/.../icon.tga
//   hudBuilder.setNodeShowVariable WarningIconShow
//   hudBuilder.setNodeInTime 0.2
//
// Далі всі `set*` застосовуються до **останнього створеного вузла** — той
// самий принцип, що й у ObjectTemplate.
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
  // Далі — вузли, які ми розбираємо, але ще не малюємо. Тип знати треба
  // однаково: без нього прямокутник читається не з тих аргументів.
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

// Ефект появи й зникнення вузла.
enum class ShowEffect {
  Alpha,  // проявляється прозорістю
  Move,   // виїжджає
  Blend,
};

std::string_view nodeTypeName(NodeType type);

struct Color {
  float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// Яким поданням показана карта. Мініатюра в кутку — звичайний бій,
// велика — екран появи та карта на весь екран, командирська — окремо.
enum class MapView { Mini, Maxi, Commander };

// Одне з подань карти: положення і розмір.
// Ставить вузлові карти прямокутник обраного подання: власного
// прямокутника карта при створенні не має.
struct Node;
void useMapView(Node& node, MapView view);

// Одна умова показу з setNodeLogicShowVariable.
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
  // Перший аргумент кожної команди — це **батьківський вузол**, а не
  // проста мітка групи. Так каже власний Readme.txt розробників у
  // HUD/HudSetup: «When you place nodes in either of these areas they
  // will gain relative coordinates from the area you placed the node
  // in». Тобто HUD — дерево, і x/y відлічуються від лівого верхнього
  // кута батька. Батьком може бути як ділянка (Global, TopLeft,
  // BottomLeftStatic…), так і будь-який інший вузол: у даних гри дерево
  // сягає восьми рівнів, а просто на ділянках висить лише шість вузлів.
  std::string group;
  std::string name;

  // Координати у віртуальному екрані гри (див. kReferenceWidth/Height).
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

  std::string texture;          // setPictureNodeTexture / setButtonNodeTexture (стан 1)
  std::string hoverTexture;     // setButtonNodeTexture 2 — під курсором
  std::string textureVariable;  // setPictureNodeVariableTexture
  std::string text;             // setTextNodeString
  std::string textVariable;     // setTextNodeStringVariable
  std::string style;            // setTextNodeStyle
  // Другий аргумент setTextNodeStyle — вирівнювання рядка в рамці
  // вузла. У даних лише три значення: 0 (142 рази), 1 (97) і 2 (103).
  // Знімок кадру оригіналу показує: повідомлення посеред екрана має 0 і
  // стоїть по центру, а підпис класу має 2 і тулиться ліворуч.
  int textAlign = 0;
  std::string showVariable;     // умова показу — setNodeShowVariable
  // setNodeLogicShowVariable завжди має вигляд `дія змінна значення`:
  // NOT (118), EQUAL (92), AND (44), OR (31). Це не ім'я змінної, як ми
  // читали доти, а окрема умова, що приєднується до showVariable. Через
  // ту помилку цілі гілки HUD не показувалися ніколи.
  std::vector<ShowTest> showTests;
  std::string alphaVariable;
  std::string command;          // перша дія натискання
  // Усі команди кнопки з їхньою подією: 0 — натискання, 1 — наведення.
  std::vector<std::pair<int, std::string>> commands;

  Color color;
  float inTime = 0.0f;
  float outTime = 0.0f;

  std::string altCommand;      // setButtonNodeAltConCmd — дія правою кнопкою
  std::string valueVariable;   // setBarNodeValueVariable — заповнення смуги
  // Смуга має свій зайвий параметр перед прямокутником — напрям росту, —
  // і дві текстури: порожню (0) і повну (1).
  int barDirection = 0;
  std::string barTextureEmpty;
  std::string barTextureFull;
  std::string positionVariable;  // setNodePosVariable
  std::string rotateVariable;    // setPictureNodeRotateVariable
  std::vector<std::string> rgbVariables;  // setNodeRGBVariables

  float offsetX = 0.0f, offsetY = 0.0f;        // setNodeOffset
  float centerX = 0.0f, centerY = 0.0f;        // setPictureNodeCenterPoint
  float textureWidth = 0.0f, textureHeight = 0.0f;  // setObjectMarkerNodeTextureSize

  // Прямокутник, у якому кнопка ловить мишу; якщо не заданий — весь вузол.
  bool hasMouseArea = false;
  float mouseX = 0.0f, mouseY = 0.0f, mouseWidth = 0.0f, mouseHeight = 0.0f;

  std::string font;              // setListNodeFont / setTextNodeFont
  Color borderColor;             // setPictureNodeBorderColor
  float borderSize = 0.0f;       // setCompassNodeBorder / setBarNodeBorder
  bool snap = false;             // setBarNodeSnap
  // Об'єкти, які позначає маркер, і вузол підпису до нього.
  std::vector<std::string> markerObjects;
  std::string lockTextNode;

  int barSnapDir = 0;      // setBarNodeSnapDir — куди смуга «прилипає»
  float rotation = 0.0f;   // setPictureNodeRotation

  // Заповнює Builder::finish(): індекс батька в nodes() (-1 — батьком є
  // ділянка), назва ділянки-кореня і положення від її лівого верхнього
  // кута, зібране з усіх предків.
  int parent = -1;
  std::string area;
  float absX = 0.0f, absY = 0.0f;

  // Список (табло, вибір загону). Тло і рамка в нього не текстури, а
  // суцільні кольори — саме тому список без них виглядав порожнім
  // місцем:
  //
  //   createListNode Scoreboard FriendlyScoreList 10 75 389 462 19 1
  //   setListNodeBackgroundColor 0.745 0.729 0.58 0.9
  //   setListNodeBorder 20 22 3 3
  //   setListNodeBorderColor 0.482 0.474 0.388 1
  //
  // Передостаннє число в createListNode — висота рядка.
  bool hasListBackground = false;
  Color listBackground;
  bool hasListBorder = false;
  Color listBorderColor;
  float listBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // ліворуч, праворуч, згори, знизу
  float listRowHeight = 0.0f;
  Color listSelectColor;                  // setListNodeSelectColor r g b a
  bool hasListScrollbar = false;          // setListNodeScrollbar <ширина> <проміжок>
  float listScrollbarWidth = 0.0f;
  float listScrollbarGap = 0.0f;
  Color listScrollbarColor;
  Color listScrollbarBackground;
  int listData = -1;             // setListNodeData — номер джерела рядків
  float listRowSpacing = 0.0f;   // setListNodeRowSpacing
  bool listOutline = false;      // setListNodeOutline
  // setListNodeConCmd <номер> "<команда>" — що виконати на клацання.
  std::vector<std::pair<int, std::string>> listCommands;

  // Поле вводу (чат, назва загону).
  std::string editFont;      // setEditNodeFont <шлях> <номер>
  int editData = -1;         // setEditNodeData
  int editString = -1;       // setEditNodeString
  int editMaxLength = 0;     // setEditNodeMaxLength
  Color editColor;           // setEditNodeColor r g b a
  bool hasEditColor = false;

  // Мітка об'єкта (захоплення цілі в техніці).
  int markerLockOnType = 0;                          // setObjectMarkerNodeLockOnType
  int markerWeapon = 0;                              // setObjectMarkerNodeWeapon
  std::string markerLockText;                        // setObjectMarkerNodeLockText <n> <вузол>
  float markerLockTextOffset[2] = {0.0f, 0.0f};      // setObjectMarkerNodeLockTextOffset

  // Місця в техніці: setOccupiedNodeData <номер>, а пари координат
  // приходять зі змінних — setOccupiedNodePosVariable <номер> <змінна>.
  int occupiedData = -1;
  std::vector<std::string> occupiedPosVariables;

  // Компас: куди «прилипають» позначки й чим їх малювати.
  float compassSnapOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  std::vector<std::string> compassSnapTextures;

  // Вузол наведення (підказка під курсором).
  float hoverMiddle[2] = {0.0f, 0.0f};
  float hoverMaxValue = 0.0f;
  float hoverWidth = 0.0f;
  float hoverLength = 0.0f;

  // Повзунок: який вузол їздить і яку змінну міняє.
  std::string sliderChild;
  std::string sliderData;

  // Обведення тексту: окремий шрифт і зсув, яким його малюють під низом.
  std::string outlineFont;                     // setTextNodeOutLine
  float outlineOffset[2] = {0.0f, 0.0f};       // setTextNodeOutLineOffset

  // Вибір об'єкта: розмір вказівника.
  float pointerSize[2] = {0.0f, 0.0f};

  // Крок між пунктами списку трансформацій (setTranformListNodeOffset).
  float childOffsetX = 0.0f, childOffsetY = 0.0f;

  // Шар малювання. `hudBuilder.newLayer` починає наступний: усе, створене
  // після нього, лягає поверх попереднього незалежно від місця в дереві.
  // У даних гри він трапляється один раз — перед картою.
  int layer = 0;

  // Карта: піктограми масштабу і шрифт підписів точок.
  int zoomIcons = 0;
  std::string cpFont;
  Color cpFontColor;

  // Карта власного прямокутника при створенні не дістає — гра задає їй
  // три різні подання окремими командами (HudElementsMap.con):
  //
  //   setMaxiPos -122/-273   setMaxiSize 512/512     екран появи
  //   setMiniPos 197/-300    setMiniSize 197/197     кут під час бою
  //   setCommanderPos …      setCommanderSize 561/561  режим командира
  //
  // Пара пишеться **одним словом** через скісну риску, а не двома
  // аргументами. Від'ємне число означає відлік від правого чи нижнього
  // краю — так само, як у решті HUD.
  MapRect mapMaxi;
  MapRect mapMini;
  MapRect mapCommander;
  // Яке подання зараз стоїть. Мініатюра в грі кругла, а велика на екрані
  // появи — квадратна; рамка map_Frame.tga кутів не закриває (у неї там
  // прозорість), тож коло має давати сам вузол.
  MapView mapView = MapView::Mini;

  // Команди, які ми вже впізнаємо, але ще не малюємо: аргументи лежать
  // тут як є. Так вони не губляться мовчки, і за списком видно, чого
  // бракує саме рендеру, а не розборові. Таблиця — hud_recorded.inc.
  std::map<std::string, std::vector<std::string>> extra;

  std::vector<ShowEffect> showEffects;
  // Імена вузлів, доданих до цього списку трансформацій.
  std::vector<std::string> children;
};

// HUD BF2 розкладений у координатах 800x600 і розтягується на екран.
//
// Це видно з самих даних, а не з припущення: `hudManager.setCommPos 150 150`
// разом із `setCommSize 490 300` дає рівно 640x450, а `setCommMousePos
// 400 300` — центр екрана 800x600. Тека шрифтів теж зветься `800/`.
inline constexpr float kReferenceWidth = 800.0f;
inline constexpr float kReferenceHeight = 600.0f;

class Builder {
 public:
  // Підключається як обробник команд до con::Interpreter.
  void feed(const con::Command& command);

  // Викликати після подачі всіх команд: саме тут дерево зв'язується і
  // з'являються Node::parent, Node::area та Node::absX/absY. Без цього
  // кроку координати лишаються відносними — і HUD розсипається.
  void finish();

  // Перемикає всі вузли карти на задане подання. Карта в грі одна, але
  // показана по-різному: у бою мініатюра в кутку, на екрані появи —
  // велика (setMaxiPos/setMaxiSize), у командира — своя.
  void setMapView(MapView view);

  const std::vector<Node>& nodes() const { return nodes_; }
  std::vector<const Node*> group(std::string_view name) const;
  std::vector<std::string> groups() const;

  long long unknownCommands() const { return unknown_; }
  const std::map<std::string, int>& unknownByName() const { return unknownByName_; }

 private:
  Node* active();

  std::vector<Node> nodes_;
  std::map<std::string, int> unknownByName_;
  int activeIndex_ = -1;  // -1 = останній створений
  int layer_ = 0;         // поточний шар, його зсуває hudBuilder.newLayer
  long long unknown_ = 0;
};

}  // namespace obf2::hud
