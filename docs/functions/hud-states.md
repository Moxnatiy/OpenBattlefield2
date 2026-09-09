# HUD states

`hudBuilder` only builds the tree; what of it is visible is decided by a
separate state machine in code. That is the answer to "why isn't the spawn
screen in the game held down with a key": it is not bound to a key at all —
it is a **state**.

## Where it is

Two functions in BF2.exe work through 32-entry jump tables:

* `0x7862ce` — takes the state through the virtual call `*0x1e4(%edx)`, table
  `0x786f88`;
* `0x786751` — takes the state as an argument, table `0x787008`.

Every handler is a long chain of identical triples:

```
push  $0 or $1           the value
sub   $0x1c, %esp        builds the string
push  $0x89f194          the variable's name
call  *0x87f46c          the string constructor
call  *0xc(%ebx)         setVariable(name, value)
```

The handlers stand one after another and **fall through**: every entry turns
off everything below it and then turns on its own. That is why the table's 32
entries give only 24 distinct entry points.

## The table

Taken from the binary by the script `tools/hud_states.py` (it reads the jump
table and takes the block chains apart), not retyped by hand:

| state | handler | turns on | turns off |
|---|---|---|---|
| 0 | 0x786761 | `ShowIngameHud`, `MapShow`, `MapBorderShow` | 4: `ScoreboardShow`, `SpawnShow`, `CommanderInterfaceShow`, `CommanderShow` |
| 1 | 0x78683e | `ShowIngameHud`, `MapBorderAlternateShow`, `SpawnShow`, `KitsShow`, `MapMenuShow` | 2: `ScoreboardShow`, `MembersShow` |
| 2 | 0x78682d | `MapShow` | 0: — |
| 3 | 0x786a2d | `SquadInterfaceShow` | 0: — |
| 4 | 0x786a4f | `RadioInterfaceShow` | 0: — |
| 5 | 0x786a60 | `RadioVehicleInterfaceShow` | 0: — |
| 6 | 0x786a71 | `SpottedInterfaceShow` | 0: — |
| 7 | 0x786a1c | `SquadLeaderInterfaceShow` | 0: — |
| 8 | 0x786a3e | `CommanderInterfaceShow` | 0: — |
| 9 | 0x786adf | `ScoreboardShow`, `LevelsListShow` | 0: — |
| 10 | 0x786f59 | *(empty)* | |
| 11 | 0x786b09 | `SetupShow` | 21: `ShowIngameHud`, `SpawnShow`, `RadioInterfaceShow`, `SpottedInterfaceShow`, `RadioVehicleInterfaceShow`, `SquadInterfaceShow`, `SquadLeaderInterfaceShow`, `CommanderInterfaceShow`, `MapMenuShow`, `SquadLeaderMenuShow`, `CommanderMenuShow`, `ChoiceMenuShow`, `CommanderRadioShow`, `ScoreboardShow`, `LevelsListShow`, `RenameSquadShow`, `VictoryShow`, `VictoryRankShow`, `VoipListShow`, `InviteListShow`, `CommanderShow` |
| 12 | 0x786f59 | *(empty)* | |
| 13 | 0x786f59 | *(empty)* | |
| 14 | 0x786f59 | *(empty)* | |
| 15 | 0x786aa4 | `CommanderShow` | 1: `SpawnShow` |
| 16 | 0x786988 | `CommanderRadioShow` | 0: — |
| 17 | 0x7869c0 | `MapShow`, `SpawnShow`, `MembersShow` | 1: `KitsShow` |
| 18 | 0x786999 | `MembersShow`, `SpawnShow` | 1: `KitsShow` |
| 19 | 0x786944 | `MapMenuShow` | 0: — |
| 20 | 0x786955 | `SquadLeaderMenuShow` | 0: — |
| 21 | 0x786966 | `CommanderMenuShow` | 0: — |
| 22 | 0x786f59 | *(empty)* | |
| 23 | 0x786f59 | *(empty)* | |
| 24 | 0x786f59 | *(empty)* | |
| 25 | 0x786f59 | *(empty)* | |
| 26 | 0x786ace | `InviteListShow` | 0: — |
| 27 | 0x786977 | `ChoiceMenuShow` | 0: — |
| 28 | 0x786f59 | *(empty)* | |
| 29 | 0x786d22 | `SetupShow` | 0: — |
| 30 | 0x786a82 | `DemoCameraInterfaceShow` | 0: — |
| 31 | 0x786a93 | `DemoRecInterfaceShow` | 0: — |

Entries 10, 12–14, 22–25 and 28 lead to the shared empty handler 0x786f59.

## How the transition actually works

`HudObject::setState(new)` is **0x786260**, and it holds two switches in a
row:

1. the first is on the **current** state (`[0xa10890]->vtbl[0x1e4]()`): it
   turns off what the old state showed;
2. the second is on the **new** one (table 0x787008): it turns on its own.

So a transition leaves no tails behind not because the handlers fall through
but because the old state first cleans up after itself.

**And that is not the same as "turn everything off and turn on what is
needed".** Most states turn nothing off: state 2 (the big map) only turns on
`MapShow`, and `ShowIngameHud` from state 0 stays — which is why in the
original the rest of the HUD is still visible under the big map. Until now we
turned everything off wholesale, and under the big map not even the map
itself was left: it lives under `IngameHud`, and that under `ShowIngameHud`.

State 11 is a case apart: it turns off 21 screens at once and turns on
nothing. It is "clear everything away".

### The first switch: what the old state turns off

