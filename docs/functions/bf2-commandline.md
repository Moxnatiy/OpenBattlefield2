# BF2.exe's command line

Taken from the binary itself, not from forums. `BF2.exe` has its own flag
table: a builder at `0x409cd0..0x40a660` fills it with
`(number, name, description)` triples, and parsing turns on
`jmp *0x40bbe8(,%eax,4)` at `0x40a697` — so **the flag's number is an index
into a jump table**. The upper bound is checked right there:
`cmp $0x49, %eax`.

## The table (number — name — the game's own description)

| # | flag | description |
|---|---|---|
| 0x00 | `dedicated` | Start in dedicated server mode |
| 0x01 | `multi` | Allow starting multiple BF2 instances |
| 0x02 | `joinServer` | Join a server by ip address or hostname |
| 0x03 | `hostServer` | *(no description — hidden)* |
| 0x04 | `playerName` | Set the player name |
| 0x05 | `password` | Set the server password when joining a server |
| 0x06 | `checkForAvailablePatch` | |
| 0x07 | `checkForPatch` | |
| 0x08 | `config` | Sets path to the ServerSettings.con file to use |
| 0x09 | `mapList` | Sets the path to the MapList.con file to use |
| 0x0a | `lowPriority` | Run the game with slightly lower priority |
| 0x0b | `loadLevel` | Set the level to load |
| 0x0d | `ai` | |
| 0x0e | `wx` | Position game window on the screen at certain x-position |
| 0x0f | `wy` | Position game window on the screen at certain y-position |
| 0x10 | `szx` | Set resolution witdth *(their typo)* |
| 0x11 | `szy` | Set resolution height |
| 0x14 | `fullscreen` | Start game in full screen mode |
| 0x15 | `noSound` | Start game without sound |
| 0x27 | `demo` | Sets the con-file with demo options |
| 0x2c | `maxPlayers` | Sets max players. |
| 0x2d | `gameMode` | Sets the game mode. |
| 0x2e | `modPath` | Set the mod path (default mods/bf2) |
| 0x39 | `help` | Displays this help |
| 0x3a | `?` | Same as +help |
| 0x3b | `ranked` | Allows gamespy snapshot sending |
| 0x3d | `playerPassword` | Set the player password |
| 0x41 | `playNow` | use playnow functionality |
| 0x42 | `port` | specifies the network port to be used |
| 0x43 | `pbPath` | Set the path to use for PunkBuster… |
| 0x46 | `restart` | Used when restarting executable. *(skips the intro movies)* |
| 0x47 | `rsconfig` | Sets path to the ReservedSlots.con file to use |
| 0x48 | `skipDXCheck` | Skips DirectX version check. Use with caution. |
| 0x49 | `dropDynamicSpawns` | Don't re-add dynamic spawn groups as round (re)starts. |

**There is no `menu` flag.** The name that circulates on forums is absent
from the table — the game simply does not know it.

## How flags become settings

The handlers are short and built the same way:

* `hostServer` (0x03, `0x40a7b8`) — takes no argument, only raises a local
  flag. **The default is already on** (`0x409cc8`), so it does not have to
  be passed;
* `loadLevel` (0x0b, `0x40a888`) — puts the level name into the frame's
  buffer;
* at the end of parsing (`0x40ba44`) the "host" flag decides where the
  name goes: host → `GSLoadLevel`, otherwise → `GSJoinAddress`.

## Why the game skips the menu

The check sits in one place, `0x401faa..0x401fcb`. The menu is **not**
shown if any one of these holds:

* `GSLoadLevel` is non-empty (put there by `+loadLevel`);
* `GSJoinAddress` is non-empty (`+joinServer`);
* `playNow` equals 1;
* `GSDedicated` is on.

In that case, instead of registering the menu it calls `0x404aa0(0, 1)`.

A working command line, verified on the live game (it opens
`Levels/Dalian_plant/{client,server}.zip`):

```
BF2.exe +fullscreen 0 +szx 1024 +szy 768 +restart 1
        +playerName defaultPlayer
        +loadLevel dalian_plant +gameMode gpm_cq +maxPlayers 16
```

## Internal switches (not in the help)

`GSDumpAllConFiles`, `GSCustomConFile`, `GSFileChangeMonitor`,
`GSDisableShaderCache`, `GSDebugGhostManager`, `GSDebugNetwork`,
`hack-ignore-asserts`, `swiffDebug`, `disable-swiff`, `keepAINav`,
`gameName`, `coll-load-debugmeshes`, `phy-convert-collision-meshes`. The
game writes the history of entered commands into
`Logs/BfCommandHistory.con` itself.

## The profile

The game takes the local profile from `Documents/Battlefield 2/Profiles`:
`Global.con` holds `GlobalSettings.setDefaultUser "0001"`, and the profile
itself lives in the `0001` directory. The login window seen when starting
from the menu is a login to an **online account**, not a choice of local
profile — with `+loadLevel` it never appears, because the menu is not
raised at all.
