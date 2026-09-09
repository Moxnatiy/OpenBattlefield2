# OpenBattlefield2

A clean-room reimplementation of DICE's **Refractor 2** engine — the engine
behind *Battlefield 2* (2005) — in C++20.

It is an engine, not a game: it ships no content and reads the files of
**your own** installed copy of Battlefield 2. Without that copy there is
nothing to run.

The goal is a **one-to-one port**, not a game "inspired by" the original.
Where the original's behaviour is unknown, it is recorded as debt in
`docs/` rather than filled in with a guess.

## Status

This is a research project in progress. What works today:

* **Main menu** — the game's own `mainMenu.swf`, played by
  [Ruffle](https://github.com/ruffle-rs/ruffle) with a bridge that answers
  the calls the movie makes into the engine: localisation, the player's
  profiles from `Documents/Battlefield 2`, the installed map list read
  from each level's `.desc`.
* **Singleplayer** — pick a map, start it, and the level loads with the
  spawn screen (kits, map, control points).
* **Levels** — terrain, water, static meshes, vegetation, collision.
* **HUD** — built from the game's own `.con` node tree, with the
  animation graph from `Menu/Ingame` (`MemeFile`).
* **Networking** — the handshake with an *original* dedicated server
  works; `openbf2 --connect <host>` joins and stays connected. Reading
  the world state back is partly done, see the debt list.

What is deliberately absent: no game content is redistributed, Bink video
is not decoded, and there is no anti-cheat.

## Requirements

* An installed **Battlefield 2 1.5** (any edition); the path is given with
  `--mod`, default `Game Files/mods/bf2`.
* macOS on arm64 is the main platform. The `windows-x64` preset is kept
  building but is not exercised in CI.
* CMake 3.24+, Ninja, a C++20 compiler, **SDL3**.
* Optional, for the Flash menu: **Rust** and a checkout of Ruffle in
  `reference/ruffle` (see `docs/research/11-ruffle-menu.md`). Without them
  the `obf2_flash` module is skipped and everything else still builds.

## Build

```bash
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
ctest --test-dir build/macos-arm64-debug --output-on-failure
```

## Run

```bash
# Menu (the game's own movie), then singleplayer from there
./build/macos-arm64-debug/src/app/openbf2

# Straight into a level
./build/macos-arm64-debug/src/app/openbf2 --level Dalian_plant

# A deterministic screenshot, no hands needed
./build/macos-arm64-debug/src/app/openbf2 --level Dalian_plant \
    --width 800 --height 600 --frames 4 --screenshot out.png

# Join an original dedicated server
./build/macos-arm64-debug/src/app/openbf2 --connect <host> --level dalian_plant
```

## Layout

| directory | what it is |
|---|---|
| `src/` | the engine, one module per subject (see the table below) |
| `tests/` | one test per parsed format; nothing is "understood" without one |
| `tools/` | scripts that pull facts out of the binary and the game data |
| `docs/` | what was reversed, and where it came from |
| `third_party/` | vendored single-file libraries: miniz, stb |

Engine modules: `core`, `vfs`, `con`, `texture`, `mesh`, `anim`, `font`,
`loc`, `game`, `level`, `gfx`, `hud`, `meme`, `net`, `server`, `engine`,
`flash`, `app`.

Two directories are **not** in git and must be provided locally:
`Game Files/` (your own installation) and `reference/` (third-party
sources used for comparison; their licences differ from ours).

Reversing the binary goes through Ghidra over MCP. That configuration is
nothing but absolute paths to one machine, so it is not in git either:
copy `.mcp.json.example` to `.mcp.json` and fill in your own.

## How the project works

Everything in `src/` is traceable to a source: an address in the original
binary, a line in the game's data, or a measured frame dump. A constant
without one of those is not written down — it is recorded as "not
measured" instead. `CLAUDE.md` states the rules the project holds itself
to, and `docs/` holds the reversing notes each of them refers to.

## Licence

MIT — see `LICENSE`. This covers **our** code only. Battlefield 2 and its
content belong to their respective owners; nothing from the game is
included here.
