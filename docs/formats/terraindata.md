# `terraindata.raw` — the terrain the game actually loads

A level ships its terrain twice. The editor's branch of `Init.con` names the
loose files — `HeightmapPrimary.raw`, `Colormaps/`, `Lightmaps/` — and the
game's branch loads one compiled blob:

```
terrain.create Terrain
terrain.load Levels/Strike_at_Karkand/terraindata.raw
```

Which branch runs is settled and not guessed: `GameLogic::loadLevel`
(`BF2.exe`, 0x004ed750) passes `"BF2Editor"` as `v_arg1` only under the
editor flag, and eight empty strings otherwise
([../research/12-renddx9.md](../research/12-renddx9.md)). So this file is
what the original reads, and everything in it is what it draws with.

We read the loose files instead, which costs nothing for the heights and the
tiles — they are the same data — but it costs us the one thing the blob has
and the `.con` does not: **the terrain's materials**. Those name the detail
textures the ground is textured with close up, and without them the ground
is a blurred colour map (docs/TODO-graphics.md, stage 2).

## The layout comes from the writer

Not from staring at bytes: `TerrainEditable::save` writes the file field by
field (`RendDX9.dll`, `FUN_1010cd70`, 0x1010cd70 — the assert beside it names
`Code\BF2\Geom\TerrainEditable.cpp`), and `tools/terrain_raw.py` walks the
same order. Its primitives are visible in the call list — `u32`, `f32`,
`vec3`, `u8`, and a string writer that appends a newline and no length.

```
u32    version              0x0001001a on every level of the game
vec3   primaryWorldScale    terrain.primaryWorldScale
vec3   secondaryWorldScale  terrain.secondaryWorldScale
f32    (uninitialised)      0xcdcdcdcd — MSVC's debug fill, written as it lay
f32    highest height       the maximum over every patch
f32    lowest height        the minimum
u32    patchSize            terrain.patchSize (128 everywhere)
u8     subdividePatches
u32    patches per side     4 on Karkand, 8 on Dalian
u32    patchColormapSize    512
u32    lowDetailmapSize     512
str    colormapBaseName     "Levels/Strike_at_Karkand/Colormaps/tx"
str    detailmapBaseName
str    lowDetailmapBaseName
str    lightmapBaseName
f32x2  farSideTiling
f32    farTopTilingHi, farTopTilingLow, farYOffset
vec3   terrain.sunColor     the pair Sky.con's game branch sets
vec3   terrain.GIColor
vec3   terrainWaterColor
u32    6                    the terrain's materials, six of them
6 x {
    str  texture            "common\terrain\textures\detail\detail_rock04"
    u8   flag
    f32  tiling x, tiling y
    f32  distance           how far it is used to
    f32  (zero on both levels read)
    u8   flag
}
... one block per patch, then up to eight secondary terrains, then 0xffffffff
```

Every value cross-checks against the level's own `.con`: Karkand's scales,
`patchSize 128`, `patchColormapSize 512`, the four base names, the tilings,
and `terrain.sunColor 0.75/0.71/0.57` beside `GIColor 0.73/0.64/0.33` —
which is how we know the walk is right rather than plausible.

## What it gives us

Strike at Karkand:

```
[0] detail_rock04    tiling 32/16  distance 50
[1] detail_grass05   tiling  2/2   distance 64
[2] detail_gravel    tiling  3/2   distance 64
[3] detail_tarmac02  tiling  2/2   distance 42
[4] detail_stones03  tiling  2/2   distance 64
[5] detail_cobble2   tiling  2/2   distance 64
```

Dalian Plant has its own six — `detail_daliandirt`, `detail_beachgravel`,
`detail_grass09_v2`. Six is not a coincidence: the shader selects among them
with `vComponentsel` against a chart map, and a level's `Detailmaps/txCCxRR_1.dds`
and `_2.dds` are the two maps that say which material owns which texel.

One oddity worth recording rather than smoothing over: Karkand's blob says
`farTopTilingLow 24` while its Terrain.con says 4. The field order is not the
thing in doubt — Dalian Plant settles that, its `.con` saying `farTopTilingHi
10` and `farTopTilingLow 12` where the blob has 10 then 12, in that order. So
on Karkand the two files were saved from different states of the editor, and
the blob is what the game loads.

That the two agree at all is not luck: **the same function writes both**.
`TerrainEditable::saveAll` (`RendDX9.dll`, `FUN_1010d9d0`, 0x1010d9d0) writes
the level's `Terrain.con` — the `if v_arg1 == BF2Editor` / `terrain.create
TerrainEditable` … `else` / `terrain.create Terrain` / `terrain.load
<path>/terraindata.raw` / `endIf` we have been reading is generated text, one
`operator<<` per line — and then calls the blob writer above. The two
branches of every level's Terrain.con and the blob beside it are two
spellings of one editor state.

## Not read yet

The per-patch blocks and the eight secondary terrains. The writer shows their
shape — two `u32`, the patch's height range, a `vec3` and a `f32`, then a
`w * h` byte blob per patch — but the blob's dimensions come out of a
compressor (`FUN_10014800` and its two size calls), and that is the next
thing to take apart.
