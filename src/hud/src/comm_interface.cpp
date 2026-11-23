#include "obf2/hud/comm_interface.h"

#include <algorithm>
#include <cctype>

#include "obf2/con/interpreter.h"

namespace obf2::hud {

void CommInterface::setStringMap(std::string_view key, std::string label, std::string command) {
  constexpr std::string_view prefix = "Interface_";
  if (key.starts_with(prefix)) key.remove_prefix(prefix.size());
  map_[std::string(key)] = Entry{std::move(label), std::move(command)};
}

bool CommInterface::feed(const con::Command& command) {
  // The object's name differs in case between the files (`radioInterface`,
  // `CommanderInterface`); the method is what matters.
  if (command.path.size() != 2 || !command.lowerPath.ends_with(".setstringmap") || command.args.size() < 2) {
    return false;
  }
  setStringMap(command.args[0], command.args[1], command.args.size() > 2 ? command.args[2] : std::string{});
  return true;
}

std::optional<CommInterface::Entry> CommInterface::entry(int order) const {
  const auto found = map_.find(path_ + ":" + std::to_string(order));
  if (found == map_.end()) return std::nullopt;
  return found->second;
}

CommInterface::Selection CommInterface::selectOrder(int order) {
  Selection selection;
  const std::string key = path_ + ":" + std::to_string(order);
  // 0x7a0de0 walks `<key>:i` for i from 1 and, on the first that exists, writes
  // the level's labels into the HUD's `%s%iString` variables and keeps the rose
  // open on the new path.
  if (map_.contains(key + ":1")) {
    path_ = key;
    selection.descended = true;
    return selection;
  }
  const auto found = map_.find(key);
  selection.command = found == map_.end() ? std::string{} : found->second.command;
  reset();
  return selection;
}

}  // namespace obf2::hud
