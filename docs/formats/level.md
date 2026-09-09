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

## The main decision: take the editor branch

A level's `Init.con` has two branches:

```
if v_arg1 == BF2Editor
  run Heightdata.con          ← the source .raw, format described by the data
  ...
else
  terrain.load Levels/<name>/terraindata.raw   ← a compiled blob
endIf
```

The game branch reads the **compiled** `terraindata.raw`, which would need
reversing to parse. The editor branch reads the source height maps, whose
format is fully determined by `Heightdata.con` itself. So the engine runs
the level's `.con` with the argument `BF2Editor` — and gets the same data
with no reverse engineering at all.

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
