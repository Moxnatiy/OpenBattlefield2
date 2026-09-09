// Who turns the HUD's show variables on.
//
// We check exactly what was reversed: the state table from BF2.exe and the two
// functions the engine runs every frame (docs/functions/hud-states.md).
#include "check.h"
#include <cmath>
#include <map>

#include "obf2/hud/states.h"

using namespace obf2;

namespace {

bool on(const hud::VariableMap& variables, const char* name) {
  const auto found = variables.find(name);
  return found != variables.end() && found->second;
}

}  // namespace

// State 0 is ordinary combat, state 1 the spawn screen. Both from the jump table
// 0x787008.
static void testStateTurnsOnItsOwn() {
  hud::VariableMap variables;

  CHECK(hud::applyState(variables, -1, 0));
  CHECK(on(variables, "ShowIngameHud"));
  CHECK(on(variables, "MapShow"));
  CHECK(on(variables, "MapBorderShow"));
  CHECK(!on(variables, "SpawnShow"));

  CHECK(hud::applyState(variables, 0, 1));
  CHECK(on(variables, "SpawnShow"));
  CHECK(on(variables, "KitsShow"));
  CHECK(on(variables, "MapBorderAlternateShow"));
}

// The main thing we used not to do: **state 2 turns nothing off**. The big
// map only turns on `MapShow`, while `ShowIngameHud` stays on from state 0 — and
// the rest of the HUD is still visible under the big map. Until now we turned
// everything off wholesale, and even the map itself disappeared: it lives under
// `IngameHud`, and that under `ShowIngameHud`.
static void testBigMapKeepsTheHud() {
  hud::VariableMap variables;
  hud::applyState(variables, -1, 0);

  // No show variable changes at that: `MapShow` is already on from state 0. The
  // whole difference of the big map is in the map node's own size target
  // (0x777dc0, obf2/hud/map_node.h), not in the variables.
  CHECK(!hud::applyState(variables, 0, 2));
  CHECK(on(variables, "MapShow"));
  CHECK(on(variables, "ShowIngameHud"));
  CHECK(on(variables, "MapBorderShow"));
}

// The same with the quick map menu (state 19): it lands over combat rather than
// instead of it.
static void testMapMenuKeepsTheHud() {
  hud::VariableMap variables;
  hud::applyState(variables, -1, 0);
  hud::applyState(variables, 0, 19);
  CHECK(on(variables, "MapMenuShow"));
  CHECK(on(variables, "ShowIngameHud"));

  // While leaving it cleans up after itself — that is already the first switch.
  hud::applyState(variables, 19, 0);
  CHECK(!on(variables, "MapMenuShow"));
}

// A transition leaves no tails: the old state cleans up after itself. In the game
// that is done by the first switch in HudObject::setState.
static void testStateLeavesNoTail() {
  hud::VariableMap variables;
  hud::applyState(variables, -1, 9);  // the scoreboard
  CHECK(on(variables, "ScoreboardShow"));
  CHECK(on(variables, "LevelsListShow"));

  hud::applyState(variables, 9, 0);
  CHECK(!on(variables, "ScoreboardShow"));
  CHECK(!on(variables, "LevelsListShow"));
  CHECK(on(variables, "ShowIngameHud"));
}

// A repeated transition into the same state changes nothing — and has to say so,
// otherwise we would rebuild the geometry every frame.
static void testRepeatedStateChangesNothing() {
  hud::VariableMap variables;
  CHECK(hud::applyState(variables, -1, 1));
  CHECK(!hud::applyState(variables, 1, 1));
}

