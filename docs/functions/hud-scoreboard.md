# The scoreboard: its lists, their rows, and how a list node draws them

The scoreboard's plates and headers are ordinary `hudBuilder` nodes
(`Menu/HUD/HudSetup/HudElementsScoreboard.con`). What is **not** in the data is
every row: the player lists are `createListNode`s whose contents are written by
code every frame. This file is that code, whole, from `BF2.exe`.

Three pieces work together:

| engine class (checked build's file) | what it does | where |
|---|---|---|
| `Scoreboard` (`BF2/Menu/Hud/Scoreboard.cpp`) | owns five list-data objects and fills them from the players | ctor 0x7a4870, `init` 0x7a2280, `update` 0x7a4c80 |
| `ListBoxData` (`dice::meme::ListBoxData`, vtable 0x936880) | columns, and per column a vector of cells | ctor 0x7c7fd0 |
| `Bf2NewListBoxNode` (vtable 0x938418) | the HUD node: plate, border, and the cells | render 0x7c5c80 |

## Which list a node shows: `setListNodeData`

`setListNodeData <n>` is answered by the HUD object's slot 0x1cc,
**0x7af170** — a switch on `n`:

| n | list | node in the data |
|---:|---|---|
| 1 | `Scoreboard +0x370` — the **enemy** players (0x7a2b20(1)) | `EnemyScoreList` |
| 2 | `Scoreboard +0x368` — the **friendly** players (0x7a2b20(2)) | `FriendlyScoreList` |
| 3 | `[hud+0x144]->vtbl[0x64]()` | `ChatList` |
| 4 | `Scoreboard +0x378` — the final scoreboard | — |
| 5 | `[hud+0x138] +0x124` | `SquadMemberList` |
| 6 | `[hud+0x13c] +0x148` | `CommanderSquadList` |
| 7 | `[hud+0x140]->vtbl[0x64]()` | `CommanderChatList` |
| 8 | `[hud+0x138] +0x128` | `SquadInviteList` |
| 9 | `Scoreboard +0x374` — the enemy team's total (0x7a2b40(1)) | `EnemyTeamTotalScoreList` |
| 10 | `Scoreboard +0x36c` — the friendly team's total (0x7a2b40(2)) | `FriendlyTeamTotalScoreList` |
| 11 | `[hud+0x124] +0x5c` | `VoipSquadList` |
| 12 | `[hud+0x128] +0x50` | `MapList` |

`hud+0x120` is the `Scoreboard`. The other objects (+0x124..+0x144) are not
taken apart here.

## `Scoreboard`: the object

Constructor **0x7a4870**:

| field | value | what |
|---|---|---|
| +0x08 | argument | the texture manager (`vtbl[0x8c](path, 1)` loads a texture) |
| +0x0c | 0 | the local player's row, when found (written by 0x7a38d0) |
| +0x10 | 0x15 (21) | rows a player list shows (`ListBoxData +0x2c`) |
| +0x14 | 0 | the local player's squad (`update`) |
| +0x18 | -1 | the local player's index (`update`) |
| +0x1c | 2 | the local player's team (`update`) |
| +0x20 | -1 | a player index a kick vote is about (0x7a3b26) |
| +0x24, +0x25 | 0 | the local player is squad leader / commander (`update`) |
| +0x28 | 0 | a row background's y offset |
| +0x2c | 19.0 | a row background's height |
| +0x30 | 2.0 | an icon's y offset in its row |
| +0x34 | 0.0 | a text's y offset in its row (not used by the renderer) |
| +0x38 | 0 | the row counter while a list is filled |
| +0x3c | 0 | "was visible last frame" |
| +0x60.. | | per team, the sum of pings (0x7a41fc) |
| +0x6c, +0xf0, +0x174, +0x1f8, +0x27c | | per team × 11 squads: ping, score, kills, deaths, teamwork sums |
| +0x300..+0x320 | 5, 5, 21, 44, 212, 244, 276, 308, 340 | the column x positions |
| +0x324 | `Ingame/Respawn/iconframe.tga` | |
| +0x328 | `Ingame/Scoreboard/Icons/checkbox_empty.tga` | |
| +0x32c | `Ingame/Kits/Icons/kit_Commander.tga` | |
| +0x330 | `…/checkbox_voipMuteGrey.tga` | |
| +0x334 | `…/checkbox_voipMute.tga` | |
| +0x338 | `…/checkbox_voipTalk.tga` | |
| +0x33c | `…/checkbox_voipInactive.tga` | |
| +0x340 | `…/vote_square.tga` | |
| +0x344, +0x348 | `…/kickbutton_up.tga`, `…/kickbutton_down.tga` | |
| +0x34c, +0x350, +0x354 | `…/kickvote_frame.tga`, `…/kickvote_yes.tga`, `…/kickvote_no.tga` | |
| +0x364 | 0 | `ToggleSquads` |
| +0x365 | 1 | `ToggleScore` |
| +0x366 | 0 | `ToggleManage` |
| +0x367 | 1 | `TeamVoteOnly` |
| +0x368, +0x36c, +0x370, +0x374, +0x378 | | the five lists (friendly, friendly total, enemy, enemy total, final) |

### `init` 0x7a2280: the columns

Every list is a `ListBoxData` (0x68 bytes, ctor 0x7c7fd0 adds column 0 at x 0).
`addColumn(x, toNext, param)` is 0x7c5850; `addHiddenColumn` 0x7c59b0.

| list | columns after 0 | hidden | `+0x2c` rows | `+0x21` | `+0x22` | `+0x23` |
|---|---|---|---:|---:|---:|---:|
| friendly, enemy players | 5, 21, 44, 212, 244, 276, 308, 340, 340 — all `toNext` 1 | one (column 10) | 21 | 1 | 1 | 0 |
| both totals | the same, but column 3 (44) has `toNext` **0** | none | 1 | 0 | 1 | — |
| final | 0x94, 0xb3, 0xd4, 0xf4 | — | 0x14 | — | — | — |

### `update(visible)` 0x7a4c80

Only while visible. From the local player (`playerManager->vtbl[0x30]`,
`getLocalHumanPlayer`): squad (+0x14, `vtbl[0x108]`), squad leader (+0x24,
`vtbl[0x138]`), commander (+0x25, `vtbl[0x110]`), team (+0x1c, `vtbl[0xe4]`),
index (+0x18, `vtbl[0x50]`). Then:

1. the friendly list with the local team, the enemy list with `2 - (team != 1)`
   (0x7a4b30);
2. on the first visible frame, the row count (0x7a2850);
3. both totals (0x7a2b60 with squad 0xb).

### A team's rows: 0x7a4b30

The list is cleared (0x7c89b0) and the row counter reset. In the squad view
(`ToggleSquads` and the local team) squads 1..9 and then 0 go through 0x7a47c0 — a
squad header row (0x7a3310) and its members. Otherwise — the default — the team's
players come from `playerManager->vtbl[0x44](team)` and go through 0x7a4820:
the team's sums are cleared (0x7a28b0) and every player is a row (0x7a38d0).

`vtbl[0x44]` is `getPlayersSortedByScore(int team)`: the Linux vtable
(`PlayerManager`, 0xb8c220) holds `getPlayers()`, `getPlayersSortedByScore()`,
`getPlayersSortedByRank()`, `getPlayers(int)`, … in declaration order, and MSVC
groups the overloads, which puts `getPlayersSortedByScore(int)` at 0x44 — the
neighbours agree (0x30 `getLocalHumanPlayer`, 0x58 `getPlayersInSquadSortedByScore`).
The sort (Linux `PlayerManager::sortByScore`, 0x6815f0) is a selection sort,
best first: higher score; on a tie higher skill score; then **fewer** deaths;
then longer `getTimeConnected`. Exact ties keep the list's order.

### A player's row: 0x7a38d0

The text colour, from four tables of three RGBA each, indexed by `1` for the
enemy list and `2` for the friendly one (`localTeam == team`):

| when | table | friendly | enemy |
|---|---|---|---|
| dead (`player->vtbl[0x68]` false — `Player::getIsAlive`, Linux 0x4a23f0) or `hud->vtbl[0x34c](player)` | 0xa1cf58 (init 0x864a80) | 0x97e0a0 (0.290, 0.271, 0.208) | 0x97df10 (the same) |
| alive, in the local player's squad (>0) on the friendly list — or the local player himself while commander | 0xa1cc78 (init 0x864920) | 0x97e080 (0.031, 0.467, 0.008) | 0x97def0 (the same) |
| alive, otherwise | 0xa1c7b8 (init 0x8649d0) | 0x97e090 (0, 0.380, 1) | 0x97df00 (0.992, 0, 0.004) |

`hud->vtbl[0x34c]` (0x751520) asks the player's object `+0x3c->vtbl[0x5c]() == 1`;
what that is, is **not established**.

The cells, when `ToggleManage` is off:

| column | x | cell | size | offset | colour | source |
|---:|---:|---|---|---|---|---|
| 0 | 0 | texture `iconframe.tga` (+0x324) | 15×15 | x +5 (`+0x300`), y +2 (`+0x30`) | 0x97e2b0 (0.576, 0.573, 0.451, 1) | 0x7a3ede |
| 1 | 5 | texture `hud->vtbl[0x64](player, 0, 0)` — the kit icon | 15×15 | x 0, y +2 | 0x97e2a0 (0.384, 0.361, 0.255, 1) | 0x7a3f21 |
| 2 | 21 | `ToggleScore` on: texture `hud->vtbl[0x68](player)` — the rank | 16×16 | x 0, y +1 | white | 0x7a3f86 |
| 3 | 44 | text `§1%s§0`, the name (`player->vtbl[0x20]`) | | x 0 (−20 when `ToggleScore` is off) | the table above | 0x7a404c |
| 4 | 212 | text `§1%d§0`, score (`score[0]`) | | centred | | 0x7a40a0 |
| 5 | 244 | teamwork (`score[1]`) | | centred | | 0x7a40e5 |
| 6 | 276 | kills (`score[5]`) | | centred | | 0x7a412c |
| 7 | 308 | deaths (`score[4]`) | | centred | | 0x7a4173 |
| 8 | 340 | empty | | | | 0x7a41a2 |
| 9 | 340 | ping (`player->vtbl[0x164]`) | | centred | | 0x7a41ed |
| 10 | hidden | `%i`, the player's index | | | | 0x7a4489 |

The score block `player->vtbl[0x170]` is `PlayerScoreData`; its fields are named
by the Linux server's `pmgr_getScore` (0x4fc780): `[0]` score, `[1]` rplScore
(teamwork), `[2]` skillScore, `[3]` cmdScore, `[4]` deaths, `[5]` kills,
`[6]` TKs, `[7]` fracScore (float), `[8]` rank. The header art agrees
(`Ingame/Scoreboard/scoreboard_icons.tga`: a cup, the teamwork emblem, a sight, a
skull, a network). **Without a score block** columns 4–9 get empty cells
(0x7a4353..0x7a443e).

The kit icon (0x74f320 with the third argument 0): `icon_Death.tga` when the
player is dead or `hud->vtbl[0x34c]`; `icon_Faded.tga` when he is on another team
than the local player; otherwise his kit's `VehicleHud` component
(`vtbl[0xc](0xc4d7)`, `+0x54`) answers `vtbl[0x44](0)` — **which of the kit's
icons that is, is not established**. The rank (0x74f8d0):
`Ingame/GeneralIcons/Ranks/rank_%02i.tga` of `score[8]` when 0..21, otherwise
`rank_00.tga`.

Along the way it adds the ping into the team's ping sum (+0x60) and score,
kills, deaths and teamwork into the squad's sums (squad 10 for the commander).

The row's background (0x7c4af0 on column 0's cell):

