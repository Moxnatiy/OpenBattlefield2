# OpenBattlefield2

Licence: MIT (see `LICENSE`).

Goal: a clean-room reimplementation of the Refractor 2 engine
(Battlefield 2, 2005) — an open engine that reads the user's own original
assets.

This is a **one-to-one port**, not a game "inspired by" the original. The
difference is fundamental and drives nearly every rule below: when we do
not know how something is done in the original, we write it down as debt
instead of filling it in with a guess.

## How the work goes

One pass is not "try something and see". It is four steps, and skipping
any of them is expensive:

1. **A measure.** First write down how we will know the task is done: a
   command, a number, a screenshot. "The enemy moves" is not a measure.
   "`--connect` prints a position update for another player's object more
   than once a second" is.
2. **A source.** The answer comes from the binary or from the game's
   data, not from memory (see "Sources of truth" and rule 12).
3. **Notes and code.** What was reversed goes into `docs/` and `src/`
   right away — **whole**, not just the side needed today (rule 3).
4. **Verification.** Run the measure. If it does not match, go back to
   the source rather than tweaking numbers.

A sign the pass went wrong: a third "let's try it this way" without a
single new look at the binary.

## Rules

### 1. Knowledge lives in files, not in the conversation

Never pull whole decompilations into the conversation. The order is:
search (a string, an xref, a table) → take apart **one** function with a
tool → write the notes into `docs/functions/<module>.md` — and do not go
back to the binary after that.

Write the notes **immediately**, while the binary is open. Every note
carries the address it came from; without one it cannot be checked.

### 2. Look whether the tool already exists

The list is below, under "Tools". This is not bureaucracy: the layout of
`readControlObjectState` was picked apart by hand from `objdump` output
even though `tools/linuxded/bitfields.py --blocks` prints it in one line
in 0.1 seconds. The tool was already there.

If the tool is missing — **make it first**, then work. One-off manual
work leaves nothing behind; a script is visible in git, can be re-run,
and shows where the answer came from.

### 3. Take a structure apart whole, not one field at a time

When a function, a format or a structure is open — write down **all** the
fields, **all** the mask bits, **all** the branches. Even the ones that
are not needed today.

This is arithmetic, not pedantry. One trip into a function costs the same
whether you take one field out of it or twenty. But when every next field
means going back, the work stretches over months — which is exactly what
happened to us: we went back into the controlled-object state three times
for one field each, and every time it cost the user a separate trip into
the game.

In practice that means:

* in the notes — a table of every field with sizes and the addresses they
  are read at;
* in the code — a structure with every field, even if part of it is
  unused (with a "not used yet" comment, not silently);
* fields whose purpose is unclear are written down too: size, offset, and
  an honest "purpose not established".

### 4. Do not reinvent what is already done

Before working on a format, check `docs/research/00-prior-art.md`. We use
the game's own data (`*_server.zip`, `*_client.zip`, `python/`,
`.con`/`.tweak`), the Project Dalian specifications (MIT) and BfMeshView.
Reverse engineering is only for what is not available openly.

`reference/` (not in git) holds third-party work for comparison. The
licences there differ: `breadflowerdos` has **none**,
`Refractor-2-BitStream-Emulator` is **GPL-3.0**; both are incompatible
with our MIT. It is a source of **hints**, not of code: everything taken
from there is verified against the binary or the game's data.

There is one exception: `reference/gameswf` is **public domain**. It is
the same library the original plays its Flash menu with
(docs/research/00-prior-art.md), so code may be taken from it directly.

### 5. Nothing is unpacked to disk

The game's archives are read in place through `obf2::FileSystem`, the same
way `fileManager` does it in Refractor 2. The `extract/` directory stays
empty.

### 6. No fudged numbers

Every constant in `src/` comes from one of three sources, and that source
is named in the comment next to it:

* the game's data (`.con`, `MemeFile`, a texture, localisation);
* the binary (an address in `BF2.exe`, `BF2_r.exe` or the Linux server);
* a direct measurement from a frame dump of the original
  (`Ctrl+Shift+D`).

Picking a number so that it "looks about right" is forbidden, even when
the result matches visually. With no source, leave it as it is, write
**"not measured"**, and say so out loud.

