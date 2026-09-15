#include "obf2/game/controls.h"

namespace obf2::game {
namespace {

// Bindings exist for several devices; the keyboard is what interests us, the
// rest (mouse, joystick, Falcon) not yet.
constexpr std::string_view kKeyboard = "IDFKeyboard";

}  // namespace

void ControlMap::feed(const con::Command& command) {
  if (command.lowerPath.rfind("controlmap.", 0) != 0) return;
  const std::string_view method = std::string_view(command.lowerPath).substr(11);

  if (method == "addkeytotriggermapping") {
    if (command.args.size() < 3 || command.args[1] != kKeyboard) return;
    triggers_.emplace(command.args[0], command.args[2]);
    return;
  }
  if (method == "addkeystoaxismapping") {
    if (command.args.size() < 4 || command.args[1] != kKeyboard) return;
    axes_.emplace(command.args[0], std::pair{command.args[2], command.args[3]});
    return;
  }
}

std::string_view ControlMap::key(std::string_view action) const {
  const auto found = triggers_.find(action);
  return found == triggers_.end() ? std::string_view{} : std::string_view(found->second);
}

std::string_view ControlMap::axisPositive(std::string_view action) const {
  const auto found = axes_.find(action);
  return found == axes_.end() ? std::string_view{} : std::string_view(found->second.first);
}

std::string_view ControlMap::axisNegative(std::string_view action) const {
  const auto found = axes_.find(action);
  return found == axes_.end() ? std::string_view{} : std::string_view(found->second.second);
}

}  // namespace obf2::game
