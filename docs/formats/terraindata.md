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

## The per-patch blocks

The writer's loop, and what the four calls around its blob turn out to be:

```c
for (patch = 0; patch < patchesPerSide * patchesPerSide; ++patch) {
    writeU32(p[0x68]); writeU32(p[0x6c]);      // the patch's place in the grid
    writeFloat(highest(patch)); writeFloat(lowest(patch));
    writeVec3(p + 0xa0); writeFloat(p[0xac]);
    FUN_10109390(…, p);                        // the geo-morph deltas, below
    ptr  = FUN_10014800(0, 0, 0);              // lock a surface
    rows = FUN_100148e0();                     // its height
    pitch= FUN_100148d0();                     // its pitch, in bytes
    writeRaw(ptr, rows * pitch);               // the surface, as it lies
    FUN_10014860();                            // unlock
}
```

`FUN_10014800` is a lock, not a compressor: it calls the object's own
`+0x2c` with `(pitch * y, pitch * x, &out, flags)` and returns the pointer,
`FUN_10014860` unlocks, `FUN_100148d0` returns the pitch (`this+0x28`) and
`FUN_100148e0` the row count (`this+0x20 / this+0x28`, size over pitch). So
the blob is **a texture written out raw**, and the size is the surface's own.

`FUN_10109390` (0x10109390) is the interesting half: it walks the patch's
`(patchSize + 1)²` vertices and, for each, computes four **geo-morph deltas**
— one per LOD level, `1 << (level + 1)` apart — as the difference between the
vertex's own height and the average of the two neighbours the coarser level
would keep. It tracks the smallest and the largest of each level, writes those
two `float[4]`, and then the whole delta array. That is what feeds
`geoMorphPosition` in the terrain shader, and it is the thing that lets a
patch change LOD without popping.

So a patch's block is: where it sits, its height range, a vector and a float,
its morph deltas with their bounds, and one raw surface.

### Which surface, and why it changes the plan

The lock is on `patch + 0x5c` (`0x1010d293`, `MOV ECX,[ESI + 0x5c]` right
before the call), and `TerrainPatch::init` (`FUN_1019fa10`, 0x1019fa10) shows
what a patch's textures are and which of them come from **files**:

| field | what | where it comes from |
|---|---|---|
| `+0x148` | the tile's name, `"%02ix%02i"` | built from the column and the row — this is the `00x00` of `tx00x00.dds` |
| `+0x4c` | the colour map | `<colormapBaseName><name>.dds`, or created and filled |
| `+0x50` | the light map | `<lightmapBaseName><name>.dds`, else filled white |
| `+0x40`, `+0x44` | the two chart maps, `"%s_%i"` | `<detailmapBaseName><name>_1.dds` and `_2.dds`, else filled `0xffff0000` and `0xff000000` |
| `+0x48` | the low-detail component map | filled `0xff0000` when absent |
| `+0x5c` | **the surface the blob holds** | not loaded from any file |

So the per-patch payload in `terraindata.raw` is **geometry, not materials**:
the heights, beside the morph deltas that let a patch change LOD. Everything
with a texture in it — the colour maps, the light maps, the chart maps that
say which of the six materials owns which texel — is in the `.dds` files
beside the blob, which we already read.

That changes what this file is for. We do not need the rest of it to texture
the ground close up: the six materials are in the header we already parse,
the chart maps are `Detailmaps/txCCxRR_1.dds` and `_2.dds`, and the
low-detail texture is the level's own. What the blob would still buy us is
the terrain's *geometry* the way the game builds it — the morph deltas, and
heights that need no `.raw` beside them.

Left: the eight secondary terrains at the end, before the closing
`0xffffffff`, and the exact format of the height surface.
