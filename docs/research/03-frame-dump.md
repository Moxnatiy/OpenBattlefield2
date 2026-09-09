# A frame dump from the original

Our `mtld3d` driver (D3D9 -> Metal) can write out a whole frame at a single
keypress: every render target, every clear and **every draw call** together
with the state, the shaders and the bound textures. That gives a reference to
check the HUD against number by number rather than by eye.

mtld3d's licence is zlib, so patching it is free.

Our three patches live in `tools/` and are kept applicable to upstream's
current `main`: `mtld3d_frame_dump_geom.patch` (where a draw landed),
`mtld3d_file_trigger.patch` (arm a dump from a file instead of a keypress)
and `mtld3d_frame_dump_constants.patch` (the frame's vertex constants).
They are independent and go on in any order:

```bash
cd reference/mtld3d
for p in geom file_trigger constants; do
    patch -p1 < ../../tools/mtld3d_frame_dump_$p.patch
done
```

When upstream moves, re-cut them from the working tree rather than
hand-editing: the tree is the thing that builds, and a patch that has
drifted from it silently loses work. That has happened once already — the
richer `geom` line (stride, per-quad rectangles, the first and last
vertex) lived only in the working tree for a while and was nearly lost to
an update.

## The checkout is pinned to b37f18d, on purpose

`reference/mtld3d` sits on a branch called `openbf2-pinned` at **b37f18d**,
not on upstream's `main`. Moving it to `02622eb` (294 commits later, past
v0.8.0) broke the picture in the game: **smoke turned purple and the
leaves on trees came out deformed**. Both are upstream's, not ours — our
patches only add logging and read vertex data, they write nothing.

The suspects, from the commits in that range that touch shader translation:

| commit | why it fits |
|---|---|
| `5be21e4` Apply sampler swizzles to texture sample results | a wrong swizzle turns a texture's channels round, which is what purple smoke looks like |
| `e55f04a` Give vs_1_1 expp its four-component result | vegetation is animated in the vertex shader |
| `9aa8e7f` Honor predicates on mova address writes | same shader, address registers |
| `568ef66` Translate predicate-based shader flow control | same |
| `626ecbd` Preserve components masked by SM3 predicates | same |

Narrowing it further means a bisect, and every step costs a cross-build
and a run of the game. Worth doing before reporting it upstream, not
before the next thing we actually need.

## How to take one

1. Start the game: `BF2_LEVEL=dalian_plant BF2_RES=800x600 tools/bf2_run.sh`
2. In the game press **Ctrl+Shift+D** — three consecutive frames are taken.
3. The `[dump]` lines land in the launch log.

(`F12` in the same place starts a full Metal GPU capture into `.gputrace`,
but for the layout it is overkill.)

## What was missing and what was added

The dump named *which* texture was drawn but not **where**. The patch
`tools/mtld3d_frame_dump_geom.patch` adds the bounds: the position is the
first two `float`s of every vertex, so a min/max over them gives the
rectangle. It is applied at three entry points — `DrawPrimitiveUP`,
`DrawIndexedPrimitiveUP` and, most importantly,
`DrawPrimitive`/`DrawIndexedPrimitive`: BF2's interface goes through **bound
vertex buffers**, not through `*UP`, so without that path not one of the
frame's 853 calls gave any bounds.

A line looks like this:

```
[dump] draw 257: rt=… vs=… ps=… tex=[s0=TextureId(64)/…/2048x2048]
       geom=[-385.5,-139.5 150.0x44.0]
```

## The frame's own constants

`tools/mtld3d_frame_dump_constants.patch` adds one more thing the dump did
not carry: the **vertex shader constants**, printed once per dumped frame
on its first draw.

Everything the engine uploads per frame rather than per draw lands there,
and none of it is in the game's data files. The fog is why the patch
exists. BF2 ships its shaders as source, so what `FogRange` is *used* for
is readable (`Shaders_client.zip:RaCommon.fx:54`), but what it *is* is
packed by the engine — and the name is nowhere in `BF2.exe`, neither as a
string nor as a semantic, so the binding lives in the compiled effect and
the code addresses parameters by handle.

```
[dump] vsc c93: 0.000000 135.000000 0.000000 0.000000
```

To read it, take a dump on two levels whose fog differs —
`Renderer.fogStartEndAndBase` is `0/135` on Strike at Karkand and `0/610`
on Dalian Plant — and the row that moves with them is the one. Two levels
rather than one on purpose: a single frame would let almost any row be
read as a fog range.

## Two things that are visible at once

**The coordinates are centred.** Zero sits in the middle of the screen, so
the screen position is `(400 + x, 300 + y)` at 800x600.

**The textures are atlased.** Almost everything is pulled from a single
2048x2048 sheet (`TextureId(64)`) — that is `GSUseEffectTextureAtlas`. So
recognising an element by its texture's size will not work; the dump's value
lies precisely in the rectangles.

## Cross-check: our layout matches exactly

The kit list on the spawn screen is seven rows with a step of 66. In the
dump:

```
150x44 at y = 160.5, 226.5, 292.5, 358.5, 425.5, 490.5
58x17  at y = 142.5, 208.5, 273.5, 340.5, 407.5, 472.5
```

In `HudElementsSpawn.con`:

```
createPictureNode Kit1Info      Kit1WeaponIcon     15 161 150 44
createPictureNode Kit1Unlocked  Kit1AltWeaponIcon 174 143  58 17
```

So `(15, 161)` against `(14.5, 160.5)` — a difference of exactly the half
pixel D3D9 adds to align texels. **Our geometry is right.**

## What we are actually missing

Not the coordinates but the **state**. The kit panels hang on variables the
game sets from the kit's data:

```
createSplitNode KitsSelectionInfo Kit0Info
setNodeShowVariable Kit0Show
  createSplitNode Kit0Info Kit0NotSelected
  setNodeLogicShowVariable NOT PlayerKitIcon0SelectShow 1
  createSplitNode Kit0Info Kit0Selected
  setNodeShowVariable PlayerKitIcon0SelectShow
```

Until our client fills `Kit0Show`…`Kit6Show`,
`PlayerKitIcon<N>SelectShow`, `KitUnlock<N>Show` and so on, the spawn screen
stays empty — even though it draws everything correctly.

The full table of the original's 172 rectangles is in
`docs/research/spawn-screen-reference.md`.

## How to take one without a keyboard — and why that was needed

`Ctrl+Shift+D` needs a hand on the keyboard, and that rules out everything
automatic: a script, a launch from a shell, an agent. So two instruments were
made.

### 1. A file trigger in mtld3d

`tools/mtld3d_file_trigger.patch` adds a check of two files every 15 frames
to `windows/d3d9/src/capture.rs`:

```
/tmp/mtld3d_dump      = Ctrl+Shift+D   (a frame breakdown into the log)
/tmp/mtld3d_capture   = F12            (a Metal trace into .gputrace)
```

Wine maps `Z:` onto `/`, so inside the driver itself the paths are written as
`Z:\tmp\...`. The file is removed as soon as the request is taken — one
`touch` fires once.

```
touch /tmp/mtld3d_dump && sleep 4 && grep -c 'geom=' /tmp/bf2run.log
```

### 2. An `.app` wrapper around Wine

Wine started from a shell is, to macOS, a process **with no bundle id**
(`lsappinfo` shows `bundleID=[ NULL ]`). Because of that nothing that works
through LaunchServices can see it: neither application control nor Xcode's
Metal tracing, which also asks which application to trace.

`tools/bf2_app.sh` assembles the smallest possible `.app`: an `Info.plist`
with its own bundle id (`org.openbf2.original`) and a script that **replaces
itself** (`exec`) with the Wine process — the pid stays the same, so
LaunchServices goes on considering it that application.

```
BF2_LEVEL=dalian_plant tools/bf2_app.sh
open -a "$HOME/Applications/OpenBF2 Original.app"
```

After that the game is visible as an ordinary application:

```
98) "OpenBF2 Original" ASN:0x0-0x1614613:
    bundleID="org.openbf2.original"
```

The wrapper also sets `MTL_CAPTURE_ENABLED=1` — without that variable, set
**at the process's start**, `MTLCaptureManager` silently refuses and no Metal
trace will begin.

### 3. Metal tracing — the same thing, but by macOS's own means

```
touch /tmp/mtld3d_capture
```

```
INFO mtld3d::unix] started GPU capture -> /tmp/mtld3d_capture.gputrace
INFO mtld3d::unix] stopped GPU capture -> /tmp/mtld3d_capture.gputrace
```

The result is a genuine `.gputrace` (146 MB in our measurement, 1314 records:
`MTLBuffer-*`, `MTLTexture-*`, `CAMetalLayer-*`). It opens in Xcode as an
ordinary GPU capture, with all the resources and passes.

Which to take when: the textual `[dump]` gives the **rectangles**, and it is
what suits checking the layout numerically; the `.gputrace` gives the GPU's
state in full — shaders, buffers, texture contents — and is needed when the
question is not "where" but "drawn with what".

### What **cannot** be taken this way

A screenshot and mouse movement get through, but **clicks do not**: BF2 reads
the mouse through DirectInput, and background input does not reach it. So
picking a spawn point and pressing DONE from the background does not work —
that needs full-screen control, which the user confirms separately.

In other words the spawn screen takes itself, while the combat HUD only after
someone presses DONE.

## A second spawn-screen dump

`docs/research/spawn-screen-reference-2.md` — taken this way already, 46
calls. A comparison with our client:

```
tools/hud_coverage.py /tmp/bf2run.log ours.txt
   original: 46 calls, ours: 474 rectangles
   matched: 24 (52%)
```

The most interesting thing not reproduced is the **title bar**:

```
call 243   249.5  -0.5   536.0 x 47.0     (the bar itself)
call 244   335.5   4.5    98.5 x 11.0     ("SELECT SPAWNPOINT")
```

There is **no** 536x47 node in the HUD's data at all — checked by searching
every `HudSetup/*.con`. So that bar is drawn by something other than
`hudBuilder`, and by what exactly is not established yet. We do not have it
at all.
