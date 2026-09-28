#pragma once
// Who turns the HUD's show variables on.
//
// A HUD variable in the game is not an entry in a dictionary but **a field of an
// object**: `registerVariable` binds a name to a field, and the engine writes
// object**: `registerVariable` binds a name to a field, and the engine writes into it.
//
//   * **the state transition** — `HudObject::setState` (BF2.exe 0x786260). Two
//     switches in a row: the first, on the old state, turns off what it showed,
//     the second, on the new one, turns on its own. The 32-entry jump table lies
//     at 0x787008, and `tools/hud_states.py` reads it straight from the binary;
//   * **the map's size** — 0x466930. It gives `MapFullSize`, `MapMinSize`,
//     `MapBorderAlternateShow` and two composite variables;
//   * **the combat set by the current player** — 0x78d0f0. With no player,
//     0x78d2d9 clears the health, the stamina and the squad icons. That is
//     exactly why in the original no bars are visible behind the spawn screen.
//
// More in docs/functions/hud-states.md.
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace obf2::hud {

// The dictionary of show variables. The order does not matter here, but a map
// gives a stable walk, which is convenient in reports.
using VariableMap = std::map<std::string, bool>;

// One state: what it turns on and what it turns off.
struct StateEntry {
  int id = 0;
  std::vector<const char*> on;
  std::vector<const char*> off;
};

// The second switch in 0x786260 — on the **new** state. Taken from the binary by
// `tools/hud_states.py` (the jump table 0x787008), not retyped by hand.
const std::vector<StateEntry>& hudStates();

// The first switch in the same place — on the **old** state: what to clean up
// after itself. Only `off` is filled in this table's entries.
const std::vector<StateEntry>& hudLeaveStates();

// The transition from the old state into the new — verbatim `HudObject::setState`
// (`BF2.exe`, 0x786260): first the old state turns off its own, then the new one
// turns on and turns off its own.
//
// **That is not the same as "turn everything off and turn on what is needed".**
// Most states turn nothing off: state 2 (the big map) only turns on `MapShow`,
// while `ShowIngameHud` from state 0 stays — which is why in the original the
// rest of the HUD is still visible under the big map. Until now we turned
// everything off wholesale, and under the big map not even the map itself was
// left: it lives under `IngameHud`, and that under `ShowIngameHud`.
//
// `previous` = -1 (there was no state yet) leads into the first switch's
// `default` branch — it clears absolutely everything.
//
// Returns true when anything changed at all — for whoever bakes the geometry
// that means it is time to rebuild.
bool applyState(VariableMap& variables, int previous, int state);

// The HUD object's own state and the one it remembers — the engine keeps both
// in the object (`BF2.exe`, vtable 0x926130): the current one at +0x250
// (`getState`, slot 0x1e0) and the remembered one at +0x254 (slot 0x1e4).
// docs/functions/hud-states.md, "Who sets the state".
class StateMachine {
 public:
  // `setState(state, remember)`, slot 0x1dc (0x74fd50): nothing happens when the
  // state is the current one; otherwise the current one is remembered when
  // `remember` is set, and the transition runs (0x786260, `applyState`).
  // Returns true when the variables changed.
  bool set(VariableMap& variables, int state, bool remember);

  // Tab, `c_GIShowScoreboard` (action 0x37), in the game-input handler 0x758770.
  //
  // Pressed: in states 9, 12, 6, 4, 5 and 7 nothing happens; otherwise the HUD
  // goes to 9 with the state it covered remembered (0x758d38).
  //
  // Released — an event with none of the handler's actions pressed: in 9 the HUD
  // goes back to the remembered state, or to 0 when that one is 10 (0x7596f8,
  // 0x759776). 12 goes back the same way while the handler's `vtbl[0x26c]()` is
  // false; what that asks is **not established**, and it is taken as false.
  //
  // So the scoreboard stands while Tab is held and goes on its release, back to
  // the screen it covered.
  bool scoreboardPressed(VariableMap& variables);
  bool scoreboardReleased(VariableMap& variables);

  int current() const { return current_; }
  int remembered() const { return remembered_; }

 private:
  // No state yet: -1 leads into the first switch's `default` branch, "clear
  // absolutely everything" (0x78653c).
  int current_ = -1;
  int remembered_ = 0;
};

// A variable's value for the show conditions (`setNodeLogicShowVariable`).
//
// A condition asks by one name for both numeric variables (bar fills) and
// boolean ones (show variables) — and they live in different dictionaries. Until
// now we looked only among the numbers, and any condition over a boolean got
// zero: `NOT MapMinSize 1` came out true **always**, and the big map's bar was
// drawn over the minimap in the corner. In the original's frame dump
// (docs/research/03-frame-dump.md) it is not there.
float showValue(const VariableMap& flags, const std::map<std::string, float>& values,
                std::string_view name);

// What the engine knows about the world for this frame.
struct WorldView {
  // Whether the player has a soldier. Not "is the client connected": in the game
  // this is a check of the current controlled object.
  bool hasPlayer = false;
  // The map full-screen (the spawn screen) or a thumbnail in the corner.
  bool mapFullSize = false;
};

// Derived variables: what the engine computes every frame rather than setting from a list.
bool applyDerived(VariableMap& variables, const WorldView& view);

}  // namespace obf2::hud
