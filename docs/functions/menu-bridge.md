# The game's menu: what it talks to the engine with

Battlefield 2's menu — both the main one and the one that opens in combat —
is **Macromedia Flash**. The engine runs it with its own player, and the whole
"menu ↔ game" link goes through a small bridge of named objects.

These are the notes on that bridge: where it is, what it consists of and what
can be done with it. We do not reproduce Flash ourselves (see
docs/research/03-startup-and-menu.md) — but **the list of what the menu can
ask of the game is the menu's structure**, and it has to be known in full.

## Where things are

| what | where | does the game open it? |
|---|---|---|
| the menu's movies | `mods/bf2/Menu_client/External/FlashMenu/` | |
| the main menu (the same one in combat) | `mainMenu.swf`, 2 065 851 bytes | **yes, always** |
| end of round | `endOfRound.swf` | **yes, on level load** |
| loading | `loadGame.swf` | no (mentioned in `BF2.exe` but never opened) |
| the developers' service menu | `menu.swf`, 72 629 bytes | **no, never** |
| the "clickdummy" | `Clickdummy/main.swf` | **no, never** |
| the player and the bridge | `SwiffPlayer.dll` | yes |

The last column is not an assumption but the result of two runs of the
original under `WINEDEBUG=+file` (the menu and a level load): exactly
`mainMenu.swf` and `endOfRound.swf` appear in the log of file opens.
`menu.swf` and `Clickdummy/main.swf` are development leftovers, not present in
the game.

Both movies are **`FWS`, that is uncompressed**: they are read by
`tools/swf_read.py` without any unpacking. Both are Flash 7.

The path to the movies is baked into the player: the string
`Menu/External/FlashMenu/` at `SwiffPlayer.dll`, 0xd7bf8.

## Is the menu really Flash — verified on a live game

Statically it cannot be proven: `BF2.exe` has an **empty import table** (the
file is packed with SecuROM), so libraries are loaded on the fly and are not
visible in the header.

So it was verified by running it:

* `WINEDEBUG=+loaddll` — the game loads
  `Battlefield 2\SwiffPlayer.dll` (and `binkw32.dll` for the intros);
* `WINEDEBUG=+file` — the game opens `mainMenu.swf` (in the menu) and
  `endOfRound.swf` (on a level load).

`BF2.exe` itself also holds the strings `SwiffPlayer.dll`, `mainMenu.swf`,
`endOfRound.swf`, `loadGame.swf` and the path
`..\..\..\..\..\Menu\Extermal\FlashMenu\` — with the typo "Extermal", just as
in DICE's sources.

## The bridge does not live in `BF2.exe`

That is the main thing worth knowing in advance, because searching in the
wrong place is expensive. Not one of the names the menu calls
(`isGameRunning`, `disconnectGame`, `setPlayerName`, `getAutoReload`…) is in
`BF2.exe`. Nor are they in `CoreDLL.dll`, `RendDX9.dll`, `dice_py.dll`.

They are all in `SwiffPlayer.dll`. The path to the source sits there too:

```
C:\dice\Projects\BF2Branches\Patch_1_50\Code\BF2\External\gameswf\SwiffPlayer\SwiffPlayer.cpp
```

(`SwiffPlayer.dll`, 0xd7b98). So the player is DICE's superstructure over the
open `gameswf`, and not "loosely based" on it: the library holds gameswf's own
error strings verbatim. The analysis and the licence are in
docs/research/00-prior-art.md, in the section on gameswf. That matters:
**gameswf is in the public domain**, so nothing stops us from playing the
original movie with the same code.

## The bridge's objects

The menu sees seventeen named objects. Their names sit as one block in
`SwiffPlayer.dll` at 0xd7a74..0xd7b18, next to the words `bf2` and `dice` — so
the full name in ActionScript has the form `dice.bf2.<object>`:

| object | string address | what it is about |
|---|---|---|
| `ControlSettings` | 0xd7a74 | the control layout |
| `EndOfRound` | 0xd7a84 | the end-of-round screen |
| `Player` | 0xd7a90 | the player |
| `Mod` | 0xd7a98 | mods |
| `Cursor` | 0xd7a9c | the cursor |
| `Sound` | 0xd7aa4 | sound |
| `Client` | 0xd7aac | the client: the player's name, the command line |
| `Options` | 0xd7ab4 | settings |
| `Profile` | 0xd7abc | the profile and statistics |
| `Clans` | 0xd7ac4 | clans |
| `Multiplay` | 0xd7acc | network play, the server browser |
| `Singleplay` | 0xd7ad8 | single-player |
| `Render` | 0xd7ae4 | video |
| `Logic` | 0xd7aec | the game's state: is a battle on, disconnect, quit |
| `Locale` | 0xd7af4 | localisation |
| `MessageHandler` | 0xd7b00 | message and error windows |
| `General` | 0xd7b10 | everything else |

That these objects really are called by the menu is visible from the movie
itself: `tools/swf_read.py mainMenu.swf --actions --unique` gives 2717 lines,
and among them are `Client`, `Logic`, `Multiplay`, `Options`, `Profile` and
nearly one and a half thousand method names.

## What opens the menu in combat

The key is **Escape**, and that is visible in the game's data, not in the
binary:

```
Settings/Controls.con:188
  ControlMap.addKeyToTriggerMapping c_GIMenu   IDFKeyboard IDKey_Escape 10000 0
