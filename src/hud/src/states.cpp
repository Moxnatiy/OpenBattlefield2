#include "obf2/hud/states.h"

namespace obf2::hud {
namespace {

// Change a value and say whether it really changed.
bool set(VariableMap& variables, const char* name, bool value) {
  bool& slot = variables[name];
  if (slot == value) return false;
  slot = value;
  return true;
}

}  // namespace

const std::vector<StateEntry>& hudStates() {
  // Made from the output of `tools/hud_states.py`, which reads the jump table
  // 0x787008 straight out of BF2.exe. Entries 10, 12-14, 22-25 and 28 lead to a
  // shared empty handler, so they are not here.
  static const std::vector<StateEntry> table = {
      {0,
       {"ShowIngameHud", "MapShow", "MapBorderShow"},
       {"ScoreboardShow", "SpawnShow", "CommanderInterfaceShow", "CommanderShow"}},
      {1,
       {"ShowIngameHud", "MapBorderAlternateShow", "SpawnShow", "KitsShow", "MapMenuShow"},
       {"ScoreboardShow", "MembersShow"}},
      {2, {"MapShow"}, {}},
      {3, {"SquadInterfaceShow"}, {}},
      {4, {"RadioInterfaceShow"}, {}},
      {5, {"RadioVehicleInterfaceShow"}, {}},
      {6, {"SpottedInterfaceShow"}, {}},
      {7, {"SquadLeaderInterfaceShow"}, {}},
      {8, {"CommanderInterfaceShow"}, {}},
      {9, {"ScoreboardShow", "LevelsListShow"}, {}},
      {11,
       {"SetupShow"},
       {"ShowIngameHud", "SpawnShow", "RadioInterfaceShow", "SpottedInterfaceShow",
        "RadioVehicleInterfaceShow", "SquadInterfaceShow", "SquadLeaderInterfaceShow",
        "CommanderInterfaceShow", "MapMenuShow", "SquadLeaderMenuShow", "CommanderMenuShow",
        "ChoiceMenuShow", "CommanderRadioShow", "ScoreboardShow", "LevelsListShow",
        "RenameSquadShow", "VictoryShow", "VictoryRankShow", "VoipListShow", "InviteListShow",
        "CommanderShow"}},
      {15, {"CommanderShow"}, {"SpawnShow"}},
      {16, {"CommanderRadioShow"}, {}},
      {17, {"MapShow", "SpawnShow", "MembersShow"}, {"KitsShow"}},
      {18, {"MembersShow", "SpawnShow"}, {"KitsShow"}},
      {19, {"MapMenuShow"}, {}},
      {20, {"SquadLeaderMenuShow"}, {}},
      {21, {"CommanderMenuShow"}, {}},
      {26, {"InviteListShow"}, {}},
      {27, {"ChoiceMenuShow"}, {}},
      {29, {"SetupShow"}, {}},
      {30, {"DemoCameraInterfaceShow"}, {}},
      {31, {"DemoRecInterfaceShow"}, {}},
  };
  return table;
}

const std::vector<StateEntry>& hudLeaveStates() {
  // The first switch in 0x786260 — on the old state. Written out from taking the
  // function itself apart (the branches' addresses in brackets); it has no jump
  // table, so `hud_states.py` does not see it.
  //
  // The three lines before the switch (0x786289, 0x7862a2, 0x7862bb) turn off
  // `SetupShow`, `DemoRecInterfaceShow` and `DemoCameraInterfaceShow` on any
  // transition — they are in `kAlwaysOff` below.
  static const std::vector<StateEntry> table = {
      {0, {}, {"VoipListShow"}},
      // 1: `SpawnShow`, but conditioned on the **new** state — see applyState.
      {1, {}, {}},
      {3, {}, {"SquadInterfaceShow"}},
      {4, {}, {"RadioInterfaceShow", "RadioVehicleInterfaceShow"}},                        // 0x786456
      {5, {}, {"RadioInterfaceShow", "RadioVehicleInterfaceShow"}},
      {6, {}, {"SpottedInterfaceShow", "RadioInterfaceShow", "RadioVehicleInterfaceShow"}},  // 0x78643d
      {7, {}, {"SquadLeaderInterfaceShow"}},
      {8, {}, {"CommanderInterfaceShow"}},
      {9, {}, {"ScoreboardShow", "LevelsListShow", "ServerInfoSelected"}},  // 0x7864be
      {11, {}, {"VictoryShow", "VictoryRankShow"}},                        // 0x786501
      {12, {}, {"ScoreboardShow", "LevelsListShow", "ServerInfoSelected"}},
      // 15: `CommanderShow`, but only when `[0xa10890]->vtbl[0x1f4]()` is false
      // (0x78649d). What that check is has **not been established**, so we always
      // turn it off: otherwise the commander screen would never close.
      {15, {}, {"CommanderShow"}},
      {16, {}, {"CommanderRadioShow"}},
      // 17, 18: `SpawnShow` conditioned on the new state — see applyState.
      {17, {}, {}},
      {18, {}, {}},
      {19, {}, {"MapMenuShow"}},
      {20, {}, {"SquadLeaderMenuShow", "InviteListShow"}},  // 0x786350
      {21, {}, {"CommanderMenuShow"}},
      {26, {}, {"InviteListShow"}},
      {27, {}, {"ChoiceMenuShow"}},
      {29, {}, {"SetupShow"}},
      {30, {}, {"DemoCameraInterfaceShow"}},
      {31, {}, {"DemoRecInterfaceShow"}},
  };
  return table;
}

namespace {

// The first switch's `default` branch (0x78653c..0x786735): there was no state yet
// or it is outside the table — clear absolutely everything.
const std::vector<const char*> kLeaveEverything = {
    "MapShow",           "MapBorderShow",      "SpawnShow",
    "RadioInterfaceShow", "SpottedInterfaceShow", "RadioVehicleInterfaceShow",
    "SquadInterfaceShow", "SquadLeaderInterfaceShow", "CommanderInterfaceShow",
    "MapMenuShow",       "SquadLeaderMenuShow", "CommanderMenuShow",
    "ChoiceMenuShow",    "CommanderRadioShow", "ScoreboardShow",
    "LevelsListShow",    "RenameSquadShow",    "VictoryShow",
    "VictoryRankShow",   "VoipListShow",       "InviteListShow",
    "CommanderShow"};

// The three calls before the switch (0x786289, 0x7862a2, 0x7862bb).
const std::vector<const char*> kAlwaysOff = {"SetupShow", "DemoRecInterfaceShow",
                                             "DemoCameraInterfaceShow"};

const StateEntry* find(const std::vector<StateEntry>& table, int id) {
  for (const StateEntry& entry : table) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

}  // namespace

bool applyState(VariableMap& variables, int previous, int state) {
  // We first assemble what we want and only then write: otherwise a variable the
  // old state turns off and the new one turns straight back on would look like two
  // changes, and the geometry would be rebuilt twice per frame.
  std::map<std::string, bool> target;
  const auto off = [&](const char* name) { target[name] = false; };
  const auto on = [&](const char* name) { target[name] = true; };

  for (const char* name : kAlwaysOff) off(name);

  // --- the first switch: on the old state ----------------------------
  const StateEntry* leaving = find(hudLeaveStates(), previous);
  if (leaving == nullptr) {
    // The empty branches (2, 10, 13, 14, 22-25, 28) are in the table with an empty
    // `off`; only a state outside 0..31 lands here — that is, "there was no state
    // yet".
    if (previous < 0 || previous > 31) {
      for (const char* name : kLeaveEverything) off(name);
    }
  } else {
    for (const char* name : leaving->off) off(name);
  }

  // The first switch's two conditional branches. Both turn off `SpawnShow`, but
  // not when the new state itself shows it or governs it.
  if (previous == 1 && state != 9 && state != 12) off("SpawnShow");       // 0x7862ea
  if ((previous == 17 || previous == 18) && state != 9 && state != 12 &&  // 0x78631a
      state != 20 && state != 26 && state != 13 && state != 19) {
    off("SpawnShow");
  }

  // --- the second switch: on the new state ---------------------------
  if (const StateEntry* entering = find(hudStates(), state); entering != nullptr) {
    for (const char* name : entering->off) off(name);
    for (const char* name : entering->on) on(name);
  }

  bool changed = false;
  for (const auto& [name, value] : target) changed |= set(variables, name.c_str(), value);
  return changed;
}

float showValue(const VariableMap& flags, const std::map<std::string, float>& values,
                std::string_view name) {
  const std::string key(name);
  const auto number = values.find(key);
  if (number != values.end()) return number->second;
  const auto flag = flags.find(key);
  return flag == flags.end() ? 0.0f : (flag->second ? 1.0f : 0.0f);
}

bool applyDerived(VariableMap& variables, const WorldView& view) {
  bool changed = false;

  // 0x466986: MapFullSize and MapMinSize are two flags of the map object itself
  // (fields 0x68c and 0x68d), and 0x4669ae makes MapBorderAlternateShow out of the
  // second by negation.
  changed |= set(variables, "MapFullSize", view.mapFullSize);
  changed |= set(variables, "MapMinSize", !view.mapFullSize);
  changed |= set(variables, "MapBorderAlternateShow", view.mapFullSize);

  // The composite variables. Both are written by 0x4668d0 — the same per-frame
  // function, at its start, and verbatim like this:
  //
  //   +0x1da (MapFullSizeAndSpawnShow)    = MapFullSize AND SpawnShow
  //   +0x1d9 (MapFullSizeAndNotSpawnShow) = MapFullSize AND NOT SpawnShow
  //                                         AND NOT player->+0x24f
  //
  // The fields are named after the registry: +0x1d7 `MapFullSize`, +0x1d2
  // `SpawnShow` (docs/functions/hud-variables.md). The third term in the second is
  // the player's flag +0x24f, **purpose not established**; so it is absent below,
  // and that is a difference from the original.
  const bool spawn = variables["SpawnShow"];
  changed |= set(variables, "MapFullSizeAndSpawnShow", view.mapFullSize && spawn);
  changed |= set(variables, "MapFullSizeAndNotSpawnShow", view.mapFullSize && !spawn);

  // 0x78d154 turns it on, 0x78d2f1 off — by whether there is a controlled player.
  changed |= set(variables, "PlayerHealthShow", view.hasPlayer);
  // 0x78acf1: in the game this is also a comparison of the stamina itself against
  // a constant (field 0x1ac), so the bar appears when the stamina is not full.
  // We have no stamina yet — the player is what is left.
  changed |= set(variables, "PlayerStaminaShow", view.hasPlayer);
  // 0x7a5bae, 0x7a5bb5, 0x7a8a18: the ammo is turned on by the weapon's update. We
  // do not model weapons yet, so this goes by the player too. Debt.
  changed |= set(variables, "PrimaryAmmoShow", view.hasPlayer);
  changed |= set(variables, "PrimaryAmmoBarShow", view.hasPlayer);
  changed |= set(variables, "PrimaryClipsShow", view.hasPlayer);

  // 0x78adc5, 0x78ae8c against 0x78af97: the squad. We are never in a squad.
  changed |= set(variables, "SquadInfoBarShow", false);
  changed |= set(variables, "ShowCommanderIcon", false);
  changed |= set(variables, "ShowSquadIcon", false);
  return changed;
}

}  // namespace obf2::hud
