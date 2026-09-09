# Open questions and decisions that have to be made

This holds what I **do not decide alone**: places where several routes are
possible and the choice changes the architecture. Small technical
decisions are made on the spot and described in the code or in `docs/`.

## Needs a decision

*(empty — every open question is currently closed)*

## Decisions taken

- **The licence is MIT.** The `LICENSE` file. Third-party components in
  `third_party/` keep their own (miniz — MIT, stb_image — public domain).
  The editor's LGPL navmesh code **does not enter our tree**: we read it as
  a specification of the format and write our own.
- **Flash — done, the movie is played by Ruffle.** The in-game interface
  (HUD, scoreboard, spawn screen) is `.con`, not Flash; Flash is left only
  in the menu (`docs/research/06-server-and-hud.md`). We first bet on
  gameswf (public domain, the very library the original plays it with),
  but it would not run on arm64 —
  `docs/research/10-gameswf-on-arm64.md`. We went with **Ruffle** (Rust,
  MIT/Apache-2.0): it plays the same `mainMenu.swf` the game opens. The
  price is Rust in the build and a C ABI to our C++ (`src/flash`).
- **The main menu is the original `mainMenu.swf`.** Our own menu built on
  `hudBuilder` existed and was removed: it never showed what is actually in
  the game, and keeping two menus side by side turned out to be actively
  harmful — a click past the movie ran the other menu's commands. Now
  `openbf2` with no arguments opens the same movie the original does, and
  the engine gives it what `SwiffPlayer.dll` gives it in the game: the
  seventeen bridge objects, localisation, the profiles from `Documents`,
  the map list from the `.desc` files
  (docs/functions/menu-bridge.md, docs/research/11-ruffle-menu.md).
- **Byte-for-byte compatibility with original servers — not yet.** We read
  and understand the format, but we do not promise compatibility. We may
  come back to it. The rig is already there: the original dedicated server
  runs in a container (`tools/linuxded/`), with remote console and without
  PunkBuster. **The handshake is already compatible**: `openbf2 --connect`
  joins an original server and stays connected
  (`docs/research/09-network-protocol.md`). What is left is the contents of
  the data packets.
- **Master server — direct connections only for now.** We do not reproduce
  GameSpy and do not attach to BF2Hub/OpenSpy.
- **We do not implement PunkBuster** and do not pretend it is there.
- **SDL3 + SDL_GPU** instead of OpenGL — `docs/research/01-render-backend.md`
- **We do not reproduce Bink** — the logic and the order of the intro
  movies are 1:1, the playback is ours —
  `docs/research/03-startup-and-menu.md`
- **The editor branch of a level's `.con`** instead of the compiled
  `terraindata.raw` — `docs/formats/level.md`
- **The Windows build is deferred**, the base stays portable — `CLAUDE.md`

## Found, but not used yet

- **The 1.5 Linux server with full symbols and DWARF** —
  `Game Files/OtherFiles/linuxded/bin/ia-32/bf2`, imported into Ghidra.
  68 921 symbols. The main instrument for netcode; so far only BitStream's
  API has been taken from it. See `docs/research/04-netcode.md`.
- **The navmesh generator's source code** (LGPL, 121 files) in
  `bf2editor_and_tools/NavMesh/Navmesh_SDK/` — it documents the `.qti`,
  `.cls` and `.vbf` formats from the levels' `GTSData/`.
- **`bf2editor/Help/`** — `ObjectEditor_Help.xls`, `UserGuide.doc`, the
  `Workshop` and `Tutorial` directories. Only `CommandDescriptions.dat` has
  been taken apart (546 commands).
- **`maya/`** (49 MB) — the official exporters and MEL scripts of the mesh
  pipeline.
- **The vector compression tables** in the ELF's data section
  (`m_compressionVectorBitTable*`) — readable directly, not extracted yet.

## Technical debt

- ~~No culling~~ — done: the view frustum culls ~60 % of the level's
  instances.
- ~~No terrain light maps~~ — done: the baked lighting is multiplied by
  TerrainSunColor/TerrainSkyColor from Sky.con; the fog comes from the
  level's data too.
- **Fragment uniform buffers in SDL_GPU never reach the shader** (other
  data is read instead). Worked around: per-frame constants travel through
  a vertex uniform and then varyings. Worth getting to the bottom of —
  either our pipeline setup is wrong or it is a quirk of the Metal
  backend.
- Mesh LODs do not switch: lod 0 is always taken. The data for the other
  levels is in the files (3-4 per mesh).
- Sound is not implemented: 37 `Sound.con` commands
  (`objecttemplate.soundFilename`, `sound.masterVolume`) have no handler.
- The vegetation (`Overgrowth/`) and ambient objects are not loaded: the
  only trees on a level are the ones placed in `StaticObjects.con`.
- ~~The server only broadcasts world state~~ — done: input, a fixed 30 Hz
  tick, soldier movement, broadcasting moving objects, smoothing on the
  client.
- No client-side prediction: on a high ping the controls will feel sluggish.
- No UDP channel: only the in-memory loop works. The `net::Connection`
  abstraction for it is ready.
- ~~No collision~~ — done: `.collisionmesh` reads (1447 of the game's 1450
  files), the soldier layer is transformed into world coordinates and put
  into an 8 m grid. On Dalian Plant that is 472 objects and 52 587
  triangles. Walls can no longer be walked through.
- Collision uses a sphere rather than a capsule: it is possible to get
  stuck in narrow doorways.
- 435 level objects have no `collisionMesh` in their template — we walk
  through them (mostly vegetation and decoration).
- **The soldier's base speed in BF2 is set by animation**, not by a
  number: `AnimationSystem3p.inc` moves him along with the clip's
  playback. Until skeletal animation exists, the speed comes from the
  server's settings — the one place where we knowingly depart from the
  original.
- The game mode's logic is partly done: control points are captured and
  spawning goes to your own team's point. There are no tickets, no player
  teams, no vehicle spawning from the spawners (27 of them on Dalian Plant
  are already read), no death.
- `.con` commands without a handler: **378 unique, 9.1 % of calls**.
  `tools/command_audit` lists them with example arguments.
- **Skeletal animation is not implemented**: `.ske` (skeleton) and `.baf`
  (animation) are not read yet, so soldiers and vehicles are motionless
  inside.
- The intro movies' durations are nominal: we do not decode Bink and do not
  know the real ones.
- `stb_image` is built with PNG only — enable JPEG separately if it is ever
  needed.