* the local player's row: (0.996, 0.753, 0.082, 0.35) at 0x7a44cd, and the row
  number goes into +0x0c;
* any other row with an **odd** number: 0x97de00 (0.725, 0.710, 0.580) with alpha
  0.8, at 0x7a4549;
* both with x 0 (`+0x28`), height 19 (`+0x2c`).

### A total row: 0x7a2b60 with squad 0xb

Colour (0.796, 0.796, 0.592, 1). Columns 0 and 1 empty; column 3 is
`HUD_TEXT_MENU_SCOREBOARD_NUMBER_OF_PLAYERS` ("TOTAL PLAYERS: #PLAYERS#") with
`#PLAYERS#` replaced by `playerManager->vtbl[0x94](team)`, x −37; column 2 empty;
columns 4–7 are the team's sums of score (0x7a2900), teamwork (0x7a2970), kills
(0x7a29e0) and deaths (0x7a2a50) — each the squads 0..9 that have members, plus
the commander's; column 8 empty; column 9 the ping sum divided by the player
count. The sums are cleared afterwards.

The commander's share is odd: both 0x7a2900 and 0x7a2970 add
`+0x19c + team × 0x2c` when the team has a commander (`[0x99ef84]->vtbl[0x70]`).
The score array starts at +0xf0 and holds 3 × 11 entries, so +0x19c is past it —
it is the **kills** array's (+0x174) entry for squad 10, the commander's kills.
We never have a commander, so this is written down and not reproduced.

