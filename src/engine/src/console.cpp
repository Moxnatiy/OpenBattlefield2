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

void Console::registerAliases() {
  bind("alias", [this](const con::Command& command) {
    if (command.args.size() < 2) return;
    // The target may consist of several words (`alias r3 game.setTeam 3`), but
    // in our data all 79 lines are exactly two words. The extra is glued back
    // together so nothing is lost.
    std::string target(command.argStr(1));
    for (std::size_t i = 2; i < command.args.size(); ++i) {
      target += ' ';
      target += command.args[i];
    }
    aliases_[toLower(command.argStr(0))] = std::move(target);
  });
}

bool Console::execute(const con::Command& command) {
  const auto found = handlers_.find(command.lowerPath);
  if (found == handlers_.end()) {
    // It may be an alias. The chain is expanded, but not endlessly:
    // `alias a b` + `alias b a` must not hang the console.
    std::string name = command.lowerPath;
    for (int step = 0; step < 8; ++step) {
      const auto alias = aliases_.find(name);
      if (alias == aliases_.end()) break;
      std::string line = alias->second;
      for (const std::string& argument : command.args) {
        line += ' ';
        line += argument;
      }
      const std::vector<std::string> tokens = con::tokenizeLine(line);
      if (tokens.empty()) break;
      name = toLower(tokens[0]);
      const auto handler = handlers_.find(name);
      if (handler == handlers_.end()) continue;  // an alias onto an alias
      con::Command expanded;
      expanded.path = con::splitCommandPath(tokens[0]);
      expanded.lowerPath = name;
      expanded.args.assign(tokens.begin() + 1, tokens.end());
      ++executed_;
      handler->second(expanded);
      return true;
    }

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
