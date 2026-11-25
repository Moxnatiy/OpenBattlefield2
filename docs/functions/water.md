# Water surface

Status: **ported** (3D path). `water_fragment` in `src/gfx/src/mesh_renderer.cpp`
is `RaShaderWaterBase.fx` as `Water3D`/`WaterHighEnd3D` compile it; the geometry is
`level::buildWater`; the constants come from `Water.con` and `RendDX9.dll` as
below. The invented branch it replaced (tilings 0.085/0.161, ripple 0.18, glint
88) is gone.

## The original shader (`Shaders_client.zip:RaShaderWaterBase.fx`)

`RaShaderWater3D.fx` defines `USE_SPECULAR`, `USE_SHADOWS`, `PIXEL_CAMSPACE`,
`USE_3DTEXTURE`, `PS_20`, then includes the base. `USE_FRESNEL` is commented out
in every variant read so far.

Vertex (`waterVertexShader`):

| output | value |
|---|---|
| `Tex.xy` (3D) | `wPos.xz / (29.13, 31.81) + WaterScroll.xy * WaterCycleTime` |
| `Tex.z` (3D) | `WaterCycleTime * 10 + Tex.x * 0.7 + Tex.y * 1.13` |
| `Tex.xy` (2D) | `wPos.xz / (99.13, 71.81)` |
| `lmtex` | `lmtex * LightMapOffset.xy + LightMapOffset.zw` (vertex element `TLightMap2D`) |
| `Position` | `wPos` with `PIXEL_CAMSPACE`, else `-(WorldSpaceCamPos - wPos) * 0.02` |

Pixel (`Water`):

* `TN` = the 3D `WaterMap` sample (2D: `lerp(WaterMapFrame0, WaterMapFrame1, WaterCycleTime)`), then `TN * 2 - 1`.
* `lookup` = `-(WorldSpaceCamPos - Position)` (camera space); `reflection = reflect(lookup, TN)`; `envcol = texCUBE(CubeMap, reflection)`.
* `specular = pow(saturate(dot(-Lights[0].dir, normalize(reflection))), SpecularPower) * SpecularColor.a`.
* `shadFac = lightmap.g` (times the shadow map with `USE_SHADOWS`); `lerpMod = -(1 - saturate(shadFac + 0.75))`.
* `rgb = specular * SpecularColor * shadFac + lerp(WaterColor, envcol, 0.4 + lerpMod)`.
* `a = lightmap.r + WaterColor.w` (with `USE_FRESNEL`: `lightmap.r * fresnel + WaterColor.w`).

The transparency therefore comes from the **lightmap's red channel**, which is
why shallows are see-through in the original and opaque in ours.

## Level data (`Levels/<name>/Water.con`, Gulf of Oman)

```
renderer.waterScroll -0.5/-0.5
renderer.waterAnimSpeed 75
renderer.waterColor 0.129412/0.14902/0.090196
renderer.waterSpecularColor 0.901961/0.886275/0.8/1.5
renderer.waterSpecularPower 50
renderer.waterFogColor 51.50/51.50/128.00
renderer.waterFogStartEndAndBase 10.00/250.00/1.00/0.50
terrain.waterSunIntensity 0.8
```

We read only `renderer.waterColor` (`src/level/src/level.cpp:386`). The level
also ships `Water/EnvMap.dds`: a 128×128 DXT1 cube map, all six faces (caps2
0xfe00).

## RendDX9.dll (headless project `ghidra_projects/headless/RendDX9`)

The water renderer is one object, built at 0x100df7c0. Fields and defaults:

| field | what | default |
|---|---|---|
| +0x6c4 | `renderer.waterAnimSpeed` (getter vtable+0x90, 0x100e2f40) | 10.0 |
| +0x6c8/+0x6cc | `renderer.waterScroll` (getter vtable+0x88, 0x100e3040) | 0.1/0.1 |
| +0x6d0 | `renderer.waterNormalMap` | `"watervolume"` |
| +0x704 | `waterQuality` | |
| +0x70c | near/distant switch distance, metres | 40.0 |
| +0x714/+0x718/+0x71c/+0x720 | near, distant, far, surrounding techniques | |
| +0x728 | 3D path on | 0 |
| +0x72c | margin: skip a patch whose lowest point is above water + this | 0 |
| +0x738 | `waterAlphaHeightMul` (registered 0x100b643b, touched at 0x100b6513) | 0 |
| +0x73c | `waterAlphaAdd` (registered 0x100b653f, touched at 0x100b6617) | 0.32 (0x3ea3d70a) |

