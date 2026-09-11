# The whole HUD, screen by screen

What the game's data builds, and how much of each screen anybody on our side
fills in. Taken with `--hud-vars` against `Menu_client.zip`:

```bash
openbf2 --hosted --level strike_at_karkand --frames 10 --hud-vars
```

**676 variables asked for, 199 filled, 477 not.** A variable nobody writes is a
node that never appears, and it fails silently — which is why this is a list and
not a feeling.

The table below is every child of `IngameHud`, with the size of its subtree, how
many distinct variables that subtree reads, and how many of those nobody fills.
A screen with 0 unfilled is finished as far as the data can tell; it says nothing
about whether the pixels match, which is a separate measure (a frame dump).

| screen | nodes | vars | unfilled | file |
|---|---:|---:|---:|---|
| `SpawnMenu` | 332 | 159 | **7** | HudElementsSpawn.con |
| `SpawnInfo` | 3 | 4 | **0** | HudElementsSpawn.con |
| `MinSizeAlpha` | 3 | 4 | **0** | HudElementsMap.con |
| `SpottedMenu` | 23 | 1 | **0** | HudElementsSpottedComm.con |
| `CommanderRadio` | 23 | 1 | **0** | HudElementsCommanderRadio.con |
| `CommanderInterfaceMap` | 74 | 7 | 3 | HudElementsCommander.con |
| `Scoreboard` | 74 | 19 | 13 | HudElementsScoreboard.con |
| `MinSizeMap` | 20 | 20 | 10 | HudElementsMap.con |
| `TicketInfo` | 13 | 14 | 7 | HudElementsMap.con |
| `ItemSelectionHud` | 57 | 18 | **18** | HudElementsWeaponSelect.con |
| `PlayerNames` | 19 | 14 | 13 | hudSetupPlayerNames.con |
| `RadioRose` | 23 | 12 | 11 | HudElementsRadioComm.con |
| `RadioVehicleRose` | 23 | 12 | 11 | HudElementsRadioVehicleComm.con |
| `SquadLeaderRose` | 25 | 12 | 11 | HudElementsSquadLeaderComm.con |
| `SquadRose` | 11 | 6 | 5 | HudElementsSquadComm.con |
| `CommanderRose` | 11 | 6 | 5 | HudElementsCommanderComm.con |
| `CommanderInterfaceBottom` | 106 | 19 | 18 | HudElementsCommander.con |
| `CommanderInterfaceLeft` | 78 | 26 | 25 | HudElementsCommander.con |
| `CommanderInterfaceTop` | 21 | 12 | 7 | HudElementsCommander.con |
| `IssueOrderMenu` | 11 | 3 | 1 | HudElementsIssueOrders.con |
| `MapsList` | 17 | 5 | 4 | HudElementsLevelsList.con |
| `IngameChatInput` | 6 | 4 | 3 | HudElementsChat.con |
| `KilledInfo` | 4 | 4 | 4 | HudElementsGameInfo.con |
| `MedalArea` | 4 | 5 | 4 | HudElementsActionIcons.con |
| the action icons | 12 | 17 | 12 | HudElementsActionIcons.con |
| `DemoRecRose`, `DemoCameraRose`, `DemoPlayerName` | 29 | 9 | 7 | HudElementsDemo*.con |
| `VoipIcon` | 6 | 6 | 5 | HudElementsVoipList.con |
| the eleven weapon HUDs | 89 | 64 | 60 | HudElementsM4.con and the rest |

## What that says

**The spawn screen is done** — 7 of 159 variables left, and the seven are the
squad list, which needs squads to exist. Everything else on it pairs with the
original's frame dump (docs/research/spawn-screen-named.md).

**The scoreboard is the nearest half-built screen.** Its thirteen:
`FriendlyTeamWinsString`, `EnemyTeamWinsString` (rounds won — we count no
rounds), `ToggleSquads`/`ToggleManage` (the tabs, now switched by
`scoreboard.setToggleShow`), and nine that belong to the server-info panel
(`ServerNameString`, `ServerIPString`, `ServerPortString`, `ServerIsFavourite`,
`ServerInfoSelected`, `ActiveLevel`, `SinglePlayerActive`, `TeamVoteOnly`,
`VoteMapSelected`).

But the variables are not what is missing there. **The rows are.** Eleven
`createListNode` in the data — the two scoreboard player lists, the two team
totals, the squad list, the squad invite list, the chat, the commander's squad
and chat lists, the level list, the VOIP list — and our renderer draws a list's
border and background and **no rows at all**. The row source is
`setListNodeData <n>`, a number from 1 to 12, and what those numbers select is
**not established**: the command's handler in the binary is behind the console
property thunks at 0x881460/0x881170, and neither the server's symbols nor the
checked build name the enum.

| list | data | where |
|---|---:|---|
| `EnemyScoreList` | 1 | HudElementsScoreboard.con |
| `FriendlyScoreList` | 2 | |
| `ChatList` | 3 | HudElementsChat.con |
| `SquadMemberList` | 5 | HudElementsSquadsNew.con |
| `CommanderSquadList` | 6 | HudElementsCommander.con |
| `CommanderChatList` | 7 | |
| `SquadInviteList` | 8 | HudElementsSquadsNew.con |
| `EnemyTeamTotalScoreList` | 9 | HudElementsScoreboard.con |
| `FriendlyTeamTotalScoreList` | 10 | |
| `VoipSquadList` | 11 | HudElementsVoipList.con |
| `MapList` | 12 | HudElementsLevelsList.con |

Nor is the row's **layout** measured. The header gives the columns' places —
`FriendlyHeaderLabel` at 15 wide 138 for the name, `FriendlyHeaderIcons` at 231
wide 150 for five icons of 30 — but which number goes under which icon is a
guess until a frame dump of the original's scoreboard says so. We have none:
every dump we hold is of the spawn screen.

**The minimap in combat is half-filled.** Its ten: `LevelTime` and `TimeInfoShow`
(the round clock), `CloseToAlliedCP`/`CloseToAxisCP` (the "you are near a flag"
marks), and six capture-point bars — `EnemyFriendlyCPs`, `EnemyNeutralCPs`,
`FriendlyEnemyCPs`, `FriendlyNeutralCPs`, `TimePreCPIsNeutral`,
`TimePreCPIsTaken`. The last six are the pairs that show a flag changing hands;
we fill `FriendlyCPs` and `EnemyCPs` and none of the transitional ones. And the
same six are what makes `barKind` 2 against 3 matter (docs/functions/hud-kits.md)
— still unmeasured for want of the same dump.

**Whole screens are untouched**, and honestly so: the item wheel (18 of 18), the
commander's interface (about 280 nodes, nearly all), the five comm roses, the
eleven per-weapon HUDs, the demo player, the VOIP list, the player-name plates
over a vehicle's seats.

## The measure

Every line above falls when the screen behind it is reversed, and the count in
`--hud-vars` is the number to watch. For the two screens that are half-built and
visible — the scoreboard and the combat minimap — the thing that unblocks them is
not more reading but **one frame dump of the original taken in a battle**, with
the scoreboard open. That is the same measure CLAUDE.md's debt table already
names, and it settles at once: the scoreboard's row layout and columns, the
minimap's ticket and capture-point bars, `barKind` 2 against 3, and the bottom
corners we have never seen drawn by the original.