// Without a player the combat set is cleared — which is exactly why in the original
// no health or ammo bars are visible behind the spawn screen.
static void testCombatSetNeedsAPlayer() {
  hud::VariableMap variables;

  hud::applyDerived(variables, hud::WorldView{false, true});
  CHECK(!on(variables, "PlayerHealthShow"));
  CHECK(!on(variables, "PlayerStaminaShow"));
  CHECK(!on(variables, "PrimaryAmmoShow"));

  CHECK(hud::applyDerived(variables, hud::WorldView{true, false}));
  CHECK(on(variables, "PlayerHealthShow"));
  CHECK(on(variables, "PlayerStaminaShow"));
  CHECK(on(variables, "PrimaryAmmoShow"));
}

// The map's size gives three variables at once, and MapBorderAlternateShow is the
// negation of MapMinSize (0x4669ae).
static void testMapSizeDrivesThree() {
  hud::VariableMap variables;

  hud::applyDerived(variables, hud::WorldView{false, true});
  CHECK(on(variables, "MapFullSize"));
  CHECK(!on(variables, "MapMinSize"));
  CHECK(on(variables, "MapBorderAlternateShow"));

  hud::applyDerived(variables, hud::WorldView{false, false});
  CHECK(!on(variables, "MapFullSize"));
  CHECK(on(variables, "MapMinSize"));
  CHECK(!on(variables, "MapBorderAlternateShow"));
}

// The composite variables are simply an "and" of two others (0x466935, 0x466950).
// It is MapFullSizeAndSpawnShow that holds the DONE and SUICIDE buttons.
static void testCombinedVariablesAreAnAnd() {
  hud::VariableMap variables;

  // The spawn screen: a full-screen map and SpawnShow from state 1.
  hud::applyState(variables, -1, 1);
  hud::applyDerived(variables, hud::WorldView{false, true});
  CHECK(on(variables, "MapFullSizeAndSpawnShow"));
  CHECK(!on(variables, "MapFullSizeAndNotSpawnShow"));

  // A full-screen map in combat: there is no spawning, so there are no buttons.
  hud::applyState(variables, 1, 0);
  hud::applyDerived(variables, hud::WorldView{true, true});
  CHECK(!on(variables, "MapFullSizeAndSpawnShow"));
  CHECK(on(variables, "MapFullSizeAndNotSpawnShow"));
}

// The table is the same as in the binary: 23 states, and the empty entries are not
// listed in it.
static void testTableMatchesTheBinary() {
  CHECK_EQ(hud::hudStates().size(), std::size_t(23));
  bool hasEmptySlot = false;
  for (const hud::StateEntry& entry : hud::hudStates()) {
    if (entry.id == 10 || entry.id == 28) hasEmptySlot = true;
    CHECK(!entry.on.empty());
  }
  CHECK(!hasEmptySlot);
}

// A show condition asks by one name for both numeric and boolean variables. Until
// now we looked only among the numbers, and `NOT MapMinSize 1` came out true
// **always** — the big map's bar was drawn over the minimap in the corner.
static void testShowValueReadsBothDictionaries() {
  hud::VariableMap flags;
  std::map<std::string, float> values;
  values["FriendlyCPs"] = 0.25f;
  flags["MapMinSize"] = true;
  flags["CommanderShow"] = false;

  CHECK(std::abs(hud::showValue(flags, values, "FriendlyCPs") - 0.25f) < 0.001f);
  CHECK(std::abs(hud::showValue(flags, values, "MapMinSize") - 1.0f) < 0.001f);
  CHECK(std::abs(hud::showValue(flags, values, "CommanderShow")) < 0.001f);
  // One we have not heard of is zero, as in the engine.
  CHECK(std::abs(hud::showValue(flags, values, "NoSuchThing")) < 0.001f);
}

TEST_MAIN({
  testStateTurnsOnItsOwn();
  testShowValueReadsBothDictionaries();
  testBigMapKeepsTheHud();
  testMapMenuKeepsTheHud();
  testStateLeavesNoTail();
  testRepeatedStateChangesNothing();
  testCombatSetNeedsAPlayer();
  testMapSizeDrivesThree();
  testCombinedVariablesAreAnAnd();
  testTableMatchesTheBinary();
})