**Variant per `waterQuality`** (0x100da870): 0/1 → `Water2D`, `WaterDistant2D`,
`WaterSurrounding2D`, +0x728 = 0; 2 → `Water3D`, `WaterDistant3D`,
`WaterSurrounding3D`, +0x728 = 1; 3/4 → `WaterHighEnd3D` near, the rest as 2.
The 3D path loads `common/textures/` + waterNormalMap (`watervolume.dds`) into the
global `WaterMap`. The cube map: `<level>/Water/envmap.dds`, else
`<level>/Envmaps/EnvMap0.dds`. `HighEnd3D` differs from `Water3D` only by
`USE_HEIGHTALPHA`, which `RaShaderWaterBase.fx` never reads; `Distant3D` drops
`USE_SHADOWS`. So for the code they are one formula.

**Global parameter table** (0x10039940): each entry is a slot whose value sits 4
bytes after it in the global block `DAT_1023ddbc`: `WaterScroll` 0x184 (value
+0x188, vec4), `WaterCycleTime` 0x198 (+0x19c), `WaterMap` 0x1a0 (+0x1a4),
`WaterMapFrame0/1` 0x1a8/0x1b0, `WaterHeight` 0x1b8 (+0x1bc), `WaterColor` 0x1c0
(+0x1c4..+0x1d0), `CameraToWater` 0x1d4, `WaterClipDir` 0x1dc,
`WaterTransparency` 0x1e4, `WaterReflectivity` 0x1ec.

**Who writes them** — 0x100dc130, once a frame. `t` is the global clock
(`DAT_1023ddbc`+0x94), the sum of the frame times the renderer is handed
(0x10039120, from 0x1002c2e0):

* 3D: `WaterCycleTime = t/animSpeed − round(t/animSpeed)` (round to nearest, x87).
* 2D: the frame pair advances at `t / (animSpeed · 0.2)` (0x101ddf30 = 0.2); not ported.
* `WaterScroll = (scroll.x, scroll.y, 0.3, 0.5)` (0x10239208, 0x10239204).
* `WaterHeight` = the cluster's water level; `CameraToWater` = eye y − that.

And 0x100d9c30 makes `WaterColor.w`:
`((20 − eye y) + WaterHeight) · 0.05 · waterAlphaHeightMul + waterAlphaAdd`
(20 = 0x1021a380, 0.05 = 0x10219fa4). With the defaults that is 0.32. No level's
Water.con sets either variable. `waterTransparency`/`waterReflectivity` are
registered but no draw path reads them in what was decompiled here.

**Geometry** (0x100dd0b0, 0x100dca60, 0x100dc490). For every patch-sized cell in
view: outside the terrain → the surrounding technique, no light map; inside →
the patch's light map (patch+0x50), unless the patch's lowest point is above the
water. The technique is the near one when the camera is within 40 m (+0x70c) of
the cell's box, else distant. The mesh is one 65×65 grid over [0,1] (step
0x3c800000 = 1/64), scaled to the cell and lifted to the water; `TLightMap2D` is
the grid's own [0,1], `LightMapOffset` = (1,1,0,0).

**The light map** is the terrain patch's `Lightmaps/txCCxRR.dds`. Checked on Gulf
of Oman: an open-sea patch (`tx00x00`) is R 255, G 200 everywhere — opaque water;
a shore patch (`tx03x04`) runs R 0..255. Red is the water's opacity, green its shade.

`Common_client.zip:Textures/watervolume.dds` is a 256×256×8 volume texture,
RGB565, 9 mips (caps2 0x200000).

## In our code