An indirect conclusion is fudging too. Example: we took a triple of
numbers in the controlled-object state for a position because it matched
the spawn camera. It is in fact the compression origin, and the soldier
was thrown a metre into the air on every packet.

### 6a. Every comment names the address it came from

Any constant, any field, any layout, any rule of behaviour in the code is
accompanied by **the address in the binary** (or the path to the data
file) it came from. Not "from the client", but `BF2.exe, 0x62d4e0`.

This is not decoration, it is the ability to check. The address lets
anyone open the same place in a minute and see for themselves; without it
the knowledge lives only in the head of whoever found it, and is lost in a
week.

The format is simple and already used throughout the code:

```cpp
// Bit 0 marks position (`BF2.exe`, SoldierNetworkable::setNetUpdate,
// 0x62d4e0). Precision 0.001 — constant 0x3a83126f at the same address.
inline constexpr std::uint32_t kSoldierStatePosition = 0x1;
```

If the source is the game's data, name the file and the line:
`objects/soldiers/common/common.con`, `Vars.Set phy-soldier-jump-factor`.

If there is no address, there is no source: write **"not measured"**
(rule 6) rather than a silent number.

### 7. Behaviour is reversed too, not only numbers

Knowing *what* value a variable has is not enough — we need to know **who
sets it and when**. Every HUD variable assignment refers to its source: a
state from `docs/functions/hud-states.md`, a console command from
`docs/functions/hud-commands.md`, or a place in the binary. Whatever we
set without a source is marked **"source not found"**.

### 8. Clean room

Code in `src/` is written from behaviour notes; decompiler output is
never copied verbatim.

### 9. Check yourself on screen, not in your head

A change to the HUD or the renderer is not done until it is visible in a
screenshot. A numeric proof ("14 position updates") is not the same proof:
it says the data is there and says nothing about whether it is visible.

When it cannot be checked on screen (no second player, an empty server),
say so plainly rather than passing a numeric proof off as a visual one.

### 10. Files are edited with the built-in tools

Code changes go through `Read`, `Edit`, `Write`, not through `python3 -c
"s.replace(...)"`. A blind substitution silently does nothing when the
text differs slightly, and hits the wrong place when the text occurs
twice. Scripts stay where they belong: **pulling data out of the binary or
out of the game's files**.

### 10a. The repository is in English

**Everything the repository carries is in English.** Code comments,
`docs/`, `README.md`, this file, **commit messages**, and every string the
programs print — logs, diagnostics, `--help`, the output of the tools in
`tools/`. If it ends up in a file or on a terminal, it is English.

The conversation with the maintainer is the one exception: it goes in
whatever language suits, and nothing is translated for it.

The comment format does not change: the address in the binary or the path
to the data file is named exactly as before (rule 6a).

`tools/translate_comments.py` moves comments over line by line; it refuses
to write when a line no longer matches, so a stale list fails loudly
instead of scrambling the source.

### 11. One thing per file

`src/app/main.cpp` is the application entry point: argument parsing, mode
selection, the frame loop. Everything else lives in its own module with
its own header and its own test.

The rule was not written out of tidiness: `main.cpp` grew to **3841
lines**, and the network conversation, the spawn screen state, the HUD
state table and geometry assembly all ended up inside it. The consequences
are visible: the same logic was written twice in two places, lambdas read
dead memory, and none of it is covered by a test.

The sign is simple: if it can be checked by a test, it does not belong in
`main.cpp`. The plan for breaking it up is below, in its own section.

### 12. When you do not know, open the binary — do not invent a way around

When behaviour does not match, the answer is looked for **in the binary**.
Not "let's try another number", not "let's add smoothing so it stops
jittering", not "let's take the neighbouring field, it looks about right".

The sign that you are working around instead of reversing: the comment you
are about to write wants to say "looks like", "picked", "so it stops
jittering".

## Sources of truth

### The main rule: we reverse `BF2.exe`, and we do it through Ghidra

**We are building a port of the client.** So the truth is in the original
client `BF2.exe`, not in the Linux server. The server is a reference: it
has symbols and signatures, and they make it easy to **understand** what
something is called and where to look. But the behaviour we reproduce
lives in the client.

**Take it apart through MCP `ghidra`**, not through `objdump` and scripts
over lines of disassembly. Ghidra sees what reading by address cannot:
control flow, function boundaries, types, references. A line-based script
broke on exactly that — it attributed the object's position to the wrong
mask bit, because the blocks in the binary are not laid out in execution
order.

