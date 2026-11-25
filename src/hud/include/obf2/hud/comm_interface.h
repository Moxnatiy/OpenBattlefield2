#pragma once
// The communication roses: the radio, the radio in a vehicle, the squad member's,
// the squad leader's and the commander's (docs/functions/command-rose.md).
//
// In BF2.exe the five are one class (vtable 0x933b10, constructor 0x7a1d40,
// 0x5c bytes) built five times by the HUD manager (0x757028..0x7570c5, fields
// +0x108 squad, +0x10c commander, +0x110 radio in a vehicle, +0x114 squad
// leader, +0x118 radio... see the notes for which getter is which). What each
// one holds is data: `HUD/HudSetup/Communication/<Name>.con` maps a key
// `Interface_<path>` to a label (a localisation key) and a console command.
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace obf2::con {
struct Command;
}

namespace obf2::hud {

class CommInterface {
 public:
  struct Entry {
    std::string label;    // the localisation key, `HUD_COMROSE_SL_attack`; "*NOT USED*" for a gap
    std::string command;  // the console command run when the entry is chosen; may be empty
  };

  // `<name>Interface.setStringMap Interface_1:3 LABEL "command"` (the console
  // object 0x76e05d → the interface's slot 0x18). The key is kept without its
  // `Interface_` prefix.
  void setStringMap(std::string_view key, std::string label, std::string command);
  // The same from a parsed `.con` line; false when the line is not one.
  bool feed(const con::Command& command);

  // The entry `order` (1-based) of the level the rose stands on.
  std::optional<Entry> entry(int order) const;
  bool empty() const { return map_.empty(); }

  // `selectOrder N` (the interface's slot 8, 0x7a0de0). The key is the current
  // path and N. When entries exist under it (`<path>:N:1`...), the rose goes down
  // a level and stays open: nothing to run. Otherwise the entry's command is the
  // answer and the rose closes (+4 = 0) and returns to the top.
  struct Selection {
    std::optional<std::string> command;  // set when the rose closes with a command (empty: none)
    bool descended = false;
  };
  Selection selectOrder(int order);

  void reset() { path_ = "1"; }
  const std::string& path() const { return path_; }

 private:
  std::map<std::string, Entry, std::less<>> map_;
  std::string path_ = "1";
};

}  // namespace obf2::hud
