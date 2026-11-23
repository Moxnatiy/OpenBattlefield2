# In-game help messages

Status: `hud::HelpBox` and `hud::HelpScheduler` (`help_messages.h`) are in;
`hud::Manager::showHelp` / `tickHelpMessages`, the conditions in `main.cpp` and
the voice-over stream named below come with the HUD wiring.

The box at the top of the screen ("Press the 'E' Key to Exit the Vehicle",
`build/retail-captures/vehicle-seats-20261122/retail-m1a2-9/entered.png`).
Ported for message 0x44 (exit vehicle), with Retail's scheduler and voice-over: `src/hud/src/help_messages.cpp`,
`hud::Manager::showHelp`; on screen in
`vehicle-seats-20261123/mac-m1a2-2/help.png` (Retail above, ours below).

## What draws it

The HUD layer variables `IngameHelpShow` (bool, +0x1c4) and
`IngameHelpString` (wide string, +0x1c8), docs/functions/hud-variables.md.
Nodes: `HUD/HudSetup/HudElementsHelp.con` (Menu_client.zip): picture
`IngameHelpBg` (260 23 335 87, `helptextBg.tga`, fade in 1 s, out 0.5 s) and
text `IngameHelp` (264 38 315 74, `helpMessageFontLocalBold_8.dif` align 2,
colour 0.686 0.686 0.588, fade out 0.5 s).

## The help class (`BF2.exe` 0x5ed000..0x5f1600)

Constructor part 0x5ed220 (one caller, 0x5056f0). The class hangs off the
Player at +0x2a4, made at 0x505d29 (0xd88 bytes) unless 0x9a4381 is set
(0x440510; purpose not established), and 0x5edb80 runs right after it
(0x505d4d).

| offset | size | what |
|---|---|---|
| +0x000 | 0x52 bytes | per message: played; skipped for good by the scheduler (0x5f153e). Set by 0x5edb20 (0x5edb44) when a message shows, and by 0x5edb80 (0x5edbf5) at start; read 0x5ed140 |
| +0x052 | 0x52 bytes | per message: suppressed (scheduler skips, 0x5f1537). 0x5ed120 writes it; Player::disableVOHelpMessage (0x4f9f20, writes 0) and enableVOHelpMessage (0x4f9f40, writes 1) are its callers — names from the Linux server, the byte's sense from those writes only; callers of those not traced |
| +0x0a4 | 0x52 ints | per message: at start a message is also marked played when this is not above a value from 0x987364 vtable +0xb4 (0x5edbc0..0x5edbe6); that value is not identified |
| +0x1ec | byte | a message showed recently (set 0x5f1572; cleared 0x5f1513 once +0x1f4 passes +0x1f0) |
| +0x1f0 | float | 10.0 (0x5ed230): the least gap between two messages |
| +0x1f4 | float | time since the last message (0x5f14da adds dt; zeroed 0x5f151a, 0x5f1579) |
| +0x1f8 | float | random(10, 40) (0x440030 with 0x41200000, 0x42200000 at 0x5f1504) after each gap; loses dt each frame; no reader found in the scheduler |
| +0x1fc | byte | enabled: the scheduler returns at once while 0 (0x5f1466). 0 at start (0x5ed244); set by 0x5ed0e0, called only from Player::enableVOHelp (0x4f9f00, Player vtable 0x8ac4d8 +0x268). Who calls that is not traced; the game-wide switch is `gameLogic.enableVoHelp 1` (`mods/bf2/GameLogicInit.con` line 9; command 0x438303 → GameLogic vtable +0x274) |
| +0x1fd | byte | the controlled object is a soldier: set every scheduler frame from player vtable +0x44 → +0x14 vtable +0x10 == `[0x91c240]` (0x9493, `CID_SoldierTemplate`) (0x5f14a4..0x5f14c8). Not a help-off option |
| +0x200 | 0x52 * 0x1c | the message strings (localisation keys), slot = +0x200 + id * 0x1c |
| +0xaf8 | 0x52 * 8 | per message {interval, remaining}, all 1/30 (0x3d088889) at start; 0x5ed150 is the repeat timer (remaining <= 0: reload, true; else subtract dt) |

A `switch` over the message id (0x5ed220, 73 keys) fills the strings and some
intervals. Examples:

| id | key | slot | other |
|---|---|---|---|
| 0x3f | `HUD_HELP_GAMEPLAY_climbLadder` | +0x8e4 | |
| 0x40 | `HUD_HELP_PLAYER_CONTROLS_radioMessages` | +0x900 | `+0x1a4 = 0x1c20` |
| 0x41 | `HUD_HELP_MENU_COMMOROSE_slCommoRoseFirstUse` | +0x91c | byte +0x93 = 1 |
| 0x42 | (no key) | | `+0x1ac = 0x1c20`, byte +0x94 = 1 |
| 0x43 | `HUD_HELP_VEHICLE_GENERAL_CONTROLS_enterVehicle` | +0x954 | |
| 0x44 | `HUD_HELP_VEHICLE_CONTROLS_exitVehicle` | +0x970 | |
| 0x45 | "" | +0x98c | `+0xd20 = 1.0f` |

