# The chat and the kill feed

Status: `hud::Chat` (`chat.h`) and the list cells it needs (`list_data.h`) are
in; `hud::Manager::chatString` / `killedBy`, `app/kill_feed` and the session's
`ChatAssembler` hand-off named below come with the HUD wiring.

Sources: OpenBF2 (GPL-3.0, hints only) — `Menu/HudChat.cpp`,
`Menu/BF2HudManagerInit.cpp`, `Menu/BF2HudManagerGameLogicEvents.cpp`
(`BF2.exe` 0x753de0), `Game/Main/Game.cpp` (`outputChatString`),
`Game/Common/StringManager.cpp`, `Game/GameEvents/StringBlockEvent.cpp`,
`Game/GameEvents/KilledByEvent.cpp`; the game's `HudElementsChat.con`.

## The list

`ChatList` (`HudElementsChat.con`: `createListNode IngameChat ChatList 0 5 505
600 12 0`, `setListNodeData 3`, fonts `ingameChatFontOutline_8.dif` 0 and
`ingameChatFontLocalOutline_8.dif` 1, shown by `ChatListShow`, which
`HudInformationLayer`'s constructor sets true). List data 3 is
`hudChat->getData()` (0x7af170). `hud::Chat` (`obf2/hud/chat.h`) is `HudChat`:
nine kinds of line in bands of one list — capacities, spacings, durations and
colours from `BF2HudManagerInit`, the rows built by `rebuildRows` (39), a full
band dropping its oldest row, a timed-out band dropping one row a frame, text
wrapped on column 1 (16..500) at a space within 20 characters of the cut.

`BfListBoxData::moveRow` is `copyRow` then `clearCell` of the source: ours
copied only, and every timed-out band left its last line behind as a copy —
seen live as four identical kill lines.

## The kill feed

`KilledByEvent` (21): killer 8, victim 8, killer alive 1, the material 16, the
**weapon** template 32, the **vehicle** template 32, the last hit bone 8, the
last hit direction (compressed, 1/1000). The wire's order is the weapon first —
`KilledByData` keeps them at +0x0C and +0x08; a live kill from a T-90 came as
`Coaxial_mg_mec` (5294) then `rutnk_t90` (5035), pinned in
`tests/test_bf2_events.cpp`.

The line is case 3 of `handleGameLogicEvent`: `HUD_HUD_KILLEDBY_KILLS` /
`_KILLED` / `_TEAMKILLS` / `_MYSELF` with `#PLAYERNAME1#`, `#WEAPON#`,
`#PLAYERNAME2#`; the weapon's `weaponHud.hudName` when the vehicle is a soldier,
ladder or parachute, otherwise the vehicle's `vehicleHud.hudName` (so a tank
kill reads `[M1A2]`); our kill of an enemy and our death add notices
(`HUD_CHAT_YOUGOTAKILLSCORE`, `HUD_CHAT_YOUWEREKILLEDBY`). It goes to
`addTeamChat` → `addGameMessage`: kind 1's band, the colour by the killer's
side against ours (`defaultChatColors`: red the enemy's, blue ours). The
resolution is `app/kill_feed`, the line `hud::Manager::killedBy`.

Not ours: the information layer's death message, the victim's last hit bone and
direction (the ragdoll's push), a kill with no vehicle template (the original
asks the killer's current vehicle).

## The chat strings

`StringBlockEvent` (15): a "new" bit and the size (8), or a chunk (8-bit length,
at most 63 bytes kept). `StringManager` assembles the string; a 12-byte header
trails the text — channel (32), type (32), sender (8, -1 sent as 0), target (8),
flags (8). `Game::outputChatString` routes it: channels 12 and 13 always, the
others with a local player; type 0/1 → `addTeamChat` (kind 6's colour), 2 →
`addGlobalChat` → `addPlayerMessage` ("Name: text", with the `HUD_TEXT_CHAT_*`
and `HUD_CHAT_DEADPREFIX` keys the text carries), 3 → the squad radio line
"§0Name: §1text". `net::bf2::ChatAssembler` and `hud::Manager::chatString`.

Live: `game.sayAll` from rcon came as channel 12, type 1, and is drawn green
(kind 6) above the kill lines. Not ours yet: sending chat (the say boxes), the
ignore list, the commander's chat list (data 7), the radio (`RadioMessageEvent`
20, event 13), the history to the console.
