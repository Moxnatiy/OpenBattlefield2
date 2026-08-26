#include "obf2/engine/console.h"

#include <cctype>

#include "obf2/con/lexer.h"

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

bool Console::executeLine(std::string_view line) {
  const std::vector<std::string> tokens = con::tokenizeLine(line);
  if (tokens.empty()) return false;

  con::Command command;
  command.path = con::splitCommandPath(tokens[0]);
  if (command.path.empty()) return false;
  command.lowerPath = toLower(tokens[0]);
  command.args.assign(tokens.begin() + 1, tokens.end());
  return execute(command);
}

}  // namespace obf2::engine
