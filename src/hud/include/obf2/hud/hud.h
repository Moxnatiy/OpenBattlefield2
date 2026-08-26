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

struct Node {
  NodeType type = NodeType::Other;
  std::string group;  // IngameHud, ScoreboardHud ...
  std::string name;

  // Координати у віртуальному екрані гри (див. kReferenceWidth/Height).
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

  std::string texture;          // setPictureNodeTexture / setButtonNodeTexture
  std::string textureVariable;  // setPictureNodeVariableTexture
  std::string text;             // setTextNodeString
  std::string textVariable;     // setTextNodeStringVariable
  std::string style;            // setTextNodeStyle
  std::string showVariable;     // умова показу
  std::string alphaVariable;
  std::string command;          // setButtonNodeConCmd — кнопка виконує команду

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
  long long unknown_ = 0;
};

}  // namespace obf2::hud