The MCP tools and when to reach for them:

| call | what for |
|---|---|
| `search_strings` | find a foothold: a command name, a message, a file path |
| `list_xrefs` | who refers to this address or string |
| `decompile_function` | **the main one**: pseudocode with branches, not a raw instruction stream |
| `disassemble` | when the instructions themselves matter: constants, offsets, read widths |
| `search_symbols_by_name` | when the name is already known (transplanted from the server) |
| `set_comment`, `rename_function` | leave what was reversed in the project so it is not searched for twice |

Order of work: the game's data → `BF2.exe` through Ghidra → the server, if
the names are not visible in the client.

| source | what it gives | when to reach for it |
|---|---|---|
| **game data** | `.con`, `.tweak`, `MemeFile`, localisation | always first: most "engine constants" live in the data |
| **`BF2.exe`** | the original client — the thing we are porting | **the main source of behaviour** |
| **`BF2_r.exe`** | a checked build: 3111 `Debug` calls with file and line, 387 source files, field names in the text | a dictionary: which file something lives in and what it is called |
| **Linux server 1.5** | 36 076 functions with full C++ signatures | a reference for names and server logic — **not a replacement for the client** |

Files: `Game Files/BF2.exe`, `Game Files/BF2_r.exe`,
`Game Files/OtherFiles/linuxded[-full]/bin/amd-64/bf2`. The server's
exported symbols are in `docs/reference/linuxded-symbols.txt`.

Names from the server are transplanted into the Ghidra project with
`tools/transplant_symbols.py --script` — after that the functions in
`BF2.exe` have human names and `decompile_function` can be called by name.

## Tools

**Binary work goes through MCP `ghidra`** (see "Sources of truth"). The
scripts below stay auxiliary: they are cheap where a **list** or a
constant's value is needed, and no good where control flow is needed.

| command | what it gives | limit |
|---|---|---|
| `tools/transplant_symbols.py --script` | server names into the Ghidra project | run this first on a new project |
| `BF2_PE="Game Files/BF2_r.exe" tools/transplant_symbols.py --source-map` | address → source file and line (3111 checks) | `BF2_r.exe` only |
| `tools/elf_symbol.py <name> --type f32\|f64\|u32` | the value of a global from the server's ELF | one number, no context |
| `tools/linuxded/bitfields.py --blocks <Class::method>` | the `readBits` list and block graph in the server | **does not say which bit a field is under** |
| `tools/linuxded/statefields.py` | a draft "mask bit → field" | **the output cannot be trusted**, see the file |
| `tools/exe_xref.py` | string → who refers to it | Ghidra does this better |
| `tools/linuxded/gen_events.py` | the game event size table | one-off generation |
| `tools/hud_variables.py` | HUD variable → object, type and field | takes the decompilation of the registration functions |
| `tools/swf_read.py` | the game's menu: screens, texts, calls into the engine | the `.swf` files in the game are uncompressed |
| `tools/swf_disasm.py` | the menu's byte code itself: branches, function bodies, component parameters | `--tag 26` reads the parameters in `ClipActions` |
| `tools/swiff_bridge.py` | the objects and methods of the menu → game bridge | `SwiffPlayer.dll`, groups in `.rdata` |
| `tools/gameswf_probe.cpp` + `gameswf_macos.patch` | build gameswf and read the menu movie with it | docs/research/10-gameswf-on-arm64.md |
| `tools/ruffle_bf2_bridge.rs` + `.patch` + `ruffle_install.sh` | the menu → game bridge for Ruffle | docs/research/11-ruffle-menu.md |
| `tools/bmp_crop.py` | cut a piece out of a screenshot and magnify it | screenshots are BMP |
| `tools/translate_comments.py` | move comments over to English line by line | refuses to write on a mismatch |

**Game data:** `tools/hud_audit.py`, `hud_commands.py`, `hud_coverage.py`,
`hud_states.py`, `hud_fields.py`, `con_objects.py`, `meme_read.py`,
`meme_dump.py`, `meme_types.py`, `extract_command_descriptions.py`.

