# Startup: from `BF2Engine::init` to the main menu

Every point below was checked in `BF2.exe` itself; B2BF/OpenBF2
(`Code/BF2/Game/Main/BF2Engine.cpp`, GPL-3.0, hints only — 00-prior-art.md) named
the addresses. The code is `src/engine/movie_queue.{h,cpp}` and
`src/engine/bink.{h,cpp}`.

## The intro movies

### Who queues them

`mods/bf2/Init.con`, whole:

```
bf2Engine.playMovie Movies\EA.bik 1
bf2Engine.playMovie Movies\dice.bik 1
bf2Engine.playMovie Movies\legal.bik 1
bf2Engine.playMovie Movies\intro.bik 0
bf2Engine.playMovie Movies\welcome.bik 0
```

`mods/xpack/Init.con` has its own list. The engine runs `Init.con` with full
access after `Settings/Sound.con` and `Menu/MenuSounds.con` (B2BF's
`BF2Engine::init`; the order **not checked** in the binary yet).

### `BF2Engine::playMovie` — 0x407a60

| address | what |
|---|---|
| 0x407a6a..0x407a9c | `settings->boolGet("skipMovies")` (vslot +0x50 of the settings at 0x987338); on → return |
| 0x407a9e | `0x440510()` (the dedicated test) → return |
| 0x407ac2 | the flag, the second argument (`[ebp+0x24]`, after the name passed by value) |
| 0x407ae8 | a node (name, flag) linked at the **end** of the list at engine+0xF8 (head pointer +0xFC, count +0x100) |

### `BF2Engine::checkMovie` — 0x407cb0

| address | what |
|---|---|
| 0x407ced | `skipMovies` on → return |
| 0x407cf5 | a movie playing (engine+0xF4 not null) → return |
| 0x407d09 | the list empty (+0x100 == 0) → return |
| 0x407d25..0x407d32 | the head's flag into engine+0x104 |
| 0x407d3e..0x407d6b | the head unlinked and freed, count less one — before the player exists |
| 0x407d86..0x407da2 | `classManager->createInstance("dice.hfe.geom.FSMoviePlayer")` into engine+0xF4 |
| after | `load(name)` (player +0x10); on failure the player is released and the next entry tried, until the list is empty (B2BF's reading; the loop's tail **not read** here) |

### The main loop's movie arm — 0x40ca80

| address | what |
|---|---|
| 0x40cd58..0x40cd63 | no movie playing → `checkMovie` — every frame |
| 0x40cd6e..0x40cd8a | menu active (+0x84), no menu logic yet (0x987358 == 0) and the first-frame flag 0x96e3a4 → the **menu is created first**: `GSPlayerName`/`GSPlayerPassword` quick login, the menu activated and drawn once, the flag cleared (0x40cecb). This branch comes before the movie arm, so the menu exists under the movies |
| 0x40cee9 | else, a movie playing: |
| 0x40cef6..0x40cf07 | the cursor hidden once (`ShowCursor(0)`, flag 0x96e3a3) |
| 0x40cf14 | engine+0xA4 = 1 — purpose not established |
| 0x40cf1d | player `drawFrame` (+0x18) |
| 0x40cf20..0x40cf44 | the keyboard device (input manager 0x9ffcb0, `getInputDevice(2)`), `update(-1.0)`, `update(0.1)` |
| 0x40cf49..0x40cf5b | `keyPressed(1)` (+0x34) **and** engine+0x104 != 1 → end the movie |
| 0x40cf65 | else player `finished()` (+0x1C) → end the movie |
| 0x40cf70..0x40cf8b | release the player, +0xF4 = 0, `checkMovie` — the next starts in the same frame |

Key 1 is `IDKey_Escape`: the name table maps it at 0x6b40a9..0x6b40be (`IDKey_1`
is 2, 0x6b40db). **So Escape is the only key that skips, and only the intro and
welcome; EA, dice and legal always play to their end.**

### `viewIntroMovie` gates nothing in the engine

