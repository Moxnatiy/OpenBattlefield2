#include "obf2/engine/console.h"

#include <cctype>

namespace obf2::engine {
namespace {

std::string toLower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace

void Console::bind(std::string_view name, Handler handler) {
  handlers_[toLower(name)] = std::move(handler);
}

bool Console::execute(const con::Command& command) {
  const auto found = handlers_.find(command.lowerPath);
  if (found == handlers_.end()) {
    ++unknown_;
    ++unknownByName_[command.lowerPath];
    return false;
  }
  ++executed_;
  found->second(command);
  return true;
}

}  // namespace obf2::engine
