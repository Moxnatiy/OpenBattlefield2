# Prior art — what the community has already done (as of 2026-08)

## Engine reimplementations

| Project | What it is | Status | Licence |
|---|---|---|---|
| [Project Dalian](https://github.com/chronic8000/ProjectDalian) | A clean-room C++20 recreation of Refractor 2 that reads assets from your own BF2 installation | v0.5.19-alpha; phases 0-7 done: PBR renderer, skeletal animation, ballistics, flight model, basic multiplayer | MIT |
| [BattlefieldRespawn](https://github.com/rigred/BattlefieldRespawn) | A data-compatible recreation of Refractor/Refractor 2 (compatible with Project Reality) | early | — |
| [breadflowerdos](https://github.com/kiwidoggie/breadflowerdos) | A "decompilation" of BF2/2142, a modern C++ recreation | early | — |

**Conclusion:** Project Dalian has already verified byte for byte:
`.staticmesh`, `.bundledmesh`, `.skinnedmesh`, `.ske`/`.baf` (skeletons +
animation), collision meshes, DDS, zip archives, the `.con`/`.tweak`
interpreter. MIT, so it can legally be reused and compared against. **We
do not re-reverse those formats from scratch** — we take their
specifications as a starting point and spend reverse engineering on what
they do not have (gameplay logic, netcode, AI/bots, scripting).

## Network / services

- [Refractor-2-BitStream-Emulator](https://github.com/matthias-hoste/Refractor-2-BitStream-Emulator) — emulation of R2's network traffic, a base for BF2.
- [Refractor-2-game-engine-extension](https://github.com/BattlefieldRedux/Refractor-2-game-engine-extension) — a C++ DLL injected through an IAT patch in BF2.exe. Useful for run-time instrumentation (hook + log calls) — a faster way to understand the logic than static decompilation.
- BF2Hub / OpenSpy — replacements for the GameSpy master server.

## Formats / tools

- [BfMeshView](http://www.bytehazard.com/bfstuff/bfmeshview/) — an open viewer/editor for BF2 meshes, with the fullest description of the mesh structures.
- [Classic Battlefield Modding Wiki](https://classic-battlefield-modding.fandom.com/) — the main community reference for `.con`, textures and modding.
- BF2 embeds Python 2.3 — a large part of the server-side gameplay logic sits in `python/` as open `.py` files (no reversing needed at all).
- **BF2 ships its own shaders as source** — `mods/bf2/Shaders_client.zip`, 101 `.fx` files of HLSL text, not compiled blobs. Fog, the material passes, roads, terrain and the sky are all written out there, so none of it has to be reversed: [../formats/shaders.md](../formats/shaders.md).

## The menu's Flash player is **gameswf**, and it is public domain

A find that changed the plan: BF2's menu is not driven by a player DICE
wrote but by Thatcher Ulrich's **gameswf**. The proof is not a
resemblance but the library's own strings, verbatim, inside
`SwiffPlayer.dll`:

```
gameswf::movie_def_impl::read() -- file does not start with a SWF header!
gameswf::notify_key_event(): no Key built-in
error: no file opener function; can't create movie.
    See gameswf::register_file_opener_callback
gameswf::fontlib::save_cached_font_data(): problem writing to output stream!
```

Three of the four are found verbatim in the current gameswf tree
(`gameswf_movie_def.cpp`, `gameswf_player.cpp`, `gameswf_test_ogl.cpp`).
The fourth is not: `save_cached_font_data` was removed from today's
`fontlib`, so DICE branched off an **older** version. A path in the DLL
confirms it: `…\Code\BF2\External\gameswf\SwiffPlayer\SwiffPlayer.cpp` —
gameswf sat there as an external dependency and `SwiffPlayer` was a thin
wrapper over it.

| what | value |
|---|---|
| licence | **public domain** — "This source code has been donated to the Public Domain. Do whatever you want with it" (`gameswf.h`) |
| language | C++ |
| size | 167 files, 43 340 lines (a mirror of trunk r1714) |
| mirror | [prepare/gameswf](https://github.com/prepare/gameswf) — archived, `svn checkout … tu-testbed/trunk` |
| also used by | Gnash, Oddworld: Stranger's Wrath, Gameloft games |

This is **the one piece of third-party work in `reference/` whose licence
lets us take code directly**: everything else there is either unlicensed
or GPL. Fetched into `reference/gameswf` (not in git, like the rest).

The second option is [Ruffle](https://github.com/ruffle-rs/ruffle),
MIT/Apache-2.0, a living project, ~99 % of the ActionScript 2 language and
~82 % of its library. It is Rust and a full Flash Player emulator rather
than a game interface library; it enters our C++20 build only through a C
ABI and cargo.

**And it is the one that worked.** Ruffle plays `mainMenu.swf` — the movie
the real game opens — and with our bridge it draws the menu. The details
and the patch are in docs/research/11-ruffle-menu.md. So the comparison is
no longer between two "maybe"s but between a working picture (Ruffle) and
a hang (gameswf).

**What it is worth.** BF2's menu is a 2 MB `mainMenu.swf` and 1485 bridge
names (docs/functions/menu-bridge.md). Rewriting an interface like that by
hand is months; playing the original movie with the same code the original
plays it with is a one-to-one port in the most direct sense. The price:
gameswf draws with its own backend (OpenGL/D3D) while all our graphics go
through `obf2::gfx`, so it would need a renderer of our own.

**The attempt was made.** gameswf builds for us on arm64 and reads all
four menu movies; the numbers match our own reader. But it still cannot
**play either of the two the game actually uses** (`mainMenu.swf`,
`endOfRound.swf`) — see docs/research/10-gameswf-on-arm64.md. It took 45
changed lines, and among them a genuine 64-bit defect in the library
itself: its hash table found **no** key at all, because `add` stored the
hash sign-extended. Details, build and patch are in
docs/research/10-gameswf-on-arm64.md.

## What follows for our plan

1. Inventory first: how much logic is open to begin with (`python/`, `.con`, `.tweak`) — that knowledge is free.
2. Asset formats — take them from Dalian/BfMeshView and only verify with our parsers.
3. Ghidra is mainly needed for: the render pipeline, physics/ballistics, the netcode protocol, bots (`.ai`), and a handful of "magic" binary formats.
4. Dynamic analysis (a hook DLL under Wine/CrossOver) is often cheaper than static.
