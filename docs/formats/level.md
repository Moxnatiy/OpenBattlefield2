# Levels

Status: **implemented** — `src/level`. Dalian Plant loads in full: a
1025×1025 terrain, 51 patches with colour maps, the sea, 906 static
objects, 2.35 M triangles a frame.

## No reversing needed here

A level describes itself with ordinary `.con` files, which our interpreter
already handles:

| File | What it gives |
|---|---|
| `Heightdata.con` | size, scale and bit depth of the height map, sea level |
| `Terrain.con` | patch size, colour / light / detail map names |
| `StaticObjects.con` | 907 placements: `Object.create` + `absolutePosition` + `rotation` |
| `Water.con` | the water's colour and parameters |
| `HeightmapPrimary.raw` | 1025×1025×2 = 2 101 250 bytes, 16-bit, little-endian |

The sizes work out without a single assumption: `1025 * 1025 * 2` is
exactly the file's size. A scale of `2/0.00640869/2` means 2 world units
between nodes and a maximum height of `65535 * 0.00640869 ≈ 420`.

## The branch: the game's, with no argument

A level's `Init.con` has two branches, and so does its `Terrain.con` and its
`Sky.con`:

```
if v_arg1 == BF2Editor
  ... twenty lines of terrain.* and the source height maps
else
  terrain.load Levels/<name>/terraindata.raw   ← a compiled blob
endIf
```

We took the editor's branch for a long time, because the compiled blob would
have had to be reversed and the editor's files are described by the data
itself. The blob **is** reversed now (docs/formats/terraindata.md), so we
run every level the way the game runs it: `Init.con` with no argument, and
its `else` branch calls Heightdata, Terrain, Sky, CompiledRoads, the
overgrowth, the ambient objects and Water in order.

What that changed, beyond being the same path the original takes:

* the terrain's six near materials, which exist **only** in the blob and are
  what the ground is textured with close up;
* `farTopTilingLow` on Karkand — 4 in the `.con` and 24 in the blob, and the
  blob is what the game reads;
* the editor's 350 `run` lines at the top of StaticObjects.con, which loaded
  object templates we already have (we read every `.con` in the archives), and
  `DefaultEnvMap`, an editor-only object with no geometry that we placed on
  eleven of the game's levels;
* the ambient objects — birds and their effects — which the game's branch
  loads and the editor's does not.

Two places still take the editor's argument, and each is written down where
it is passed:

* **`StaticObjects.con` is run by us, not by the chain.** The game reaches it
  through `run tmp.con`, and `tmp.con` in the archives is zero bytes: the game
  writes that list itself at load time.
* **`Roads/Splines/*.con`** — all eighty files are a single
  `if v_arg1 == BF2Editor` around their whole body, so with no argument a road
  has no texture at all. Where the game reads road templates from instead is
  not established.

The switch was verified frame by frame: Strike at Karkand and Dalian Plant
render **pixel for pixel** what they rendered from the editor's branch.

Getting there needed a fix in the interpreter, and it is the reason this took
so long to notice: `else` was not implemented. A false `if` swallowed its
`else` branch as well, and a true one ran both. The game's data has 534 `if`s
and 110 `else`s.

## The addHeightmap argument trap

```
heightmapcluster.addHeightmap Heightmap 0 0     ← the main map
heightmapcluster.addHeightmap Heightmap 0 -1    ← the surroundings on the horizon
```

The zeroth argument is a **name**, not a coordinate. Read the cluster
coordinates from positions 0 and 1 and `Heightmap 0 -1` parses as (0, 0),
so the secondary 257×257 map replaces the main 1025×1025 one. That is
exactly what happened on the first run: the level loaded, but four times
smaller and at the surroundings' scale. There is a test for this
substitution.

The cluster is 3×3: the central map is the playable area, the eight around
it are the low-detail surroundings visible on the horizon. We take only
the central one.

## Terrain

It is split into patches of `patchSize` (128) quads: 1024 / 128 = **8×8 =
64 patches**. The node on a border is shared with the neighbouring patch,
otherwise gaps would remain between them, so a patch has 129×129 vertices.

Every patch has its own colour map `Colormaps/tx<column>x<row>.dds`. The
game has **51 of the 64** — patches entirely under water simply have no
colour map, and the game does not draw them. We skip those patches too,
and the sea is covered by a water plane at `setSeaWaterLevel` (141.4 for
Dalian Plant) in the colour from `renderer.waterColor`.

The terrain is centred on the origin: `x = (node - (size-1)/2) * scale.x`.
That is the same system `Object.absolutePosition` is given in, so objects
land in their places right away with no corrections.

## Static objects

`Object.create <template>` + `absolutePosition x/y/z` +
`rotation yaw/pitch/roll`. Every template is resolved through the
`ObjectTemplate` registry and assembled the same way vehicles are (see
[vehicle-assembly.md](vehicle-assembly.md)).

907 placements give **131 unique geometries** — the same building occurs
dozens of times, so a mesh is uploaded to the GPU once and then drawn with
different matrices. One template was left without geometry,
`DefaultEnvMap` — that is not an object but an environment map.

## Checking

```bash
./build/macos-arm64-debug/src/app/openbf2 --level Dalian_plant
./build/macos-arm64-debug/src/app/openbf2 --level Dalian_plant --focus -40/180/-200 --dist 220
```

## What is still missing

- Light maps (`Lightmaps/`) and detail maps — we read only the colour map,
  so the terrain is flat in lighting terms.
- Vegetation: `Overgrowth/` and `Undergrowth` — separate formats.
- Roads (`CompiledRoads.con`) — a mesh format of their own.
- Culling: right now all 2.35 M triangles are drawn every frame.
