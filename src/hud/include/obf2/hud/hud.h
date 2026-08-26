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
  Other,
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
};

// HUD BF2 розкладений у координатах 640x480 і розтягується на екран.
// Видно з самих файлів: вузли доходять до 640 по X і 480 по Y.
inline constexpr float kReferenceWidth = 640.0f;
inline constexpr float kReferenceHeight = 480.0f;

class Builder {
 public:
  // Підключається як обробник команд до con::Interpreter.
  void feed(const con::Command& command);

  const std::vector<Node>& nodes() const { return nodes_; }
  std::vector<const Node*> group(std::string_view name) const;
  std::vector<std::string> groups() const;

  long long unknownCommands() const { return unknown_; }

 private:
  Node* active() { return nodes_.empty() ? nullptr : &nodes_.back(); }

  std::vector<Node> nodes_;
  long long unknown_ = 0;
};

}  // namespace obf2::hud
