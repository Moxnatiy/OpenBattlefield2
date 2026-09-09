#pragma once
// Controls: `Settings/Controls.con`.
//
// This is the largest block of startup commands — 232 calls of 314 — so without
// it a "full startup" is impossible. The format is this:
//
//   ControlMap.create PlayerInputControlMap
//   ControlMap.addKeyToTriggerMapping c_PIFire IDFMouse IDMouseButton0
//   ControlMap.addAxisToAxisMapping   c_PIMouseLookX IDFMouse IDAxis0
//   ControlMap.mouseSensitivity 0.15
//
// The first argument is an engine action (`c_PI*`), then the device (`IDFMouse`,
// `IDFKeyboard`) and its element. The names are kept as in the original: the
// user edits these files, and they have to stay compatible.
#include <string>
#include <unordered_map>
#include <vector>

#include "obf2/engine/console.h"

namespace obf2::engine {

// What exactly a binding drives. The names match the game's commands.
enum class MappingKind {
  KeyToTrigger,      // key -> action (fire, crouch)
  ButtonToTrigger,   // a device button -> action
  AxisToTrigger,     // axis -> action
  AxisToAxis,        // axis -> axis (the mouse to looking)
  KeysToAxis,        // a pair of keys -> axis (W/S to forward-back movement)
};

struct Mapping {
  MappingKind kind = MappingKind::KeyToTrigger;
  std::string action;   // c_PIFire, c_PIMouseLookX ...
  std::string device;   // IDFMouse, IDFKeyboard ...
  std::vector<std::string> elements;  // IDMouseButton0, IDKey_W ...
};

class ControlMap {
 public:
  // Registers the ControlMap.* handlers in the console.
  void bind(Console& console);

  const std::vector<std::string>& maps() const { return maps_; }
  const std::vector<Mapping>& mappings() const { return mappings_; }
  float mouseSensitivity() const { return mouseSensitivity_; }
  bool mouseInvert() const { return mouseInvert_; }

  // Every binding of the given action — there may be several (a key plus a button).
  std::vector<const Mapping*> forAction(std::string_view action) const;

  std::size_t size() const { return mappings_.size(); }

 private:
  void add(MappingKind kind, const con::Command& command);

  std::vector<std::string> maps_;  // ControlMap.create
  std::vector<Mapping> mappings_;
  float mouseSensitivity_ = 0.15f;
  bool mouseInvert_ = false;
};

}  // namespace obf2::engine
