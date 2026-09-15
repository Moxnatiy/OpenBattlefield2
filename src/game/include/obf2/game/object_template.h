#pragma once
// The ObjectTemplate registry — Refractor 2's central data structure.
//
// 87% of all commands in the game's files (433 778 of 497 195) address
// ObjectTemplate, and they describe absolutely everything: weapons, vehicles,
// effects, sounds, the HUD. The .con language is a stream of commands, so the
// registry works as a state machine: `create` makes a template active, and the
// following commands write into it until the next `create`/`activeSafe`.
//
// There are 855 distinct properties, so there can be no typed structure here —
// only a universal bag of values. Typed views (weapon, vehicle) are built on
// top when their turn comes.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "obf2/con/interpreter.h"

namespace obf2::game {

using con::Vec3;

// One property assignment. The source is kept, because the path "the .con
// created it, the .tweak overwrote it" is normal, and without the file and line it cannot be untangled.
struct Property {
  std::vector<std::string> args;
  std::string file;
  int line = 0;

  std::string_view value(std::size_t index = 0) const {
    return index < args.size() ? std::string_view{args[index]} : std::string_view{};
  }
  std::optional<float> asFloat(std::size_t index = 0) const;
  std::optional<int> asInt(std::size_t index = 0) const;
  std::optional<bool> asBool(std::size_t index = 0) const;
  std::optional<Vec3> asVec3(std::size_t index = 0) const;
};

// The keys are lower-case; the values are every assignment in order, because
// some properties accumulate (`mapMaterial` — 10 561 calls, several per
// template) while others are overwritten.
using PropertyMap = std::unordered_map<std::string, std::vector<Property>>;

struct Component {
  std::string name;  // the original case, as in the file
  PropertyMap properties;
};

// An attached child template: `addTemplate` plus `setPosition`/`setRotation`,
// which apply to the LAST child added.
struct ChildTemplate {
  std::string name;
  Vec3 position;
  Vec3 rotation;
  bool hasPosition = false;
  bool hasRotation = false;
};

class ObjectTemplate {
 public:
  std::string className;  // "PlayerControlObject", "GenericFireArm", ...
  std::string name;       // "apc_btr90"
  std::string file;       // where it was first created
  int line = 0;

  PropertyMap properties;
  std::vector<Component> components;
  std::vector<ChildTemplate> children;

  // The last assignment is the one that applies, because a .tweak runs after the .con.
  const Property* property(std::string_view name) const;
  const std::vector<Property>* propertyHistory(std::string_view name) const;
  const Component* component(std::string_view name) const;

  std::string_view text(std::string_view propertyName) const;
  std::optional<float> number(std::string_view propertyName) const;
};

class Registry {
 public:
  struct Stats {
    int created = 0;           // ObjectTemplate.create
    int reopened = 0;          // activeSafe on an already existing template
    int componentsCreated = 0;
    int childrenAdded = 0;
    long long propertiesSet = 0;
    int orphanCommands = 0;    // a command before the first create — see below
  };

  // Attached as a command handler to con::Interpreter.
  void feed(const con::Command& command);

  const ObjectTemplate* find(std::string_view name) const;
  std::size_t size() const { return templates_.size(); }
  const Stats& stats() const { return stats_; }

  // Every template in creation order.
  std::vector<const ObjectTemplate*> all() const;

 private:
  ObjectTemplate& createTemplate(std::string className, std::string name,
                                 const con::Command& command);
  void applyProperty(PropertyMap& target, const std::string& key, const con::Command& command);

  std::vector<std::unique_ptr<ObjectTemplate>> order_;
  std::unordered_map<std::string, ObjectTemplate*> templates_;  // the key is lower-case
  ObjectTemplate* active_ = nullptr;
  ChildTemplate* lastChild_ = nullptr;
  Stats stats_;
};

}  // namespace obf2::game
