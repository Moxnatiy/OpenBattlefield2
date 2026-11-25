# The mod's and the levels' music

**Status:** the paths are read and a stream plays them (`engine::readModMusic`,
`engine::LevelEntry::music`, `app::MenuMusic`, `tests/test_menu_music.cpp`).
Nothing in the session starts it yet: **who plays which music, and when, is not
established** in the binary, and the wiring comes with the session's own work.
Source: `BF2.exe` 1.50, read by address with `objdump`; the game's data.

## Where the paths come from

Two descriptors name music, both with the same `<music>` tag (`BF2.exe` string
`"music"` at `0x8a1180`):

| descriptor | read at | kept at | bf2's value |
|---|---|---|---|
| the mod's `mod.desc` (`0x8a11a4`, appended to the mod's folder at `0x47ddab`) | `0x47df11` | +0x8c of the mod's description | `common/sound/menu/music/menu_music.ogg` |
| a level's `Levels/<name>/Info/<name>.desc` | `0x4802fe` | +0x70 of the level's description | `common/sound/menu/music/load_CH_music.ogg` (Dalian Plant, Daqing Oilfields, Dragon Valley, FuShe Pass, among others) |

The text between the tags has a space on each side in the game's files
(`<music> common/sound/menu/music/menu_music.ogg </music>`); the reader trims it.
The path is a path in the archives (`Common_client.zip`).

## What plays them

The original's music goes through its own sound engine (`MusicPlayer.cpp`,
the source path `BF2.exe` carries at `0x93c668`; the console has `loadMusic`,
`playMusic` and `stopMusic`, the first registered at `0x7dd5f2` with its name at
`0x93bd7c`). None of that is opened: not who
reads +0x8c or +0x70, not when the menu music starts or stops, not whether it
loops, not how the music volume (`AudioSettings.setMusicVolume`, our
`engine::Settings::audio.musicVolume`) applies.

Ours (`app::MenuMusic`) is a plain stream outside the game's sounds:

* an `.ogg` is decoded whole with stb_vorbis (v1.24, public domain or MIT,
  fetched at a pinned revision by `cmake/stb_vorbis.cmake`) and played through an
  SDL3 audio stream of its own; with `loop` the whole decoded file is queued
  again whenever the stream runs low;
* a RIFF `.wav` goes through SDL's loader and plays once;
* the bounds it decodes within (16 MB, 300 s, mono or stereo, 8–96 kHz) and the
  refill threshold are our own, **not measured** against the original.

## Measured

`tests/test_menu_music` with the game's folder as its argument: the mod's path
is read from `mod.desc` (10 archives mounted as the application mounts them),
the file exists in the archives, it decodes and plays on SDL's dummy driver;
Dalian Plant's `<music>` is read by the engine's level scan and plays the same
way. Heard on speakers: no — nothing in the application calls it yet.