**Our own dumpers** (C++, built with the project): `mesh_info`,
`object_info`, `texture_info`, `ske_info`, `baf_info`, `anim_info`,
`con_dump`, `hud_dump`, `command_audit`.

**Live rig:** `tools/linuxded/` — Docker with the original server,
`capture.py` (record traffic), `pcap_bf2.py` (take a `.pcap` apart),
`rcon.py`, `probe_connect.py`.

## Layout

```
Game Files/     the original BF2 installation (not in git)
extract/        must stay empty (rule 5)
reference/      third-party repositories for comparison (not in git, other licences)
third_party/    vendored single-file libraries: miniz, stb
```

| module | what it does |
|---|---|
| `core` | platform, paths (`normalizeAssetPath`), maths, `parallelFor` for the loading phases |
| `vfs` | the game's archives, mounted the way `fileManager` does |
| `con` | lexer and interpreter for the `.con` language |
| `texture` | `.dds` |
| `mesh` | meshes, skeletons, skinning, collision, simple bodies |
| `anim` | the animation system's trigger tree |
| `font`, `loc` | `.dif`, `.utxt` |
| `game` | the `ObjectTemplate` registry, the scene, control |
| `level` | terrain, water, placement, `GamePlayObjects.con` |
| `gfx` | window and GPU (SDL3 + SDL_GPU) — **the only place with graphics** |
| `hud` | the `hudBuilder` node tree, geometry, transitions |
| `meme` | the `MemeFile` animation graph |
| `net` | BitStream, protocol, events, connection |
| `server` | local server, soldier physics, collision world |
| `engine` | console, settings, key bindings |
| `flash` | the Flash menu: Ruffle behind a C ABI |
| `app` | the `openbf2` executable — and nothing else (rule 11) |

### Breaking up `main.cpp`

In order of usefulness. Each item is a module with a test:

1. **`net/bf2_session`** — `RemoteWorld` whole: the conversation with the
   server, player and object state, the action stream. This is the
   largest piece and the one best covered by tests (we already have
   recorded traffic).
2. **`app/spawn_screen`** — the spawn screen: selection state, clicks, DONE.
3. **`app/frame_loop`** — camera, input, the prediction step.
4. **`hud/ingame`** — assembling the battle HUD and its live values.
5. **`app/scene_build`** — loading a level into the scene and uploading it
   to the GPU.

## Building

```bash
cmake --preset macos-arm64-debug && cmake --build --preset macos-arm64-debug
ctest --test-dir build/macos-arm64-debug --output-on-failure
```

The main platform is **arm64 macOS**; the Windows preset is kept building.
C++20 without extensions, `-Wall -Wextra -Wpedantic -Werror`, nothing
platform-specific outside `obf2/core/platform.h`, paths only through
`normalizeAssetPath`, graphics only through `obf2::gfx`.

Tests: one file per parsed subject. **No parsed format stays without a
test.**

## How we check ourselves

```bash
# a screenshot of the spawn screen
openbf2 --level Dalian_plant --width 800 --height 600 --frames 4 --screenshot out.png

# what exactly landed on screen: rectangle, texture, show variable
openbf2 --level Dalian_plant --frames 2 --hud-rects

# a particular HUD screen
openbf2 --level Dalian_plant --frames 4 --hud-screen Scoreboard

# a synthetic click (DONE on the spawn screen)
openbf2 --connect <host> --level dalian_plant --frames 5000 --click --mouse 727 546

# clicks on a schedule — the menu leads the player through several steps
openbf2 --frames 260 --click-at 20:400:240 --click-at 60:682:531

# matching template numbers to names
openbf2 --level dalian_plant --calibrate tests/data/bf2-world.bin
```

The original for comparison: `tools/bf2_run.sh`, `BF2_LEVEL=dalian_plant`,
`BF2_SERVER=<host>`. `Ctrl+Shift+D` under `mtld3d` dumps a frame — the
only source for renderer measurements.

## Debt: what is not reversed

This is not a wish list. It is a list of places where we knowingly do not
know how the original does it. Each has a measure.

**The protocol (the largest debt).**

