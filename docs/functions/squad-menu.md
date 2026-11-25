# The squad menu: the spawn screen's SQUAD tab

`BF2/Menu/Hud/SquadMenuNew.cpp` (docs/reference/engine-source-tree.txt), read
from the Phonex 1.50 `BF2.exe` (code identical to the QA copy; they differ only
in the stats host string) with `tools/ghidra_at.sh` (headless Ghidra 12.1.4,
`tools/ghidra_scripts/DecompileAt.java`), `tools/list_cells.py` and
`tools/pe_read.py`. Our port: `src/hud/src/squad_menu.cpp`, fed by
`app/squad_roster` (`squadMenuWorld`, `sendSquadMenuAction`).

## The object

`hud->vtbl[0x254]` (0x504f30) returns `[hud+0x138]`. The HUD manager's vtable
is 0x926130 (its slot 0x34c is 0x751520, the scoreboard's known call).

| | |
|---|---|
| vtable | 0x932084: 0x7920e0 (event: 2 → 0x791a90), 0x794530 (set +4), 0x78dcb0 init, 0x5a5e20, 0x792100 `update(visible)` |
| ctor | 0x791260 (called from 0x756ce0) |
| dtor | 0x790f80 |

Fields (ctor 0x791260, init 0x78dcb0, `update` 0x792100):

| offset | init | what |
|---|---|---|
| +0x04 | arg | the texture manager (`vtbl[0x8c](path, 1)`) |
| +0x14..+0x48 | | textures: +0x14 `Respawn/button_upp`, +0x18 `button_down`, +0x1c `GeneralIcons/empty`, +0x20/+0x24/+0x28 `Scoreboard/Icons/checkbox_empty/cross/ask`, +0x2c `Respawn/button_longlight7`, +0x30 `Respawn/iconframe`, +0x34 `Respawn/lockicon`, +0x38 `checkbox_voipMuteGrey`, +0x3c `Respawn/icon_speaker`, +0x40 `icon_speakerMute`, +0x44 `Kits/Icons/kit_Commander`, +0x48 `kit_Squad` |
| +0x4c..+0x70 | 0 | per squad, `mini_Squad_%d_small.tga` loaded on first use (0x790328) |
| +0x78 | 0 | the row counter while filling |
| +0x7c | 0 | the local player's row |
| +0x80 | −1 | the local player's index (`vtbl[0x50]`) |
| +0x84 | 21 | rows a list shows (`ListBoxData +0x2c`) |
| +0x88 | 0 | the sort the fill asks for: set, by score (`[0xa08f60]` 0x58, 0x4c); clear, by rank (0x5c, 0x44) |
| +0x8c | −1 | the picked squad (0x78f270 toggles) |
| +0x90 | −1 | the picked player (0x78f240 toggles) |
| +0x94 | 0 | our squad (`vtbl[0x108]`) |
| +0x98 | 2 | our team (`vtbl[0xe4]`) |
| +0x9c | 0 | we lead our squad (`vtbl[0x138]`), `IsSquadLeader` |
| +0x9d | 0 | we are commander (`vtbl[0x110]`) |
| +0x9e | 1 | the commander row is listed |
| +0x9f | 1 | the commander row is open |
| +0xa0 | 0 | shown, `SquadMemberListShow` |
| +0xa1 | 0 | `InviteListShow` |
| +0xa2 | 0 | `IsInSquad` (our squad > 0) |
| +0xa4 | 0 | a bit per squad 0..9: open (0x78fa60; set for our own squad on show) |
| +0xa8, +0xac | 0, 19.0 | a row background's y and height |
| +0xb0, +0xb4 | 2.0, 0.0 | an icon's and a text's y in its row |
| +0xb8..+0xe4 | 5, 5, 22, 45, 85, 196, 116, 133, 216, 216, 7, 181 | the columns' x |
| +0xe8 | 0 | `SquadCreateShow` |
| +0xe9 | 0 | `SquadRenameShow` |
| +0xea | 0 | `SquadCreateLockedShow` |
| +0xec | | `SquadCreateTagString`: the first free squad's number (0x78f1b0) |
| +0xf0 | | `NewSquadName` |
| +0x104..+0x11c | | lists: invite candidates (+0x104), invited (+0x110), muted (+0x11c) |
| +0x124 | | `SquadMemberList` (`ListBoxData`, `setListNodeData 5`) |
| +0x128 | | `SquadInviteList` (8) |

Variables registered in init (0x78dcb0): `TeamTopLabelString` (+0x08),
`NrOfUnassigned` (+0x0c), `NrOfSquads` (+0x10), and the flags above.

## The member list's columns (init 0x78dcb0)

Column 0 from the `ListBoxData` ctor. 0x7c5850(x, toNext, flag) and
0x7c5900(x, width, flag; `toNext` 0, width at column +0x20):

| col | x | | col | x | |
|---:|---:|---|---:|---:|---|
| 1 | 5 | | 8 | 116 | toNext |
| 2 | 22 | toNext | 9 | 133 | toNext |
| 3 | 22 | toNext | 10 | 133 | width 80 |
| 4 | 45 | width 166 | 11 | 216 | toNext |
| 5 | 45 | width 69 | 12 | 216 | toNext |
| 6 | 85 | width 30 | 13 | hidden | the row's id |
| 7 | 116 | toNext | 14 | hidden | the row's kind |

List flags: +0x2c ← +0x84 (21), +0x21 ← 1, +0x23 ← 0 (purpose not
established). The invite list: column 1 at +0xe4 (181), then a hidden column.

The hidden columns are named cells (0x7c8090 with `"%i"`), read back through
0x7c48d0 (the picked row's — `ListBoxData +0x08` — cell text in a column).
Kinds: 0 commander, 2 squad, 3 create, 4 player.

## `update(visible)` (0x792100)

With a local player: reads +0x94, +0x9c, +0x9d, +0x98, +0x80, sets +0xa2; on
the visible edge opens our squad's bit and posts the row count (0x78e2c0);
joining or creating a squad opens it too: HUD events 0x16 (joined) and 0x18
(new leader) for the local player call 0x78fa60(squad, 1) (0x759e50 at
0x75b2a0 / 0x75b36a); event 0x15 (left) calls it with 0 (0x75b1d4). Ours
stands the change of our squad number in for the events, and does not close
on 0x15;
then 0x78e130 (`TeamTopLabelString`: `HUD_TEXT_MENU_SPAWN_SQUAD_SOLDIER` /
`_COMMANDER` / `_SQUADLEADER` as `"%s - %s"` with the team — ours writes the
role alone; the team half is not ported),
0x78e330 (`NrOfSquads`, `NrOfUnassigned`), the fill 0x791b20, 0x78f1b0(0).

`NrOfSquads` is `HUD_TEXT_MENU_SPAWN_SQUAD_NUMBER_OF_SQUADS` with `#ACTIVE#`
and `#TOTAL#`; `NrOfUnassigned` is `..._NUMBER_OF_UNASSIGNED` with
`#UNASSIGNED#` and `#TOTAL#`. Which counts fill the two `#TOTAL#`s
(`[0xa08f60]->vtbl[0x94]`) is not read: ours puts 9 and the team's players.

## The fill (0x791b20)

1. clear (0x7c89b0), +0x78 = 0;
2. +0x9e: the commander row 0x78e6a0, and while +0x9f the commander himself
   (0x791030 → 0x7902c0);
3. in no squad and not commander: the create row 0x78eca0;
4. squads 1..9 (0x791110): a squad with members gets its header 0x78fc30;
   while its bit is open, the leader (`[0x99ef84]->vtbl[0x3c]`) and then every
   member who is not a leader, each 0x7902c0;
5. +0xa1: the invite list (0x791740, 0x78e520, 0x791090, 0x790a90) — **not
   ported**.

### The rows (texture: column, texture, w×h, x, y, colour; text: column, text, x, y, colour, alignment)

Colours: 0x97de20 text (0.749, 0.749, 0.561), 0x97e2b0 frame (0.576, 0.573,
0.451), 0x97e2a0 kit icon (0.384, 0.361, 0.255), 0x97dd50 white, 0x97de30
button (1, 0.792, 0.043), 0x97de10 header background (0.486, 0.478, 0.388),
0x97de00 odd row (0.725, 0.710, 0.580, 1), 0xa1ceb8 picked = 0x97e0e0 (white,
init 0x865b70), 0xa1c758 own = 0xa1c9e8 (init 0x8650c0) = 0x97dd70 (0.447,
0.890, 0.345; init 0x8644d0). Button plates are 80×17 at y = +0xb0 − 1
(0x87fe78).

**Commander, 0x78e6a0.** Colour 0x97de20; our own when we command (0xa1c758);
picked (0xa1ceb8) when +0x90 is the commander. Label: APPLY
(`HUD_TEXT_MENU_SPAWN_SQUAD_APPLY`) when nobody commands, the arrow then the
empty icon; when someone else does, `HUD_TEXT_MENU_SPAWN_SQUAD_MUTINITY` if
0x597a40 allows (not read) else none; `HUD_TEXT_COMMANDER_RESIGN` for
ourselves. 0 iconframe 15×15 x 5; 1 kit_Commander; 2–4 empty; 5
`HUD_HUD_MENU_COMMANDER_HEADING` x −21; 6–8 empty; 9 plate and 10 label (when
there is one); 11 empty; 12 arrow (+0x14 open / +0x18 shut) 17×17; 13 the
commander's index; 14 `0`; background 0x97de10.

**Create, 0x78eca0.** 0 iconframe; 1 kit_Squad; 5 `HUD_SQUADTAB_SQUAD` x −21;
9 plate; 10 `HUD_SQUADTAB_CREATE` in 0x97de30, centred; 14 `3`; background
0x97de10. All in 0x97de20.

**Squad, 0x78fc30.** Colour: picked 0xa1ceb8 when +0x8c, own 0xa1c758 when our
squad, else 0x97de20. 0 iconframe x 5; 1 `%i` the squad x 5; 2–4 empty; 5
`§1%s§0` the name (through 0x431410 → 0x4311b0, not read) x −21; 6 `§1%i/%i§0`
members/6 right-aligned; 7 iconframe in the frame colour; 8 lockicon in
0x97de20 when locked (the squad's `vtbl[4]`), the frame colour otherwise; 9
plate; 10 JOIN (`HUD_TEXT_MENU_SPAWN_SQUAD_JOIN`), LEAVE on our own squad, in
0x97de30 — dimmed to the frame colour when we are in another squad or command;
11 empty; 12 arrow; 13 the squad; 14 `2`; background 0x97de10.

**Player, 0x7902c0.** Name colour: dead or `hud->vtbl[0x34c]` 0xa1cf78;
picked 0xa1ceb8; in our squad (or our own row while commanding) 0xa1cc98;
otherwise 0xa1c7d8 — the scoreboard's friendly entries. 0 iconframe (frame
colour) x 5; 1 `hud->vtbl[0x60](player, 1)` (0x74f480: the kit's
`VehicleHud +0x54 → vtbl[0x60] → +0x98`; 0x74f480's second argument 0 reads +0x94
instead, and it returns 0 when the player is the local one's own `vtbl[0xa0]`
object; which `vehicleHud.*` property sits at +0x94 / +0x98 is not established) in
0x97e2a0; 2 iconframe; 3 `hud->vtbl[0x64]` (0x74f320: icon_Death when dead) in
0x97e2a0; 4 `§1%s` the name x −4; 5–8 empty; 9 the leader's
`mini_Squad_%d_small.tga` 16×16 at x (216 − 133) − 18 (0x932078), own colour,
the empty icon for others; 10 empty; 11 iconframe; 12 the voice icon when the
player has voice (`vtbl[0x268]`: speaker in 0x97de40 yellow, white while
talking `vtbl[0x274]`, the muted icon when in +0x11c) — **ours stays empty**;
13 the player; 14 `4`. Background: our own row (0.996, 0.753, 0.082, 0.35,
0x7c4af0, its row into +0x7c), an odd row 0x97de00 (0x7c4ba0).

## The commands

Each `SquadMenu.*` command object's vtable differs from its neighbours only
in slot 23 (+0x5c), the handler; each handler checks its argument count,
clears +0xa0 of the command and calls the menu through `hud->vtbl[0x254]`.

| command | handler | method | |
|---|---|---|---|
| `singleClickNew` | 0x76d99e | 0x791ce0 | below |
| `doubleClickNew` | 0x76d927 | 0x83d270 | a bare `ret` |
| `doubleClickInvite` | 0x76d927 (same vtable 0x92dd08) | 0x83d270 | a bare `ret` |
| `createSquad` | 0x76da15 | 0x78f9b0 | below |
| `setSquadCreateSelect b` | 0x76daf8 | 0x78f5e0 | below |
| `squadCreateLockToggle` | 0x76db7f | 0x78f220 | +0xea = !+0xea |
| `setSquadNameSelect` | 0x76dbf6 | 0x78f190 | while +0xe8: 0x7b11e0 (focus the name box) |
| `setShowInviteList b` | 0x76dcd9 | 0x7918e0 | leaders only: on → fill the candidates (0x791690), HUD state 0x1a; off → state 0x11 |
| `applyInviteList` | 0x76dd60 | 0x791fd0 | invites (`[0x99ef84]` 0x2c) and kicks (0x34) from the two lists, then state 0x11 |
| `singleClickInvite` | 0x76de3e | 0x791f50 | column 1: toggle a candidate (0x791940, at most 6 − members) |

**`singleClickNew` (0x791ce0)** switches on the picked row's kind (column 14)
and the picked column (`ListBoxData +0x0c`):

* commander: columns 0–6 and 11 pick the commander (0x78f240); 10 applies
  (`hud->vtbl[0x2b0]`, 0x7b1c50, with no commander), mutinies (0x2b8, when
  0x597a40) or resigns (0x2b4); 12 opens or shuts the row (+0x9f);
* squad: 0–6 and 11 pick the squad (0x78f270); 8 our own squad's lock, as its
  leader (0x78f940 → `[0x99ef84]->vtbl[0x20](team, squad, !locked)`); 10 — in
  HUD state 0x1a as leader back to 0x11 — leaves our own squad (0x78f480:
  `[0x99ef84]` 0x34 then 0x2c) or joins another (0x78f2a0: in no squad and
  the squad has members, `[0x99ef84]->vtbl[0x30](team, squad, player)`; in
  another squad `HUD_HUD_SQUAD_MUST_LEAVE_TO_JOIN`); 12 opens or shuts it
  (0x790ef0 → 0x78fa60);
* create: 10 commits the name box (`hud->vtbl[0x28c]`, 0x7b15b0 →
  `vtbl[0x294]`, 0x7b15f0);
* player: any column picks the player (0x78f240).

**`setSquadCreateSelect` (0x78f5e0).** Off: closes the panel. On: refused with
`HUD_HUD_SQUAD_COMMANDER_MUST_RESIGN` for a commander,
`HUD_HUD_SQUAD_MUST_LEAVE_TO_CREATE` in a squad, `HUD_HUD_SQUAD_MAX_REACHED`
without a free squad (`hud->vtbl[0x2a4]`, 0x7b1750: the first squad 1..9 with
no members); otherwise opens it, fills the name box with 0x78f0e0's default
(`hud->vtbl[0x284]`), focuses it (0x268) and resets the lock (0x78f1b0(1)).

**`createSquad` (0x78f9b0).** Re-fills the name box when the panel was shut,
takes its text (`vtbl[0x288]`), commits it (`vtbl[0x28c]` → 0x7b15f0) and
shuts the panel. 0x7b15f0: not a commander; leaves our squad first (0x78f480);
the first free squad; an empty name becomes that squad's default (table
0xa212ac); `[0x99ef84]->vtbl[0x1c](team, squad, player id, name, false)`
(the pushes at 0x7b170f..0x7b172d). Slot 0x1c is
`ClientSquadManager::setSquadName(int, int, int, std::string const&, bool)`
(Linux 0x4be950): with the bool set it applies the name locally
(`SquadManager::setSquadName`, 0x4c4190); with it clear, as here, it sends
`ChangeSquadNameEvent(team, squad, player, name)` (0x4207b0) — **no lock**.
And nothing sends the panel's lock at all: +0xea is only reset (0x78f20c),
toggled (0x78f220), set in the ctor (0x791333) and registered as
`SquadCreateLockedShow` (0x78df6f) — the create panel's lock icon is display
only in 1.50. Ours sends `ChangeSquadNameEvent` 22 and no lock: the same.

**The default names (0x78f0e0)** come from 0xa1e380 (init 0x8673e0, 0x1c per
entry): Unassigned, Alpha, Bravo, Charlie, Delta, Echo, Foxtrot, **Gamma**,
Hotel, India, Juliet … Zulu. The free squad's own name, moved on while a squad
of the team already has it.

## The squad manager, `[0x99ef84]`

`ClientSquadManager` (names from the Linux server's vtable at the MSVC
offsets, `tools/linuxded/vtable.py dice::hfe::ClientSquadManager --msvc`):
0x1c `setSquadName(team, squad, player, name, local)`, 0x20
`setIsPrivate(team, squad, on, local)`, 0x28 `getSquadInfo(team, squad)` → its
`vtbl[0]` name, `vtbl[4]` locked, 0x2c `inviteToSquad(team, squad, player,
invite, local)`, 0x30 `addToSquad(team, squad, player)`, 0x34
`removeFromSquad(team, squad, player, kick)`, 0x3c `getSquadLeader`, 0x70
`getCommander(team)`. The player manager `[0xa08f60]` (`PlayerManager`): 0x20
`getPlayerFromId`, 0x30 the local player, 0x44 `getPlayersSortedByRank`, 0x4c
`getPlayersSortedByScore(team)`, 0x54 a squad's members (team, squad; size at
+8), 0x58 `getPlayersInSquadSortedByScore(team, squad)`, 0x5c
`getPlayersInSquadSortedByRank(team, squad)`, 0x9c `getNrOfPlayersInSquad`,
0xa0 `getInvitedPlayers(team, squad)`. A player (`dice::hfe::Player`): 0x20
`getName`, 0x50 `getId`, 0x68 `getIsAlive`, 0x108 `getSquadId`, 0x110
`getIsCommander`, 0x1f4 `getIsInvitedToSquad(squad)`.

