#pragma once
// The control layout comes from the game's own data (`Settings/Controls.con`).
//
// The game does not keep keys in code: there is a `ControlMap` that binds an
// **action** to a key, and those bindings live in the .con:
//
//   ControlMap.addKeyToTriggerMapping c_GIShowScoreboard IDFKeyboard IDKey_Tab 0 0
//   ControlMap.addKeysToAxisMapping   c_PIThrottle IDFKeyboard IDKey_W IDKey_S 0
//
// So this is not a list of keys but a reader of those bindings: the code asks
// about an action (`c_GIRadioComm`), and which key it is the data decides. Just
// as in the original, and changing with the player's profile just the same.
#include <map>
#include <string>
#include <string_view>

#include "obf2/con/interpreter.h"

namespace obf2::game {

class ControlMap {
 public:
  // Attached as a command handler to con::Interpreter.
  void feed(const con::Command& command);

  // The key for an action, e.g. "IDKey_Tab"; empty means the action is unbound.
  // The first binding wins: in the data the same action occurs twice (a key and
  // an alternative), and the game takes the one declared earlier.
  std::string_view key(std::string_view action) const;

  // An axis gives two keys: towards plus and towards minus.
  std::string_view axisPositive(std::string_view action) const;
  std::string_view axisNegative(std::string_view action) const;

  std::size_t size() const { return triggers_.size() + axes_.size(); }

 private:
  std::map<std::string, std::string, std::less<>> triggers_;
  std::map<std::string, std::pair<std::string, std::string>, std::less<>> axes_;
};

}  // namespace obf2::game