The whole table is not written down yet
(`ghidra_projects/rea/dossiers/BF2_5ed7df.json`, `tools/rea_dossier.py --code`).

## Raising a message: 0x5ede00

`(id, ...)`: needs the local player (0xa08f60 vtable +0x30); ids above 0x51
return false. Jump table at 0x5f1310, one case per id. Each case checks its
condition, plays the voice-over (0x5ed1e0 with a sound name), then calls the
HUD manager (0xa10890): +0x38 localise the key (0x74ef30), +0x58 expand the
markers (0x752490), +0x80 showHelp (0x74f7f0) with a time.

| id | case | condition | voice-over | time |
|---|---|---|---|---|
| 0x43 enter | 0x5efc64 | +0x1fd (on foot); the controlled object (player +0x44) answers interface `[0x91c410]` (0xc4c5, a player control object); 0x5de7e0 (an object search, radius 3.0 = 0x40400000 through the object's vtable +0x68, with player vtable +0xe4(player, 1, 1, 1)) finds something; the controlled object's template class is not `[0x91c38c]` (0x9c40, `CID_LadderTemplate`); its +0x54 VehicleHud, if any, has byte +0x96 set | `enter_Vehicle_help` (0x8efd84) | 9.0 s |
| 0x44 exit | 0x5efd74 | the controlled object (player +0x44) is not the soldier (+0xa0) and player vtable +0x68 is true; its template class is not `[0x91c3a4]` (0x9c46, `CID_LadderContainerTemplate`); the same +0x54 check | `exit_Vehicle_help` (0x8efd70) | 9.0 s |

The class ids are the Linux server's `dice::hfe::world::CID_*` constants
(`tools/elf_symbol.py --binary "Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2"`).
Byte +0x96 of the VehicleHud's +0x64 is taken to be
`vehicleHud.standardHelpEnabled` (Linux `VehicleHudCompTemplate::
setStandardHelpEnabled`; console command 0x5856c0 → active VehicleHud
template vtable +0x7c, getter +0x80): the only writers in
`Objects_server.zip` set it 0 on the stationary weapons (`us_bipod`,
`ats_tow`, `usaas_stinger`, …) and `MultiPlayerOverheadCamera`. The +0x7c
body that stores it was not found by a scan, so the offset is not tied.

### Who raises them: the scheduler 0x5f1460

One caller, 0x50537b: the Player's update, for the local player only, when
the player is not dead (vtable +0x58) and +0x2a4 exists. `(dt)`:

1. +0x1fc clear: return. `DAT_0099ef80` (GameLogic) present and its vtable
   +0x158 not 1: return.
2. +0x1fd = controlled object is a soldier (table above).
3. +0x1f4 += dt, +0x1f8 −= dt. If +0x1ec: while +0x1f4 <= 10.0 return;
   else clear +0x1ec, zero +0x1f4, +0x1f8 = random(10, 40).
4. For id 0 .. 0x51 in order: skip if +0x52[id] or +0x0[id]; skip unless the
   repeat timer 0x5ed150(id, dt) fires (every 1/30 s); call 0x5ede00(id, dt).
   The first true ends the walk: +0x1ec = 1, +0x1f4 = +0x1f8 = 0, then
   0x5edb20(id, 1).

0x5edb20(id, save): when the slot's key is not empty, +0x0[id] = 1; with
`save`, 0x5ed0f0 hands the key to GeneralSettings (`[0x987390]`) vtable
+0x114 — the same call `GeneralSettings.setPlayedVOHelp "<key>"` makes
(0x414d6d) — unless 0x9a4381. 0x5edb80 at start sets +0x0[id] for every
key GeneralSettings +0x118 knows. So **each message shows once per
profile**, and no two within 10 s. Retail showed 0x44 on entering the M1A2
because that profile had not played it.

Ported: `hud::HelpScheduler` (help_messages.h); the profile's
`GeneralSettings.setPlayedVOHelp` lines are read (`GeneralSettings::playedVoHelp`,
engine/settings.h). Not wired yet: the manager's per-frame tick that hands the
played keys to the scheduler at start (0x5edb80) and the conditions it is ticked
with, so no help message shows. Debt:

* writing the played keys back to the profile (we keep them for the run);
* message 0x43: 0x5de7e0 (what it searches for, its +0x60/+0x50 filters,
  the 0x41700000 = 15.0 it stores) is not taken apart, so ours never raises
  it;
* player vtable +0x68 in case 0x44 is not identified (a spawned player
  stands in);
* +0x0a4 and the 0x987364 stat, +0x52 callers, the caller of
  Player::enableVOHelp (ours is on, as the data's `gameLogic.enableVoHelp 1`);
* the other 71 messages.

### Voice-over: 0x5ed1e0

With a local player, `GameLogic` (`DAT_0099ef80`) 0x4bdbb0 gives its
VoiceMessages, and 0x4f8110 (`playHelpMessage`) plays the name: 0x431450
makes a lower-case copy (the data writes `exit_vehicle_help`), it is looked
up in the map at +0x84 (`gamelogic.messages.addHelpMessage <name> <file>`,
`Common_server.zip` Sound/VoiceMessages_Help.con, run from VoiceMessages.con);
when a volume (0xa23dc0 vtable +0xb0, not identified; ours takes
`AudioSettings.setHelpVoiceVolume`) is above 0.05 (0x8abfec), the help
stream (+0x20ec) is stopped (+0x30) and the sample started (+0x20). The
folder is `common/sound/<GSLanguage>/` (0x4edb6c..0x4edc6c: the setting
`GSLanguage`, default `English`, which also stands in when
`<lang>/Help_VO/HUD_HELP_BATTLERECORDER_controlsQ.ogg` is missing). Ported:
`RemoteWorld::helpVoicePath` and a `helpVoice` stream in `main.cpp`.

`GSLanguage` comes from initLocalization (0x4086e0): 0x450fe0 reads the value
`Language` of the install key `SOFTWARE\Electronic Arts\EA Games\Battlefield 2`
(0x40874b), `English` when absent (0x408714), and settings vtable +0x44 stores
it (0x40878c). The same name picks `localization/<language>/` (0x40879f), so
it changes the help text as well as the voice. Ported:
`app::installLanguage` (the registry on Windows; `--language <name>`
everywhere), `Engine::language`, `Engine::loadLexicon`. Every one of the 12
localisation folders has the help keys; Help_VO exists for English, French
and German only (`Common_client.zip`), so the other nine speak English, as
Retail's probe does. The HUD fonts come from `Fonts\<GSLanguage>\`
(0x7aec8e..0x7aed48); what Retail does when that language lacks a font
(0x7aedda) is not read. The Flash menu's `Locale.getLanguage`
(SwiffPlayer.dll 0x1004c720) answers the same language.
`helpHud.helpSoundKey` and the other
`helpHud.*` keys belong to the HelpHud component and are not this path.

## Markers: 0x752490

`#` opens a marker; `P` takes the action from `defaultPlayerInputControlMap`,
`G` from `defaultGameControlMap`; the action is `c_` + the marker
(`#PIUse#` -> `c_PIUse`). The key name shown is the localisation entry
`CONTROLLER_<key>` (`CONTROLLER_IDKey_E` = `E`, English.utxt); where the
function reads it is not traced.

## Show: HudManager::showHelp, 0x74f7f0 (vtable 0x926130 +0x80)

`(wide text, float seconds)`. Text empty, seconds <= 0.0 (0x93c850), or
0x99e348 vtable +0xdc true: the box is hidden. Otherwise `IngameHelpString`
= text, `IngameHelpShow` = 1, countdown +0x22c = seconds. +0x84 (0x74f7b0)
is the narrow-string form.

## Countdown: 0x755fd0

Per frame (`param_1` the frame time):

* returns at once while `DAT_00987358` is set;
* while `IngameHelpShow` is set, +0x22c loses the frame time; below zero the
  box is hidden (layer vtable +0xc);
* with a controlled object that is not the soldier (vtable +0x68 false), a
  second timer at +0x2a8 counts down while byte +0x228 is set and is reset
  from +0x2ac; on expiry +0x1dc (set HUD state, 0x74fd50) and +0x1bc
  (0x74f180) run and `DAT_0099ef80` vtable +0x74 is told the object;
* `DAT_00978fc4` is a three-state flag set from byte +0x15c.

## Text highlight: 0x7c2600

`§C`/`§c` toggles the highlight and takes six characters (0x7c27bc); the four
digits become floats the glyph draw does not use. Highlighted glyphs draw in
(0x3f53d3d4, 0x3f51d1d2, 0x3f33b3b4, 1.0) = (0.827, 0.820, 0.702, 1)
(0x7c2a49..0x7c2a6b). Ported in `src/hud/src/render.cpp`.

**Width (debt).** At 800x600 the line "Press the 'E' Key to Exit the
Vehicle" inks 265..469 on Retail (204 px,
`vehicle-seats-20261122/retail-m1a2-9/entered.png`) and 265..464 on ours
(199 px, `vehicle-seats-20261123/mac-m1a2-2/entered.bmp`). From the font
(`Fonts_client.zip` English/helpMessageFontLocalBold_8.dif: point size 16,
glyph advances and kerning) the plain sum is 200.55 px; rounding each
advance gives 209.4 and rounding up 215.4, so neither is Retail's rule.
Retail's glyph lefts run about 1.7 % wider from the second glyph on (`r` at
+8, `e` +13, `s` +20 against 7.2, 12.4, 18.5). The cause lies in the edit
node draw 0x7aaef0 (its scale from the font +0x28, 0x7aafab, divided at
0x7aafbc), not taken apart. No spacing is changed.