## The list node's press (0x7c40a0)

Event 1 (press) or 0x15 (double press), inside the rows' band — right of
the left border, left of the scrollbar (+0x24 + +0xf8), below the top
border and above the bottom: the row is |(top + y) − mouse| ÷ (+0x1c spacing
+ +0x18 height), rounded down (0x402ec0), plus the first row shown (+0x08);
past the end it is −1. The column is the one before the first column (from
1) whose x lies past the cursor (the comparison adds `|rect.x|` to the
mouse: the sign convention is not established; ours is the cursor less the
node's left). Then `data->vtbl[0x74](column)`, `data->vtbl[0x80](row, 1)`.
The release (0x7c4530) runs command 1 (`setListNodeConCmd 1`, +0x48) when the
row is the one pressed; a double press runs 2 (+0x4c). Ours runs 1 on the
click (`hud::listAt`, `Manager::clickSpawn`); 2 is the empty 0x83d270.

## Measured

A scripted live run (clicks and console lines on a schedule) on the LAN server (FuShe Pass Co-op 16, the
server's bots), 21 Nov 2026 (`build/retail-captures/squads-20261121/run-8`):
the tab listed the commander row (APPLY), the create row and Alpha 4/6; the
arrow opened its four bots; JOIN sent join squad 1 and the server put us in
Alpha (5/6, LEAVE, the create row gone, our row highlighted). Stills
`members.bmp`, `opened.bmp`, `joined.bmp`. On the spawn screen every member
showed the death icon: whether the bots were alive then is not checked.

### Against Retail's own tab (22 Nov 2026)

Retail 1.50 on the same server (
`build/retail-captures/decal-ab-20261122/retail-squads-fushe-8/squad-tab.png`)
against ours at the same 800×600 (
`evidence/retail-captures/squads-20261122/run-18/members.bmp`), the left panel
(10 50 to 256 540) row by row. Fixed by it:

* **The squad's name is upper case.** 0x431410 copies the name through
  0x4311b0, the HUD's upper case (`hud::upperCase`): å ä ö ü é mapped to Å Ä Ö Ü
  É by hand, every other byte through `toupper`. Retail draws ALPHA; the row
  now matches it (mean difference 1.4 of 255 over the name).
* **COMMANDER is plain text.** 0x78e6a0 never touches the `§1%s§0` format
  (0x932030); we wrapped the heading in it and drew it in the thin font.
* **A button's third texture is the pressed one.** The button node's setter
  (0x7d0650, vtable 0x938c88 slot 0xec) keeps state 1 at rest (+0x60), 2 under
  the cursor (+0x64), 3 pressed (+0x68); the drawn texture is +0x6c, set by
  0x7d06c0 (cursor in: hover unless hover or pressed; out: rest), 0x7d0840
  (button down, event 1: pressed, the con command 0), 0x7d0910 (up on it:
  hover, the command 3), 0x7d09e0 (up elsewhere: rest); event 0x15 is the
  right button. Our parser let state 3 overwrite state 1, so CUSTOM SQUAD
  (`HudElementsSquadsNew.con:111`) was drawn pressed. The hover and pressed
  looks are still not drawn by our HUD.

What still differs, and why:

* the tab header: Retail's cursor stands on SQUAD in its still, not a defect;
* **text advances by a pixel every few glyphs less than Retail's**: COMMANDER's
  nine letters start at 35 41 47 54 61 67 73 79 84 in Retail, 35 40 46 53 59 65
  71 76 81 here; SQUAD's five end at 60 against 59. Our pen keeps fractional
  advances (`font/src/text.cpp`); how Retail's text renderer rounds is not read.
  It touches every HUD text, so it is font debt (docs/TODO.md), not this tab's;
* HUD text nodes (the SQUADS header, the CUSTOM SQUAD label) sit 1 px higher
  than Retail's (best match with ours moved 1 px down) — not read either.

The capture itself needed two fixes: the VM's pointer moves 1.2 pixels per
`mouse_event` unit (`--probe`: three −40,−40 moves took it 402,303 → 353,253 →
305,205), and a click right after a move can be a hover only, so the tab is
clicked again until the left panel changes.

### The name box (22 Nov 2026)

`CreateSquadNameBox` (`HudElementsSquadsNew.con:182`: 17 497 210 13,
`squadCreationFont_6.dif`, data and string 2, at most 10 characters, colour
0.82 0.82 0.61) is an edit node. What it shows is a slot of the HUD manager's
string table: `vtbl[0x284]` (0x7b13a0) writes the text into `[hud+0xdc]`
(`vtbl[0x54](index +0xf4, text)`) and sets the edit node's cursor
(`[hud+0xe0] → +0x14 → +0x20`) to its length; `vtbl[0x288]` (0x7b1410) reads it
back. `setSquadCreateSelect 1` fills it with 0x78f0e0's default, so Retail's
panel opens on "Bravo" with Alpha taken.

The edit node class: vtable 0x938dd8, its type check 0x7950d0 against 0x7d1000;
`setEditNodeMaxLength` is its `vtbl[0xd4]` (through 0x797280). Its draw is slot
0x68, 0x7aaef0, whole:

* the string from the scroll start (+0x1c), one glyph at a time from the box's
  left edge — no padding, no alignment;
* the font's scale `s` is the .dif's +0x28 (1 otherwise); each glyph's x is
  `floor(x·s + 0.5)/s` (fmod against 0.5, then floor), and y the same around
  the glyph's offset when `s ≠ 1`; the quad stands at that rounded pen with the
  glyph's width, and the pen then advances by the metric's three terms
  unrounded (renderer `vtbl[0x74]`; which term is the left bearing is not read);
* the first glyph whose x reaches the box's right edge ends the draw; a glyph
  of 0x0d ends it too;
* when +0x3d, an outline first: the glyph eight times in (0.15, 0.15, 0.15, 1)
  at ±1 px (0x7733d0, 0x82d730); then the glyph in the node's colour (+0x40);
* the cursor: at the cursor index (+0x20), while focused (+0x28) and the blink
  (+0x24) is at or past 0x93c850, a bar 2 wide and the font's line height
  (renderer `vtbl[0x7c]`).

Typing (22 Nov 2026): the box's button (`CreateSquadNameSelect`,
`HudElementsSquadsNew.con:177`) runs `setSquadNameSelect`, 0x78f190: only while
+0xe8, 0x7b11e0 asks HUD state 0xd, calls 0x7aadb0(1), `vtbl[0x278](1)` and
`vtbl[0x268](1)` (focus), sets +0x2fc and resets the keyboard buffer globals
0x9ffc90 / 0x9ffc94. Ours: the device's text field takes printable ASCII up
to the 10 characters, Backspace deletes, Return or Escape end the focus
(Retail's keys there are not read), CREATE or closing the panel drop it.
Live (a scripted run that typed "Raiders"): "Raiders" in
the box, the server's squad row RAIDERS.

Ours (`render.cpp`, `font::TextLayout::snapGlyphs` / `clipRight`) draws the
text, the rounding and the clip; the scroll, outline and cursor are not
ported, and the rounding is in screen pixels (the same at 800×600; other sizes
not measured). Against Retail (
`retail-squads-fushe-10/squad-create-1.png`; ours at 800x600,
`squads-20261122/run-21/create.bmp`): "Bravo" on the same
rows 497–505 and the same columns, mean difference 2.0 of 255 over the box.

The same quad-at-the-rounded-pen rule is a likely reading of the text-node
drift above, but text nodes draw through another function, not read.

## The invite list (read 22 Nov 2026)

`SquadInviteList` (`HudElementsSquadsNew.con:20`, list data 8, 258 91 210×445,
rows 19) under `InviteListShow`; MANAGE (`SquadManage`, `setShowInviteList 1`)
opens it for a leader, HIDE / CANCEL (`setShowInviteList 0`) and APPLY
(`applyInviteList`) close it.

Textures the ctor (0x791260) loads, by field: +0x14 `Respawn/button_upp.tga`,
+0x18 `button_down.tga`, +0x1c `GeneralIcons/empty.tga`, +0x20
`Scoreboard/Icons/checkbox_empty.tga`, +0x24 `checkbox_cross.tga`, +0x28
`checkbox_ask.tga`, +0x44 `Kits/Icons/kit_Commander.tga`, +0x48
`kit_Squad.tga`; then `button_longlight7.tga`, `iconframe.tga`,
`lockicon.tga`, `checkbox_voipMuteGrey.tga`, `icon_speaker.tga`,
`icon_speakerMute.tga` (fields not read).

The state, all `std::list<int>` of player indices:

| field | count | what |
| --- | --- | --- |
| +0xf8 | +0xfc | the rows shown (a player, or 0 for an empty slot) |
| +0x104 | +0x108 | players picked to invite |
| +0x110 | +0x114 | our members marked to kick |

* **0x791690** (on `setShowInviteList 1`): fills +0x104 from the squad manager
  `[0xa08f60]->vtbl[0xa0](team +0x98, squad +0x94)` — each entry's
  `vtbl[0x50]` (the player's index); what 0xa0 returns (pending invitations?)
  is not read.
* **0x791740** (the fill, step 5): +0xf8 cleared, then every player of the given
  list (`[0xa08f60]->vtbl[0x20](index)`) not marked to kick, then
  `6 − members − picked + kicked` empty slots (`[0xa08f60]->vtbl[0x9c](team,
  squad)` is the member count).
* **0x791090**: one row (0x790a90) per player that is null, or is not
  `vtbl[0x110]` (not established), not us (+0x80), and not in the squad given
  (when it is ≥ 0).
* **0x790a90**, the row: an empty slot is cells 0 and 1 empty, 2 `-1`, the odd
  background 0x97de00. A player: 0 `§1%s` his name (`vtbl[0x20]`) at x +0xe0,
  y +0xb4, alignment 0; 1 the checkbox 15×15 at +0xb0 in white (0x97dd50); 2
  his index `%i`; background 0x97de00. The checkbox: +0x1c (empty) for one
  outside our squad when the squad argument is < 0; else +0x24 (cross) when he
  is marked to kick and is ours; +0x28 (ask) when he is picked (or already
  invited, `vtbl[500](squad)`) and not marked; +0x20 (empty box) otherwise.
  The name's colour: outside our squad 0xa1c798 with alpha 0.5; else dead
  (`vtbl[0x68]` false, or `hud->vtbl[0x34c]`) 0xa1cf78, picked (+0x90)
  0xa1ceb8, ours 0xa1cc98, others 0xa1c7d8.
* **0x78e520**: a header row — two wide texts in 0x97de20 (alignment 0 and 2;
  the keys not read), `-1` in column 2, background 0x97de10.
* **`singleClickInvite` 0x791f50**: on column 1 (the checkbox), the row's index
  (column 2) through **0x791940**: a player outside our squad not yet invited
  is toggled in +0x104 (0x78fbc0 takes him out; adding is refused when
  `picked − kicked ≥ 6 − members`, and the HUD plays "warning"); one of ours (or
  already invited) is toggled in +0x110 (0x78fb50 takes him out).
* **`applyInviteList` 0x791fd0**: for each in +0x110: ours → kick,
  `[0x99ef84]->vtbl[0x34](team, squad, player, 1)`; not ours → withdraw,
  `vtbl[0x2c](team, squad, player, 0, 0)`. Then each in +0x104 not marked →
  invite, `vtbl[0x2c](team, squad, player, 1, 0)`. Then, when +0x9c, HUD state
  0x11 (`hud->vtbl[0x1dc](0x11, 1)`).

What fills 0x791740's and 0x791090's player lists, and the header's keys, are
in 0x791b20 step 5 and are not read yet.

## Not ported

The invite list and its commands (read above); the name box's cursor and
scroll; the create
panel's lock; the team half of `TeamTopLabelString`; the commander's mutiny
(apply and resign are ported); the voice icons; the two kit icons
(not established, as on the scoreboard); scrolling (+0x08).