## `ListBoxData`: the cells

A column (0x24 bytes, 0x7c5850): `[0]` index, `+0x08..+0x10` the cells vector,
`+0x14` x, `+0x18` visible (1; 0 for the hidden one), `+0x19` `toNext`, `+0x1a`
a flag (0 here), `+0x20` a width (−1).

A cell (0x94 bytes): `+0x00` the text, `+0x1c` a wide text, `+0x38` 0,
`+0x3c..+0x48` RGBA, `+0x4c` a texture handle, `+0x50`/`+0x54` a texture's size,
`+0x58`/`+0x5c` x and y offsets, `+0x60` "is a texture", `+0x64` the horizontal
alignment, `+0x68` the vertical one (always 1), `+0x6c` "has a row background",
`+0x70..+0x7c` its RGBA, `+0x80` its height, `+0x84` its x, `+0x88` its y,
`+0x8c` −1, `+0x90` the default font (0).

| call | what |
|---|---|
| 0x7c8210 / 0x7c8120 (0x7c2bf0) | a text cell: column, text, x, y, RGBA, alignment |
| 0x7c8470 / 0x7c83d0 (0x7c2c80) | the same with a wide text |
| 0x7c81c0 / 0x7c8090 (0x7c2b70) | a texture cell by name, no size |
| 0x7c8300 / 0x7c8260 (0x7c2da0) | a texture cell: column, handle, width, height, x, y, RGBA |
| 0x7c4af0 | a row background on column 0's cell of that row |
| 0x7c89b0 | clears every column's cells |
| 0x7c5740 | posts the row count to the node |

