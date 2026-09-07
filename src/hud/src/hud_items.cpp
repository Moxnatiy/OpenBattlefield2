#include "obf2/hud/hud_items.h"

namespace obf2::hud {

void HudItems::bind(engine::Console& console) {
  console.bind("hudItems.setBool", [this](const con::Command& command) {
    if (command.args.size() < 2) return;
    flags_[std::string(command.argStr(0))] = command.argInt(1).value_or(0) != 0;
    dirty_ = true;
  });
}

std::string_view HudItems::text(std::string_view name) const {
  const auto found = text_.find(name);
  return found == text_.end() ? std::string_view{} : std::string_view(found->second);
}

float HudItems::value(std::string_view name) const {
  const auto found = values_.find(name);
  return found == values_.end() ? 0.0f : found->second;
}

std::optional<float> HudItems::alpha(std::string_view name) const {
  const auto found = alpha_.find(name);
  if (found == alpha_.end()) return std::nullopt;
  return found->second;
}

}  // namespace obf2::hud
