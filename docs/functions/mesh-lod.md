# Mesh LODs: the quality skip and the distance switch

Which LOD of a mesh `RendDX9.dll` draws, in two steps: some leading LODs
are dropped at load by the geometry quality, and the rest are switched
each frame by distance. Code: `app::skippedLods` (scene_build.h),
`game::loadMesh` (object_mesh.cpp), `game::lodSwitchDistances` /
`game::pickLod` (lod_switch.h), `app::placeObjects` (scene_build.cpp),
and `WorldView::worldObjectMesh` (world_view.cpp).

## The skip at load

| where | what |
|---|---|
| 0x10119a80 | StaticMeshTemplate LOD load. Per geometry, skip = renderer+0x7e4 + max(0, renderer+0x7ec + template+0x2b8). The skipped LODs are read and thrown away. |
| 0x1000fb50 | The geometry quality sets +0x7e4/+0x7ec to 1/0 at low, 0/0 at medium, 0/−1 at high. |
| template+0x2b8 | `GeometryTemplate.meshDetailLevel`: 0 from the ctor (0x1011a430), and no game file sets it. |
| template+0xc0 / +0xc4 | `maxSkip3pLods` / `maxSkipWreckLods`, both 1 from the GeometryTemplate ctor (0x10192960). Their saver (0x101921e0) writes them only when not 1. 158 statics set `maxSkip3pLods 0`. |
| static cap | geometry 0 by +0xc0, geometry 1 by +0xc4, any other geometry 0 (0x10119a80) |
| bundled cap | 0x100ea5f0 takes +0x7e4 only. With three geometries it caps 1 by +0xc0 and 2 by +0xc4; with two, it caps 1 by +0xc0 (0x100ea65d–0x100ea67e). |
| skinned cap | 0x10110f40 takes +0x7e4 only, capped by +0xc0 for geometry 0 of one or geometry 1 of two (0x10110fae–0x10110fc4) |

So at low quality a mesh draws from its LOD 1. For example, nc_medbush01's
LOD 0 is the twig texture `leaf_ncmedbush01de.dds` and its LOD 1 is the
leafy `_le`.

## The distance switch

| where | what |
|---|---|
| 0x100fe5d0 | The static draw picks LOD i+1 for the highest i with `scale · distance[i] < factor · d − base`. |
| scale (static) | renderer+0x8b0 · +0x84c, set by 0x1000fb50: 2 · 0.5 at low, 1 · 0.8 at medium, 1 · 1 at high |
| 0x101050c0 | The bundled draw, once a frame, by the same rule |
| scale (bundled) | renderer+0x84c · +0x8b4. +0x8b4 is `globalBundleMeshLodDistanceScale` (console accessor 0x1005aeb0). It is 1.0 from the renderer ctor (0x1002dc40), as are +0x8b0 and +0x8b8, and the quality never sets it, so: 0.5, 0.8, 1. |
| distances (static) | `GeometryTemplate.setSubGeometryLodDistance` (pre-load setter 0x10119740; a new geometry fills earlier slots with 25 · (j+1)), else running 25 m steps from (skip+1) · 25 (0x10119a80). Quirk: slot i reads the given value at skip + i, while the running total adds the given value at i. |
| distances (bundled) | The same rule, in 50 m steps from (skip+1) · 50 (0x100ea5f0) |
| base | The first loaded LOD's box radius about its centre: 0x100fc630 copies LOD +0x20, which 0x101c6570 computes. |
| d | Static: the object's distance from the camera. The bundled draw's own distance (+0x264) is filled elsewhere and not traced; the port uses the origin distance for both. |
| factor | camera +0x20 (vtable 0x1021c968, slot +0x2c = 0x101c2ca0) |
| the skinned switch | not traced; skinned meshes keep one LOD |

### The FOV factor and its base

setFov (0x101c2bd0, vtable slot +0x1c) sets the factor to
tan(fov/2) / tan(base/2). The base is process-wide: global 0x10267d08, set
from the first setFov call while the byte 0x1023b400 is still 1, its
initial value. The camera ctor (0x101c3260) starts at fov 60°
(0x1023b418 · 60, with 0x1023b418 = π/180) and factor 1.0, and does not
call setFov itself. The first call comes from BF2.exe, and its value is not
measured. The port takes its own first frame's field of view.

### Force LOD 0 and the pin

* `noLods` is StaticMeshRenderer+0x94: a console bool (get/set
  0x100bafb2), 0 from the ctor (0x101226b0). When it is on, the static draw
  calls setLod(0) every frame unless the object's LOD is pinned.
* setLod is 0x100fcb30, slot +0x78 of vtable 0x1021b158. An index ≥ 0 pins
  it (object byte +0x357 = 1); −1 clears the pin and returns to LOD 0.
* The draw's own switch calls setLod and clears the pin at once, so a pin
  only stands when something outside the draw set it.

Both are debug paths, and the port leaves them out.

## Light maps per LOD

Every LOD has its own baked light map. The atlas key is
`=<geometry·10 + absolute LOD>=` (0x10198460; 0x100fc630 adds the template
skip at +0x74). `maxLightMappedLod` is StaticMeshRenderer+0x98 = 9 (ctor
0x101226b0, read at 0x100fb550). Dalian's atlas has =00= ×367, =01= ×353,
=02= ×237 and =03= ×37.

## Road statics

A mesh under `objects/roads/` is flagged at load (+0x29c, set at
0x1011af67). The static draw returns early for it unless renderer+0x6a4 is
set (0x100fe618), and 0x1000fb50 sets that byte from the terrain quality:
0 at low, 1 at medium and high. Code: `mesh::markRoadMesh`,
`MeshRenderer::setTerrainQuality`.

## Measured

* Dalian at high, from 115/175.7/−80, yaw 200: 122 templates have more
  than one LOD. Against forced LOD 0, 1977 edge pixels differ once each LOD
  takes its own light map (20 049 with LOD 0's).
* Fushe Pass, live, profile at medium, 4000 frames: 172 static templates
  have chains, every vehicle 4 LODs, and the `world model LODs drawn`
  report reads 0 / 3853 / 15698 / 44385 for LODs 0–3.
* No frame of the original switching a vehicle's LOD has been taken to
  compare against.
