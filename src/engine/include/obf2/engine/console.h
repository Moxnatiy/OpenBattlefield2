#pragma once
// The console — the engine's command dispatcher.
//
// In Refractor 2 it is a subsystem of its own (`IO/Console/Console.cpp` by the
// paths in the binary), and it is the single point every `.con` converges on:
// settings, object templates, levels, the player's console input. So here too a
// command from a file and a command typed in the console take the same path.
//
// From BF2.exe's string table we know the engine knows 1735 commands
// (docs/reference/con-commands-from-exe.txt). They need not all be implemented —
// but we do need to **see** which of them occurred and were left without a handler.
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include "obf2/con/interpreter.h"

namespace obf2::engine {

class Console {
 public:
  using Handler = std::function<void(const con::Command&)>;

  // The name in the form "target.method"; case does not matter, as in the game.
  void bind(std::string_view name, Handler handler);

  // true means a handler was found. Unknown commands are counted rather than
  // silently ignored: that counter is the port's measure of readiness.
  bool execute(const con::Command& command);

  // The same call but from a text line: "target.method arg arg".
  // That is exactly how an interface button holds a command (setButtonNodeConCmd).
  bool executeLine(std::string_view line);

  std::size_t handlerCount() const { return handlers_.size(); }
  long long executedCount() const { return executed_; }
  long long unknownCount() const { return unknown_; }

  // Unknown commands by descending frequency — a work plan in its purest form.
  const std::map<std::string, int>& unknownCommands() const { return unknownByName_; }

  // Aliases: `alias <short> <target>`.
  //
  // This is the engine's own command, not our invention: the game has a whole
  // file `Settings/AliasedCommands.con` with 79 such lines, and they are what
  // make `fps`, `hud`, `lp`, `suicide` and the other short names work in the
  // console. An alias has no dot, so it does not fit our "target.method" split —
  // it is looked up separately, when no handler was found.
  //
  // A chain of aliases (`alias a b`, `alias b c.d`) is expanded down to the real
  // command; a closed loop is broken off by a step count.
  void registerAliases();
  std::size_t aliasCount() const { return aliases_.size(); }

 private:
  std::unordered_map<std::string, Handler> handlers_;
  std::unordered_map<std::string, std::string> aliases_;
  std::map<std::string, int> unknownByName_;
  long long executed_ = 0;
  long long unknown_ = 0;
};

}  // namespace obf2::engine