It has no jump table, so `hud_states.py` does not see it — this was written
out from taking the function itself apart. Before the switch, on any
transition, `SetupShow` (0x786289), `DemoRecInterfaceShow` (0x7862a2) and
`DemoCameraInterfaceShow` (0x7862bb) are turned off.

| old state | turns off |
|---|---|
| 0 | `VoipListShow` |
| 1 | `SpawnShow`, if the new one is neither 9 nor 12 (0x7862ea) |
| 2, 10, 13, 14, 22–25, 28 | — |
| 3 | `SquadInterfaceShow` |
| 4, 5 | `RadioInterfaceShow`, `RadioVehicleInterfaceShow` (0x786456) |
| 6 | the same plus `SpottedInterfaceShow` (0x78643d) |
| 7 | `SquadLeaderInterfaceShow` |
| 8 | `CommanderInterfaceShow` |
| 9, 12 | `ScoreboardShow`, `LevelsListShow`, `ServerInfoSelected` (0x7864be) |
| 11 | `VictoryShow`, `VictoryRankShow` (0x786501) |
| 15 | `CommanderShow` — under the condition `[0xa10890]->vtbl[0x1f4]()` (0x78649d), **what that check is has not been established** |
| 16 | `CommanderRadioShow` |
| 17, 18 | `SpawnShow`, if the new one is not 9, 12, 20, 26, 13, 19 (0x78631a) |
| 19 | `MapMenuShow` |
| 20 | `SquadLeaderMenuShow`, `InviteListShow` (0x786350) |
| 21 | `CommanderMenuShow` |
| 26 | `InviteListShow` |
| 27 | `ChoiceMenuShow` |
| 29 | `SetupShow` |
| 30 | `DemoCameraInterfaceShow` |
| 31 | `DemoRecInterfaceShow` |
| outside 0..31 | `default` (0x78653c…0x786735): clear absolutely everything — 22 variables |

In the code that is `hudLeaveStates()` and
`applyState(variables, old, new)`.

## Derived variables: what the engine computes every frame

Besides the states, two more functions write HUD variables, and **they run
every frame**, not once at level start. It is precisely because of them that
in the game, behind the spawn screen, neither the health nor the ammo bars
are visible.

### 0x466930 — the map's size

| Variable | From |
|---|---|
| `MapFullSize` (field 0x1d7) | `[0xa10890]->vtbl[0x250]()->+0x68c` |
| `MapMinSize` (0x1d8) | the same, `+0x68d` |
| `MapBorderAlternateShow` (0x1d0) | the negation of `MapMinSize` (0x4669ae, `SETZ`) |
| `MapFullSizeAndSpawnShow` (0x1da) | `MapFullSize && SpawnShow` (0x466935, 0x466950) |
| `MapFullSizeAndNotSpawnShow` (0x1d9) | `MapFullSize && !SpawnShow && !player->+0x24f` |

So `MapFullSizeAndSpawnShow`, which we used to turn on "so DONE would be
visible", is not a separate variable but simply an "and" of two others. Since
it has been computed nobody sets it by hand: checked on the spawn screen —
the DONE button is in place.

The block at 0x466930 is the beginning of **0x4668d0**, the update function
of `HudInformationLayer`, and in the whole function one more term is visible:
`MapFullSizeAndNotSpawnShow` has a third factor, the player's flag
**+0x24f**, whose purpose is not established. We do not have it.

The field names from here on come from the registry in
docs/functions/hud-variables.md: it says which variable lives in which field
(`SpawnShow` — +0x1d2, `MapFullSize` — +0x1d7).

### 0x78d0f0 — the combat set by the current player

```
player = [0xa08f60]->vtbl[0x30]()
if there is no player                    -> touch nothing
if !player->vtbl[0x68]()                 -> 0x78d2d9
or [0xa10890]->vtbl[0x34c](player)       -> 0x78d2d9
otherwise                                -> 0x78d154: PlayerHealthShow = 1
```

`0x78d2d9` turns off at once `SquadInfoBarShow` (0x295), `ShowCommanderIcon`
(0x296), `ShowSquadIcon` (0x297), field 0xb8, `PlayerHealthShow` (0x24b) and
`PlayerStaminaShow` (0x245).

`PlayerStaminaShow` is on top of that computed separately at 0x78acf1 — from
comparing the stamina itself (field 0x1ac) against a constant: the bar
appears when the stamina is not full.

Ammo is turned on by the weapon update: `PrimaryAmmoBarShow` at 0x7a5bae,
`PrimaryClipsShow` at 0x7a5bb5, `PrimaryAmmoShow` at 0x7a8a18.

## A HUD variable is a field of an object

There is no dictionary of variables in the game at all. During startup
`registerVariable` binds a name to a **field** of the HUD object, and from
then on the engine writes to the field. There are four overloads: one through
the virtual method table (`[edx+0x10]`) and three direct calls — 0x466240,
0x4664e0, 0x466630.

`tools/hud_fields.py` pulls the whole list out of the binary (252 variables
with their offsets), and `--writers` additionally shows the places that write
into a field — a constant or a computed value. That is exactly how everything
above was found.

## What follows from this

* **The spawn screen is state 1**, not holding Enter. Together with
  `SpawnShow` it turns on `KitsShow` too: which is why in the game the kit
  column is visible right away, and why we had to turn both variables on by
  hand.
* **The scoreboard is state 9**, and it also turns on `LevelsListShow`.
* Every state **turns off** all the variables below it in the chain, so
  transitions leave no tails from the previous screen.
* `HudState` from `.con` (`setNodeLogicShowVariable EQUAL HudState 0`) is that
  same quantity; in the data it occurs only twice, because the rest of the
  work is done not by a condition in a node but by the state transition
  itself.
