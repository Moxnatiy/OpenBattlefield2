# What the interface does: the list of console commands

A HUD button has no logic of its own. It runs **an ordinary console
command** given in the data:

```
hudBuilder.createButtonNode Kit0NotSelected SelectKit0 10 73 246 69
hudBuilder.setButtonNodeConCmd "spawnManager.setPlayerKit 0"
```

So the interface's "backend" is not a separate system but exactly the list
of commands the game hangs on its buttons. That is what has to be
implemented, rather than inventing the logic again.

Three commands set the action:

| command | count | what |
|---|---|---|
| `setButtonNodeConCmd` | 381 | left button |
| `setButtonNodeAltConCmd` | 30 | right |
| `setListNodeConCmd` | 15 | a list row (the index comes before the command) |

That is **426 calls, 191 distinct commands, 22 objects** in total. Dumped
by `tools/hud_commands.py`.

## The whole list, by object

| object | calls | methods |
|---|---|---|
| `sound` | 181 | `playSound` |
| `CommanderMenu` | 65 | `deselect`, `sendRadioMessage`, `singleClick`, `doubleClick`, `rightClick` |
| `MiniMap` | 20 | `setPaintKit`, `setPaintVehicle`, `setZoom`, `setPaintAllKits`, `toggleShowKits`, `setPaintAllVehicles` |
| `Commander` | 19 | `sendOrder`, `accept`, `satellite`, `toggleArtilleryState`, `toggleUAVState`, `toggleSupplyState`, `commanderResign`, … |
| `spawnManager` | 17 | `setPlayerKit`, `selectNextUnlock`, `setPlayerTeam`, `commitSuicide` |
| `game` | 15 | `radioMessage`, `simulationRate` |
| `SquadLeader` | 13 | `sendOrder`, `sendRequest` |
| `SquadMenu` | 13 | `setShowInviteList`, `setSquadCreateSelect`, `createSquad`, … |
| `Radio` | 12 | `sendSpottedMessage`, `setSpottedMenuActive` |
| `RadioInterface` | 11 | `selectOrder` |
| `RadioVehicleInterface` | 11 | `selectOrder` |
| `SquadLeaderInterface` | 11 | `selectOrder` |
| `hudManager` | 7 | `addFavouriteServer`, `setDone`, `enableSay*ChatBox` |
| `demo` | 6 | `togglePlayerDemo`, `toggleCameraDemo`, … |
| `hudItems` | 5 | `setBool` |
| `levelsList` | 4 | `setVoteMapShow`, `singleClick` |
| `CommanderInterface` | 4 | `selectOrder` |
| `SquadInterface` | 4 | `selectOrder` |
| `scoreboard` | 3 | `setToggleShow` |
| `Scoreboard` | 2 | `singleClick` |
| `SpawnManager` | 2 | `toggleMembers` |
| `Minimap` | 1 | `setZoom` |

The case of the first letter is inconsistent in the data (`Scoreboard` and
`scoreboard`, `MiniMap` and `Minimap`, `SpawnManager` and `spawnManager`)
— the game's console does not distinguish it, so neither do we.

## Nearest target: the spawn screen

To make it work fully, only seven methods are needed:

| command | what it does |
|---|---|
| `spawnManager.setPlayerKit <0..6>` | pick a class |
| `spawnManager.setPlayerTeam <1\|2>` | pick a team |
| `spawnManager.selectNextUnlock <0..6>` | the unlock arrow |
| `spawnManager.commitSuicide` | suicide |
| `SpawnManager.toggleMembers <0\|1>` | the KIT / SQUAD tabs |
| `hudManager.setDone 1` | the DONE button |
| `sound.playSound <name>` | the click sound |

The click mechanism is already in place — `hud::buttonAt` finds the button
under the cursor and `engine.console().executeLine` runs its command; that
is how the main menu worked. What is left is wiring it to the spawn screen
and writing those seven handlers.