## `Bf2NewListBoxNode`: drawing, 0x7c5c80

The node's fields, and the command that sets each (builder calls in brackets):

| field | default (ctor 0x7c54xx / `createListNode` 0x796710) | set by |
|---|---|---|
| +0x18 | 12 / the 7th argument | `createListNode … <row height> <flag>` (slot 0x10c) |
| +0x1c | 1.0 | `setListNodeRowSpacing` (slot 0x110, 0x795790) |
| +0x24 | 4 | `setListNodeScrollbar a b`: a (slot 0x160, 0x7957c0) |
| +0xf8 | 1.0 | `setListNodeScrollbar a b`: b (slot 0x15c) |
| +0x95 | 0 | `setListNodeBorder` turns it on (slot 0x114) |
| +0x98, +0x9c, +0xa0, +0xa4 | 2 | `setListNodeBorder` **top, bottom, left, right** (0x795720 → slots 0x11c, 0x120, 0x124, 0x128: 0x5c9560, 0x4b3a90, 0x4b3aa0, 0x4b7320) |
| +0xa8..+0xb4 | (1, 0, 0, 0) | `setListNodeBackgroundColor` (0x795600, slots 0x12c..0x138) |
| +0xb8..+0xc4 | (1, 1, 0, 0) | `setListNodeBorderColor` (0x7956c0, slots 0x13c..0x148) |