| what | state | measure |
|---|---|---|
| `readControlObjectState` | we read the start, we do not know the end | ghost records are read in packets that also carry controlled-object state |
| ghost records for other players' soldiers | the layout is known, the data does not arrive | another player's position updates more than once a second |
| tickets on `--connect` | the layout is known; it rides a ghost (`ScoreManager::setNetUpdate`, 0x5c9650) — blocked by the line above | the numbers match the original on the same server |
| simple object state mask (19 bits) | we know 1 bit out of 19 | all 19 named in the notes |
| soldier state mask (21 bits) | we know 1 bit out of 21 | all 21 named |
| template number → name | the number is creation order, ours differs | `--calibrate` says "matched 21" |
| ping | ours and the original's differ by 5–6× on the same machine | the difference is within noise |

**Physics.** The tick step is taken from the binary (1/30), but the jump
itself is too long compared with the original. The source of jump height
and duration has not been found: `phy-soldier-jump-factor` is in the data,
the rest is not. Measure: jump height and duration match those measured
from the original.

**HUD.** Regions and speeds now come from `Menu/Ingame` — the graph runs
(`obf2::meme`, docs/formats/hud-meme-graph.md). What is left:

| what | state | measure |
|---|---|---|
| `Kit<N>Show`, `CPInterfaceEnabled` | the fields are found (`HudInformationLayer`, docs/functions/hud-variables.md: +0x202+N and +0xa8); **who writes them is not** | the notes name who writes |
| ticket colours (`FriendlyTicketRed` +0x158 and neighbours) | the fields are confirmed in the registry (docs/functions/hud-variables.md); the mechanism exists (`setNodeRGBVariables`), the values are unknown | the ticket numbers are the same colour as in the original |
| `FriendlyTicketBleed` 0x8c | 0x4646b0 writes the neighbouring field 0x88 from `team->+0x90`; whether it is the same is not established | the bar blinks while tickets bleed |
| who turns on `BottomRightDirection` | the machine is reversed (docs/functions/hud-bottom-right.md), only the matching "hide" at 0x7a8280 was found | the notes name who writes the one |
| `BottomRightFadedAlpha` (+0x14) | the formula for the left side exists (end of 0x78b600), the place for the right side was not found | nodes on this variable fade as in the original |
| `VariableColorEffect`, `AlphaFadeEffect` in the graph | not executed | in `Menu/Ingame` they only appear on test squares under `showTest`, so there is no visible debt |

**Menu.** The menu ↔ game bridge is reversed — seventeen objects, all
forty methods of `Logic` with addresses (docs/functions/menu-bridge.md).
The menu itself is the game's own `mainMenu.swf`, played by Ruffle
(docs/research/11-ruffle-menu.md). Not done: the tables for the other
sixteen objects (the approach is known and mechanical), what `c_GIMenu`
and `c_GIEscape` do in the game, the screens other than singleplayer, and
the chevrons on `PLAY NOW` — the movie never asks for
`images/components/playNow.png` and it is not established why. Measure:
Escape in a battle opens the menu, and it can be used to quit the game.

**There is no reference for the battle HUD.** We have a frame dump of the
original (`docs/research/03-frame-dump.md`) only for the spawn screen —
172 rectangles. There is no such dump for a battle, so for the top bar and
the scoreboard we cannot currently say "matches or not", only "matches the
data". Measure: the same dump, taken in a battle.

## Rakes we have already stepped on

* **A `[&]` lambda called from the frame loop.** The HUD context and the
  spawn screen state each turned out to be local variables of a block:
  after the block exited, the lambda read dead memory. Anything that
  outlives the block is declared at the outer level.
* **A helper layer that shadows the source of truth.** The transition
  animator answered "show" about nodes it did not know. Such a layer must
  be **advisory**: when it does not know, the main condition decides.
* **The same logic in two places.** The team side for map icons existed
  twice; one was fixed and the other stayed swapped.
* **A world sampler in the interface.** `REPEAT` at the edge of a quad
  drags in the opposite edge of the texture.
* **One reader for everything in a row.** An error parsing one ghost
  record killed the whole stream. The right way is the engine's way: the
  main pass goes **by length**, the contents are read by a separate
  reader.
* **A physics step as long as a frame.** The engine counts in 1/30 ticks
  (`WorldPref::mTickTime`); without that the jump differs at 60 and 120
  frames per second.
* **Reading the input twice in a frame.** `InputState::clicked` is an
  edge, and `readInput()` consumes it: the second call in the same frame
  always sees "not pressed". Read it once per frame and share it.
