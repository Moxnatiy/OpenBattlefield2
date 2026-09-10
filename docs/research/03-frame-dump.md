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

## The checkout follows upstream's `main`

`reference/mtld3d` sits on a branch called `openbf2-dev` tracking
upstream's `main`, currently **02622eb** (past v0.8.0). Our three patches
are kept applicable to it; when upstream moves, re-cut them.

It was briefly pinned to the older `b37f18d` instead. Purple smoke and
deformed leaves on trees had appeared, and the update looked like the
cause — 294 commits had gone by, several of them in shader translation.
That reading was wrong: the same artefacts are there on `b37f18d` too, so
the update neither caused them nor fixes them, and there is no reason to
sit on an old tree. The pin is gone.

What the artefacts actually are is still open. Both are ours to
investigate, not upstream's to be told about, until we can say which draw
call goes wrong — a bug report that amounts to "smoke is the wrong colour"
helps nobody. The dump is the tool for it: smoke is a particle pass and
the leaves are vegetation, so both have a handful of draws to look at.

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
[dump] draw 40 vsc c7: 0.005882 -0.008235 -0.176471 1.176471
[dump] draw 40 vsc c8: 0.827451 0.749020 0.639216 0.400000
```

Two levels rather than one on purpose: a single frame would let almost any
row be read as a fog range. It worked — `FogRange` is settled, see
[../formats/shaders.md](../formats/shaders.md).

## Where the dump comes out

**Not in the game's log.** Since v0.8.0 mtld3d's D3D9 side writes to a file
of its own: `mtld3d-logs/<exe stem>-<pid>.log` beside the game's `.exe`, or
wherever the `log.dir` setting points. `/tmp/bf2run.log` holds only Wine's
output and the shim's one startup line, so grepping it for `[dump]` and
finding nothing looks exactly like a driver ignoring Ctrl+Shift+D. It is
not.

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

## Naming what the original drew, instead of guessing at it

Matching rectangle against rectangle is what made the interface expensive: two
icons of the same size a few pixels apart are indistinguishable, so every
disagreement had to be argued about rather than looked up. The dump carries the
answer already, and we were not reading it.

**The interface does not sample its pictures from files.** Everything under
`Menu/HUD/Texture/` is packed into two atlas pages — `Menu/Atlas/
MemeAtlas_010.dds` and `_020.dds`, 2048×2048 — and `Menu/Atlas/MemeAtlas.tai`
is the index: 986 lines, one per original file, each with the page and the
rectangle inside it in UV units. It is the same `.tai` format the object light
maps use, so we already had a reader for the idea.

Which page a call samples follows from the dump's own texture line. Over every
`.dds` in the game the pair (DXT3, 2048, 2048) belongs to `MemeAtlas_020` and
to nothing else, so `s0=TextureId(67)/0x33545844/2048x2048` **is** that page;
the DXT5 one beside it is `_010`.

And the call's vertices say which picture. The interface's vertex is eleven
floats, stride 44:

```
x  y  z   r g b a   u0 v0   u1 v1
```

so the atlas coordinate is at 7 and 8, and the colour at 3..6 is the node's
tint — `setNodeColor`, which the game multiplies the art by. Look the
coordinate up in the index and the call has a name:

```
draw 235   14.5  94.5  150 x 44   Ingame/Weapons/Icons/Hud/USRIF_MP5_A3.tga  150x44
draw 240  173.5 142.5   58 x 17   Ingame/Weapons/Icons/Hud/sasgr_fn2000_mini.tga  58x17
draw 233    9.5  72.5  246 x 69   Ingame/Respawn/kit_selected.tga  246x69
```

The entry's own size is checked against the call's, and where they disagree the
name is refused rather than printed: the corner then belongs to another quad of
the same call — the interface batches a frame, its icon and its caption into one
draw. `tools/hud_atlas.py` does all of this.

On the spawn screen of a joined server (48 two-dimensional calls) that names 8
outright, refuses 13 as first-quad-only, and leaves the rest to the font pages —
text is not in the atlas and never will be.

**What is left to name the other 13**: the geom patch already prints each
quad's screen rectangle, and printing its UV rectangle beside it is the same
loop. Then every quad of a batch gets its own name and the whole screen is a
list of file names — which is what our own `--hud-rects` prints for our side.
Comparing two lists of names needs no tolerance and no argument.