The order of the draw:

1. the background colour over the **whole** rectangle;
2. when bordered: four strips in the border colour — top (full width, `top`
   high), right, bottom, left;
3. for every column, for every visible row (`+0x08` the first, as many as
   `min(list +0x2c, rows)`), at `y = rect.y + top + spacing + row × (height + spacing)`:
   * the row's background from column 0's cell, at `x = rect.x + left + bg.x`,
     `width − left − right − bg.x` wide, `bg.height` high. It sits inside the
     column loop, but the node keeps a list of the rows whose background is
     already down (searched at 0x7c6217, added to at 0x7c62b8 — the decompiler
     shows it as an anonymous `std::list` from 0x594b70), so it is drawn **once**,
     in column 0's pass, under all of the row's cells;
   * when the column is visible and the row fits (`y ≤ rect.y + top + h − bottom − height`):
     the cell.

A texture cell: at `rect.x + column.x + cell.x`, `y + cell.y`, its own size and
colour.

A text cell: its text is walked for codes — `§0`..`§3` pick the node's font of
that number (`setListNodeFont <file> <n>`), `§C`/`§c` toggles a highlight colour
(0.996, 0.64, 0.169, 1). The width is the sum of the glyphs' advances. Its x:

| `+0x64` | x |
|---:|---|
| 0 | `rect.x + column.x + cell.x` |
| 1 | `rect.x + column.x + cell.x + (right − column.x) / 2 − width / 2` |
| 2 | `rect.x + right − cell.x − width` |

where `right` is the column's right edge (0x7c4d20): the next visible column's x
when the list's `+0x22` and the column's `toNext` are set, otherwise the node's
width less the right border; then, when the list's `+0x21` is set, less
`floor(+0xf8 + +0x24)` of the node if it overlaps that, otherwise less 6.

Its y: the vertical alignment (`+0x68`, always 1 here) centres the glyphs' ink —
from the lowest glyph top (starting at 10) to the highest glyph bottom — in the
row's height. Both x and y are floored.

## Where the numbers come from

On `--connect` a player's alive flag, score block, ping, squad and rank are his
own networkable's (docs/functions/player-state.md); `app/scoreboard_players`
hands them over. `--hosted` has none of it yet.

The kit icon of another team (`icon_Faded.dds`) is a paletted DDS — a white
frame and question mark — which the texture loader now reads
(`DDPF_PALETTEINDEXED8`).

## What is left

* who writes `ServerNameString`, `ServerIPString`, `ServerPortString` (the
  layer's +0x57c, +0x580, +0x584, registered by name at 0x46b314; nothing else in
  the exe, the menus or the Python writes them by name) — **source not found**,
  so the SERVER INFO bar's address is empty;
* the squad view (`ToggleSquads`: 0x7a3310, 0x7a3140, 0x7a4780) and the manage
  view (`ToggleManage`: the VOIP and kick-vote columns 0x7a3bae..0x7a3e5b);
* which of the kit's icons column 1 shows (the `VehicleHud` component's
  `vtbl[0x44](0)`);
* `hud->vtbl[0x34c]`;
* the rows' mouse: `setListNodeConCmd 1 "Scoreboard.singleClick 2"`;
* the scrollbar (drawn when there are more rows than `+0x2c`, 0x7c7a30 on);
* the final scoreboard (list 4).