* `level::buildWater` — the per-patch grids, each range's one map its light map.
* `Water.con` → `level::Terrain` (`waterAnimSpeed`, `waterScroll`,
  `waterSpecularColor`, `waterSpecularPower`, alpha mul/add); tests in `test_level`.
* `texture::loadDds` reads volumes and whole cube maps; tests in `test_texture`.
* `MeshRenderer::setWater`, `uploadVolumeOrCube`, `water_fragment`, `waterPipeline_`.

## Not ported / not measured

* No frame dump of the original over water exists, so nothing here is measured
  against the original's pixels (rule 13). Checked only against the source.
* `USE_SHADOWS`: the shadow map is not multiplied into `shadFac`.
* The 2D variants (`waterQuality` 0/1) and the surrounding cells beyond the terrain.
* Our clock is `SDL_GetTicks` seconds wrapped at 1000 s (`src/app/src/game.cpp`),
  not the original's summed frame time.
* The `alpharef = 1` test: D3D's alpha func for it is not read, so no test is applied.
* Underwater fog (`waterFogColor`, `waterFogStartEndAndBase`) is not handled here.
* The time unit of the frame delta is taken as seconds; it is not traced into `BF2.exe`.

## Water meshes (`objects/water/`)

Status: **ported** (`obf2::mesh::markWaterMesh`, `MeshRenderer` `waterMesh`
ranges). Measure: FuShe Pass,
`openbf2 --qa-direct-connect --level FuShe_Pass --map-capture 0.05,0.35,0.35,0.65`
shows a river of water below the dam, not pale triangles (checked 2026-11-22).

Levels place these as `SimpleObject`s from `Objects_client.zip:water/`:
`fushewplane_high/_mid/_low`, `poolwater_big` (FuShe Pass), `dragon_waterplane`
(Dragon Valley), `waterplane_128`. They are flat (bbox Y 0..0), collision
`mapMaterial 0 water`, and their material is a placeholder
(`objects/water/textures/watertemp.dds`, `StaticMesh.fx / Base`). Each placement
has an entry in the level's object light-map atlas (`Lightmaps/Objects/LightmapAtlas.tai`)
even though the tweak says `doNotGenerateLightmaps 1`. The light map is laid out on
the mesh's last UV set (TEXCOORD1): red is the opacity (0 outside the river's
outline), green the light.

`RendDX9.dll` (retail 1.50, `retail-vm-qa/.../runtime/RendDX9.dll`):

| address | what |
|---|---|
| 0x1011aeea..0x1011af9d | `StaticMeshTemplate` load: flags from substrings of the mesh path — `vegitation` -> +0x29e, `objects/water/` -> +0x2a4, `objects/roads/` -> +0x29c, then `noBlend` when +0x29c is set |
| 0x100fed5d..0x100fed87 | static-mesh draw: if template +0x2a4 and the water object `[0x1024b930]` exists and the instance has a light map (+0x26c), call 0x100fbab0 and skip the normal draw; otherwise fall through to the road test (+0x29c) and the ordinary draw |
| 0x100fbab0 | water draw of a mesh: binds the instance light map (+0x26c) and its offset (+0x270 count, +0x274..+0x27c), picks the effect `water+0x718` or `water+0x71c` by comparing `instance+0x288 - x+0x1c` with `water+0x70c`, binds `water+0x6c0`'s map (fallback `[0x1024b914]+0x248`) unless the item has its own (+0x14c), submits to `[0x1023e408]+0x28` |
| 0x100fd29f, 0x100fe40a | the same test in two other draw paths (not read) |
| 0x100abd07 -> 0x100ad935 | the flag in a per-instance height/lighting step (not read) |

Ported: the flag, the light-map condition, drawing with `water_fragment` in the
blended pass with the instance's atlas window. Not ported: the choice between the
two water effects (+0x718 / +0x71c, likely near and distant), the +0x14c texture
override, the paths at 0x100fd29f / 0x100fe40a / 0x100abd07. Not compared with a
retail frame: Dragon Valley's plane leaves straight-edged triangles of riverbed
where its baked red is 0; that follows from the data, not from a measurement.