`GeneralSettings::getViewIntroMovie` is 0x4b7280 (`mov al,[ecx+0x80]; ret`),
slot +0x78 of the vtable at 0x8a4a68 (the setter 0x4b7290 at +0x7C). No `call`
reaches 0x4b7280 directly, and no call through slot +0x78 follows a load of the
settings global 0x987390 within ten instructions. The limit of that search: a
pointer passed on, or a call from `SwiffPlayer.dll` (the menu's options screen,
which shows the box), would not be seen. What the menu's own byte code holds for
it in `mainMenu.swf` is a stand-in returning `true` (block 10, the test
harness's mock).

### `skipMovies`

Set by `+restart` (B2BF's argument table, case 70 — **not checked** here).
Nothing else in the client was seen setting it.

## The movies' lengths

We do not decode Bink; each movie is held for its own length, read from its
header (`engine::readBinkHeader`, the public MultimediaWiki layout):

| file | frames | rate | seconds | size |
|---|---|---|---|---|
| EA.bik | 96 | 2997/100 | 3.20 | 720 × 486 |
| Dice.bik | 187 | 30/1 | 6.23 | 640 × 480 |
| Legal.bik | 150 | 30/1 | 5.00 | 640 × 480 |
| Intro.bik | 4586 | 10000000/333333 | 152.87 | 640 × 480 |
| welcome.bik | 48 | 10000000/333333 | 1.60 | 640 × 480 |
| menu.bik | 3396 | 30/1 | 113.20 | 320 × 240 |
| menu_loggedin.bik | 4556 | 10000000/333333 | 151.87 | 320 × 240 |

`menu.bik` and `menu_loggedin.bik` are not queued by `Init.con`; who plays them
(the menu's background) is not established.

## What is left

* the order `Init.con` runs in against the other startup scripts, in the binary;
* `checkMovie`'s retry loop's tail and `+restart`, in the binary;
* engine+0xA4 set while a movie plays;
* how a movie is drawn (aspect, letterbox) — ours is a black screen;
* who plays `menu.bik` / `menu_loggedin.bik`.

## The menu's background

The menu movie asks for it itself, and it is a video:

1. `mainMenu.swf`, DoAction block 43, `bg.flush()`:
   `reg5 = Logic.getStorageString("BGmovie", "?menu.bik")`, then
   `background.loadImg(reg5)`; then a colour plate over it,
   `getStorageString("BGcolor", "0x464646")` through `Color.setRGB` and
   `getStorageString("BGalpha", "30")` into `_alpha`.
2. DoAction block 76 (the login path) writes `BGmovie` = `?menu.bik` and calls
   `_root.bg.flush()`. `menu_loggedin.bik` is the other value the movie holds
   (where it is written: **not read yet**).
3. `loadImg` (DoInitAction block 31) is the image container's method: unless the
   URL is the one already shown, `m_mcForImage.loadMovie(url)`, then
   `setImageSizes` and a fade (`m_fadeSpeed`).
4. In the original, `MovieClip.loadMovie` makes a bitmap through the render
   handler, which is `SwiffRender::loadTexture` (`ISwiffRender` +0x28) in
   `RendDX9.dll`. Its routing by the first character of the name:

| address | prefix | what |
|---|---|---|
| 0x101ba466 | `?` | a video: `<mod>/Movies/<name>` through the file manager (vslot +0x18, mode 2); failing that `mods/bf2/Movies/<name>` (0x101ba4fe); `dice.hfe.geom.Video` created (0x101ba58e) and opened (+0x18); width and height **0x100** (0x101ba5d7) whatever the movie's own size |

   The other prefixes (`@`, `$`, `#`, a server logo URL, and the fallback to
   `Menu/External/FlashMenu/`) are B2BF's reading of the same function — **not
   checked** here yet.
5. While drawing, a fill whose texture record holds a video advances it one
   frame (Bink's own clock) and binds its texture (B2BF's reading of the fill
   setup; **not checked**).

`images/background/background_*.png` are referenced by nothing — no `.swf`, no
binary. Ours shows `background_2.png` under the transparent stage as a stand-in.

## The order the startup scripts run in

Measured in `BF2.exe`; the code is `Engine::boot` (src/engine/src/engine.cpp).

| step | what | source |
|---|---|---|
| 1 | the archives: `ServerArchives.con`, `ClientArchives.con` | B2BF's `initEngine` (0x408ef0) — not re-read here |
| 2 | `Settings/Usersettings.con` through the settings manager (`SettingsManager.boolSet GSClPunkBuster 1`, `stringSet GSPlayerName "defaultPlayer"`, …) | B2BF's `initEngine` — **not checked** |
| 3 | the two default control maps | `BF2Engine::init` 0x40c5d5, 0x40c5f6 |
| 4 | `Logs/BfCommandHistory.con` (the console's history) | 0x40c60a |
| 5 | `Settings/Sound.con` | 0x40c6c2 |
| 6 | `Menu/MenuSounds.con` | 0x40c7fb |
| 7 | `Init.con` | 0x40c934 |

`Settings/VideoDefault.con`, `Settings/Video.con`, `Settings/GeneralOptions.con`,
`Settings/Controls.con` and `Settings/AliasedCommands.con` — which our boot used
to read — are named by **nothing**: no binary (ASCII or UTF-16), no script, no
movie. Video and general settings come from the profile (below). Ours still runs
`Settings/Controls.con` (default keys; the original builds its default control
maps in code) and `Settings/AliasedCommands.con` (the console aliases), marked as
unattested.

## The profile

`@HOME@/Profiles/` (`LocalProfileManager::getProfilesPath` 0x4a76a0). On this
machine `~/Documents/Battlefield 2/Profiles`, written by the original:
`Global.con` (`GlobalSettings.setDefaultUser "0001"`), `Default/` (Profile,
Audio, Video, ServerSettings) and `0001/` (Profile, General, ServerSettings,
Audio, Video, Haptic, Controls, mapList).

**At startup** `LocalProfileManager::init` (0x4a8980): unless a default profile
exists or can be made (+0x10, +0x14), fail; otherwise a profile named `Default`
(0x4a89b6), `loadCurrentProfile` (+0x3C), `applySettings(true)` (+0x58). When
`init` runs relative to the scripts above is **not established**.

**At login** (`Profile.loginOfflineAccount(n)` in the menu, `SwiffPlayer.dll`
0x1005e850) the named profile is loaded by the same `loadCurrentProfile`:

`loadCurrentProfile` 0x4ab1d0:

| address | both branches |
|---|---|
| 0x4ab23b | `<profiles>/` + the global settings' file (0x987380 +0x10 → `Global.con`, 0x8a34c4) |
| 0x4ab200 | the branch: is the profile named `Default`? |

| address | `Default` |
|---|---|
| 0x4ab25c, 0x4ab267 | audio (0x987384 +0x24) and video (0x987388 +0x20) settings to their defaults |
| 0x4ab285 | `Default/Profile.con`; failing it, "unable to open current profile" (0x8a3220) |
| 0x4ab30c, 0x4ab361 | `Default/` + audio's file, + video's file |

| address | a named profile |
|---|---|
| 0x4ab4ae | `<name>/Profile.con`; failing it, the same message |
| 0x4ab522 | `Default/ServerSettings.con` kept as the server settings' config file name (+0x4c) |
| 0x4ab587 | general settings (0x987390) → `General.con` |
| 0x4ab5dc | game server settings (0x987354) → `ServerSettings.con` |
| 0x4ab631 | audio (0x987384) → `Audio.con` |
| 0x4ab686 | video (0x987388) → `Video.con` |
| 0x4ab6db | haptic (0x987394) → `Haptic.con` |
| 0x4ab716..0x4ab749 | the input manager's control maps reset (0x9ffcb4 +0x34 twice, +0x14), control settings +0x28 |
| 0x4ab774 | control settings (0x98738c) → `Controls.con` |
| 0x4ab80b | `DemoBookmarks.con` |
| 0x4ab86d | the map list (0x4a84d0) |
| 0x4ab8a0 | the profile's name into the setting `GSPlayerName` |

The file names are each settings object's `getFileName` (+0x10); they match the
files the original wrote. Only `Global.con`'s string was read in the binary; the
others' (`General.con` …) are taken from those files and B2BF's reading.

Ours: `Engine::boot` → `loadDefaultProfile`; the menu's login reaches
`Engine::login` through the bridge's command queue (`login 0001`). Verified: the
menu's LOGIN page, the profile row, SELECT ACCOUNT → `profile: login 0001 —
loaded`.

Debt: the bridge (`tools/ruffle_bf2_bridge.rs`) still lists the profiles and
reads `Global.con` itself, apart from the engine — the same logic in two places.

## How a movie is drawn

`FSMoviePlayer::load` (`RendDX9.dll`, its shader `mods/xpack/Shaders/LoadingScreen`
0x1020d384 pushed at 0x100a293b) fills a four-vertex buffer, stride 24, at
0x100a2aa5..0x100a2af6: corners ±1 (0xbf800000 / 0x3f800000), colour
0xffffffff, UV 0/1 — one quad over the whole viewport with an identity
transform. **The movie is stretched to the window; no bars.** Which pass of the
`LoadingScreen` shader it takes and what that shader does to the colour: not read.

Ours: `obf2::video::Video` (src/video, FFmpeg's libavcodec linked dynamically —
LGPL) decodes the Bink picture; `MainMenu::frame` draws it as `#movie` over the
window while the engine is in `Intro`, and nothing else — the main loop's movie
arm is exclusive (0x40cee9), so the menu is neither stepped nor drawn under it.
Verified: a frame of EA.bik on screen at frame 120. The sound is not played.