Settings/Controls.con:191
  ControlMap.addKeyToTriggerMapping c_GIEscape IDFKeyboard IDKey_Escape 10000 0
```

So **two** triggers hang on Escape. The name `c_GIMenu` sits in `BF2.exe` at
0x90e06c, `c_GIEscape` at 0x90e04c; 0x68f87b registers both. What each of them
actually does has not been worked out yet.

Which screen to show the engine tells the movie itself:
`Logic.setFrameLabel` moves it to the right frame, and
`Logic.setActivateReason` says why the menu was opened. The frame labels come
from the movie itself (`tools/swf_read.py mainMenu.swf --labels`), and among
them are `disconnect`, `settings`, `video`, `joinServer`, `createServer`,
`profile`, `demo`, `credits`, `awards`.

## The methods

The method names sit in `SwiffPlayer.dll`'s `.rdata` as one continuous block
0xd8000..0xdf400 — **1485 identifiers** (after that, from 0xe0794, come
ActionScript's own built-in classes: `MovieClip`, `Key`, `Math`, `XML`). They
are grouped by object: one object's methods run consecutively. For example, at
0xdd010 lies the tail of a group that ends at 0xdd0fc:

```
getDefaultLevel  eraseStorageString  setStorageString  getStorageString
getAbsoluteFileName  getTeamsConId  setFrameLabel  getFrameLabel
setActivateReason  getActivateReason  assert  active  isGameRunning
disconnectGame  quit
```

and at 0xdd15f the next one:

```
getPunkBusterMessage  enableMessageHandler  clear  removeLast
getLastCode  getLastType  getLastText  getNumMessages  raiseMessage
```

The groups are cut by `tools/swiff_bridge.py`: it looks for gaps between
neighbouring strings. Among them are things that are not methods but data —
0xd9acc..0xdab00, for instance, is **a list of countries** (`UA Ukraine`,
`CN China`…) for the server browser.

## How the bridge is built inside

Every bridge object is a class in `SwiffPlayer.dll`, and its constructor
registers the methods one by one. For `Logic` that is **0x1004def0** (the
image base is 0x10000000, so the file offset is 0x4def0). One entry looks like
this:

```
descriptor.type = 5            (a native function)
descriptor.impl = <address>
FUN_10007680(string, "quit")   (assemble the name)
FUN_1000bb50(this, string, descriptor)   (register)
```

`FUN_1000bb50` is "add a method". So to get any object's table it is enough to
find in Ghidra a reference to **any** of its names and look at the function it
sits in.

### `Logic` — all forty methods

Taken from 0x1004def0 in full, in registration order. The address is the
implementation in `SwiffPlayer.dll`.

| method | address | | method | address |
|---|---|---|---|---|
| `quit` | 0x1004ca70 | | `startCommandlineServer` | 0x1004c9a0 |
| `disconnectGame` | 0x1004cb10 | | `setDecimals` | 0x1004d640 |
| `isGameRunning` | 0x1004cc30 | | `getModVersion` | 0x1004d520 |
| `active` | 0x1004cca0 | | `beginWaitState` | 0x1004d030 |
| `assert` | 0x1004cb60 | | `endWaitState` | 0x1004c980 |
| `getActivateReason` | 0x1004ccf0 | | `getCommandLine` | 0x1004d4b0 |
| `setActivateReason` | 0x1004cb70 | | `setCommandLine` | 0x1004cfd0 |
| `getFrameLabel` | 0x1004cd70 | | `isCapsLockPressed` | 0x1004d450 |
| `setFrameLabel` | 0x1004cbd0 | | `restartExe` | 0x1004de30 |
| `getTeamsConId` | 0x1004da00 | | `isAbortLoadLevel` | 0x1004d3e0 |
| `getAbsoluteFileName` | 0x1004daa0 | | `setScrollSpeed` | 0x1004cf80 |
| `getStorageString` | 0x1004db60 | | `enableScroller` | 0x1004cf30 |
| `setStorageString` | 0x1004d0e0 | | `cacheCredits` | 0x1004c950 |
| `eraseStorageString` | 0x1004d170 | | `getRandomCreditsTexture` | 0x1004d350 |
| `getDefaultLevel` | 0x1004d5b0 | | `getNumRoundsLeft` | 0x1004d2e0 |
| `getIsLoading` | 0x1004d980 | | `isLoadingDemo` | 0x1004d270 |
| `setIsLoading` | 0x1004d090 | | `getAsync` | 0x1004d210 |
| `readyToPlay` | 0x1004de90 | | `setAsync` | 0x1004cef0 |
| `isFirstTimeLoading` | 0x1004d910 | | | |
| `getCommandlineServer` | 0x1004d810 | | | |
| `getCommandlineServerPassword` | 0x1004d890 | | | |
| `joinCommandlineServer` | 0x1004d740 | | | |

Forty names — exactly as many as in the `.rdata` group 0xdce54..0xdd0fc. So
the guess "adjacency in `.rdata` = one object" is **confirmed by the code**,
and the other sixteen objects can be taken the same way.

What of this the in-combat menu needs:

* `isGameRunning` — are we in a round; from this the menu shows "DISCONNECT"
  instead of "QUIT";
* `disconnectGame` — leave the game for the main menu;
* `quit` — leave the game entirely;
* `setFrameLabel` / `getFrameLabel` — which screen to go to;
* `setActivateReason` / `getActivateReason` — why the menu was opened;
* `beginWaitState` / `endWaitState` — "please wait".

## What the movie proves of this

Adjacency in `.rdata` is a hint, not a proof. An independent check comes from
`mainMenu.swf` itself: in ActionScript 2 a call `Logic.quit()` is "push the
object's name, push the method's name, call", so the pairs are visible simply
from the order. `tools/swf_read.py --calls` does that.

It agrees: what the movie calls on `Logic` is exactly the group at
`.rdata` 0xdce54..0xdd0fc; what it calls on `MessageHandler` is the group
0xdd178..0xdd1dc; on `Mod` — 0xdd250..0xdd2f8. Two independent sources give
the same answer.

**148 methods across nine objects, confirmed by the movie:**

**`Multiplay`** (45): `addMapToMapList`, `clearMapList`, `createServer`, `deleteDemo`, `downloadDemo`, `getBrowser`, `getConnectToIpHost`, `getConnectToIpPort`, `getDownloadingDate`, `getDownloadingProgress`, `getDownloadingUrl`, `getFilterGameMode`, `getGameModeFromId`, `getLeaderboardDescription`, `getLeaderboardName`, `getMapDescriptionFromId`, `getMapNameFromId`, `getMapPathFromName`, `getNumDemoBookmarks`, `getNumServers`, `getScrollFadeSpeed`, `getSelectedFilterGameMode`, `getSelectedFilterGameModeId`, `getSponsorLogoUrl`, `getTeamName`, `getToggleFilter`, `hasDemoDownloadFailed`, `isBrowsing`, `isDownloadingDemo`, `isGameSpyAvailable`, `playNow`, `removeMapfromMapList`, `setBrowser`, `setConnectToIpHost`, `setConnectToIpPort`, `setCurrentServerId`, `setFilter`, `setFilterGameMode`, `setHighestPing`, `setJoinPassword`, `setToggleFilter`, `sort`, `startBrowse`, `stopBrowser`, `updateFilter`

**`Profile`** (35): `createOfflineAccount`, `createOnlineAccount`, `deleteCurrentProfile`, `getActivePlayer`, `getAwardDescription`, `getAwardFirstDate`, `getAwardLatestDate`, `getAwardLevel`, `getAwardName`, `getCurrentRankId`, `getCurrentRankString`, `getEquipmentDescription`, `getEquipmentName`, `getLoginStatus`, `getNamePrefix`, `getNextRankString`, `getNumLocalProfiles`, `getNumOnlineProfiles`, `getNumRanks`, `getNumTimesLoggedIn`, `getRankProgress`, `getStatPlayerName`, `hasAward`, `hasThisAward`, `loginOfflineAccount`, `loginOnlineAccount`, `loginOnlineAccountWithEmail`, `loginOnlineAccountWithNick`, `logout`, `newGamespyProfile`, `searchPlayer`, `selectAward`, `selectLatestAward`, `setSelectedEquipment`, `setSelectedUnlockEquipment`

**`Logic`** (19): `cacheCredits`, `disconnectGame`, `enableScroller`, `getActivateReason`, `getCommandline`, `getDefaultLevel`, `getFrameLabel`, `getModVersion`, `getRandomCreditsTexture`, `getStorageString`, `getTeamsConId`, `isCapsLockPressed`, `isGameRunning`, `joinCommandlineServer`, `quit`, `setAsync`, `setFrameLabel`, `setStorageString`, `startCommandlineServer`

**`ControlSettings`** (12): `cancel`, `getInvertMouse`, `getMousePITCHFactor`, `getMouseSensitivity`, `getMouseSmoothing`, `getMouseYAWFactor`, `setActiveControlMap`, `setKeyboardSensitivity`, `setMousePITCHFactor`, `setMouseSensitivity`, `setMouseSmoothing`, `setMouseYAWFactor`

**`Sound`** (12): `playSound`, `voipGetBoost`, `voipGetEnable`, `voipGetRecieveVolume`, `voipGetSendingVoiceVolume`, `voipGetThresholdVolume`, `voipGetTransmitVolume`, `voipInitialize`, `voipIsBoostEnabled`, `voipSendingVoice`, `voipSetEnable`, `voipStopTest`

**`Mod`** (10): `activateMod`, `getCurrentModName`, `getModDesc`, `getModLogo`, `getModPath`, `getModPathShort`, `getModTitle`, `getModUrl`, `getModVersion`, `selectMod`

**`Locale`** (7): `addDelayedInstance`, `checkXMLStatus`, `getLanguage`, `initialize`, `loadString`, `setDefaultLang`, `setFlaName`

**`MessageHandler`** (6): `enableMessageHandler`, `getLastText`, `getLastType`, `getNumMessages`, `getPunkBusterMessage`, `raiseMessage`

**`General`** (2): `cacheExtendedServerInfo`, `setEquipmentIndex`

The other eight objects (`Client`, `Options`, `Render`, `Player`,
`Singleplay`, `Clans`, `Cursor`, `EndOfRound`) are called in the movie not
directly but through a variable, so this method does not see them. Their
methods are in `.rdata` (the largest group is 261 names at
0xdb1c0..0xdcd44, the settings), but **the binding to an object is not proven
yet**: the proof comes from the registration code in `SwiffPlayer.dll`, and
that is exactly why the library was brought into the Ghidra project.

## Lists: `General`, and why the movie has no "give me profile number N"

Something simple would not add up for a long time: the movie has
`Profile.getNumLocalProfiles()`, while a method "give me the name of profile
number N" exists **nowhere** — neither in `Profile` nor in the other sixteen
objects. Lists in the menu are filled differently.

The registration of the `General` object is `SwiffPlayer.dll`, 0x100316d0:

| method | address | what it does |
|---|---|---|
| `flushList` | 0x10019ba0 | drop the list's cache |
| `getListRevision` | 0x10019d80 | the list's revision number |
| `getListEntries` | 0x10022610 | **the list's rows** |
| `getHostVersion` | 0x10019f50 | the player's version, otherwise "Undefined Version" |
| `setFilter` | 0x10014b60 | |
| `setSortOrder` | 0x10014b70 | |
| `cacheExtendedServerInfo` | 0x100215e0 | |
| `setEquipmentIndex` | 0x10017dc0 | |

Lists are named by a string, and a component in the movie receives the list's
name as a parameter. The polling works like this: `getListRevision(<name>)`
says whether the list has changed, and only then does the movie take
`getListEntries(<name>)`.

**`getListRevision` knows only three names** — `serverList`, `serverListLAN`,
`favouriteServers` (0x10019d80). For all the others it returns **−1**. So the
rest of the lists are not versioned at all and are re-read every time; the
same holds in `flushList` (0x10019ba0): the three server lists return 1, the
rest −1.

The list names sit consecutively in `.rdata`, 0x100d9490..0x100d95b0:

```
teamSummary  ismysquad  commander  teamScore  tempList
ACCOUNT_online  ACCOUNT_offline  profileName  gamespy
GlobalSettings  localProfiles  url  logo  mods  equipments
kitid  kits  teams  buddyMessages  nick  profileSelection
profileSearch  buddyRequests  buddies  buddiesOnline  team2
```

### A row of the `localProfiles` list

The `getListEntries` branch at 0x10026baa. The profiles come from
`LocalProfileManager` (the string 0x100d7d54), and an object is assembled for
each:

| field | write address | from |
|---|---|---|
| `gamespy` | 0x10026cb0 | a comparison of the profile's field +0x54 against an empty string |
| `profileName` | 0x10026cf8 | the profile's field +0x1c |
| `password` | 0x10026d28 | the profile's field +0x8c |
| the account icon | 0x10026d6a / 0x10026d8e | `ACCOUNT_offline` or `ACCOUNT_online` — by the same comparison |

**Not established:** which way round that comparison reads (0x10026cd0, a call
through the import at 0x100d7168, whose name Ghidra does not show). So we do
not supply `gamespy` and the account icon yet — rule 6.

Meanwhile `profileName` and `password` are unambiguous, and it is those our
bridge puts into the row (`tools/ruffle_bf2_bridge.rs`).

## Single-player: how the menu launches a map

The "SINGLEPLAYER / INSTANT BATTLE" page is `DoAction` block 250 in
`mainMenu.swf`. The order is this:

1. on entry the page says `GeneralSettings.setUseBots(true)` and
   `setGamemode("gpm_cq")` -> `Multiplay.setFilterGameMode`;
2. the map list comes from `dice.General.getListEntries("installedMaps")`;
3. picking a row calls `selectMap(mapObject)`, which reads `id`, `path`,
   `mapSize`, loads the picture
   `$/Levels/<path>/Info/<mode>_<size>_menuMap.png` and rewrites the game's
   map list: `Multiplay.clearMapList()`, `addMapToMapList(path, "", size)`;
4. the captions on the right come from `Multiplay.getMapNameFromId(path)`,
   `getMapDescriptionFromId(path)`, `getGameModeFromId(mode)`;
5. "START SINGLEPLAYER" runs `setSingleplayerSettings()` and
   `Multiplay.createServer()`.

`setSingleplayerSettings` (block 40, 0x1cf9) is the full list of what the menu
tells the engine before a game:

```
Multiplay.setFilterGameMode("sp1")
GeneralSettings.setUseBots(true)      GeneralSettings.setMaxBots(16)
GeneralSettings.setMaxBotsIncludeHumans(true)
ServerSettings.setMaxPlayers(16)      ServerSettings.setFriendlyFire(true)
Sound.voipSetEnable(false)            …
```

### Where the map list's rows come from

Not from `Profile` and not from `Multiplay` — a method "give me map number N"
exists nowhere. The list is assembled by
`General.getListEntries("installedMaps")` (the branch at 0x1002b01d), walking
the levels, their modes and sizes. The data is in the level itself,
`Levels/<directory>/Info/<directory>.desc`:

```xml
<map gsid="101">
  <name> Dalian Plant </name>
  <briefing locid="LOADINGSCREEN_MAPDESCRIPTION_dalianplant">…</briefing>
  <modes>
    <mode type="gpm_cq">
      <maptype ai="1" players="16" type="doubleassault" locid="…">…</maptype>
```

`gsid` is the row's `id`, the directory's name is `path` (the game writes it
in lower case: `maplist.append "dalian_plant" "gpm_cq" 16`), and `players` is
`mapSize`.

**Bots decide what is shown.** At 0x1002b280 an entry gets into the list only
when either `GameServerSettings` says "no bots" (vtable +0xa8) or the entry
has its own flag set (+0x58). In the data that is `ai="1"`. That is why the
multiplayer page first does `setUseBots(false)` and sees every size, while the
single-player page does `setUseBots(true)` and sees one row per map.

## Instruments

| command | what it gives |
|---|---|
| `tools/swf_read.py <swf>` | the header and the list of labels |
| `tools/swf_disasm.py <swf> --grep <text>` | the byte code itself: branches, function bounds |
| `tools/swf_read.py <swf> --labels` | the frame labels — these are the menu's screens |
| `tools/swf_read.py <swf> --text` | text fields and their variables |
| `tools/swf_read.py <swf> --actions` | every ActionScript string |
| `tools/swf_read.py <swf> --calls Logic` | what the movie calls on that object |
| `tools/swiff_bridge.py <dll> --objects` | the bridge's seventeen objects |
| `tools/swiff_bridge.py <dll>` | the groups of names in `.rdata` |

## The measure

These notes count as done when:

1. each of the seventeen objects has the **full** list of its methods together
   with the address of its registration table (rule 3);
2. for those the in-combat menu needs (`Logic.isGameRunning`,
   `Logic.disconnectGame`, `Logic.quit`, `MessageHandler.*`) it is said what
   they do in the game;
3. our client answers the same calls — that is, the menu can be opened in
   combat and used to leave the game.
