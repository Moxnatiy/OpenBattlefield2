# The video settings: ten switches, and what each one really changes

The options screen has ten controls. In the original every one of them
changes **what the renderer compiles and what it draws** — not a flag read
at draw time but, for most of them, a `#define` handed to the shader
compiler at start-up. `RendDX9.dll` builds that text itself; the strings
are in it whole:

```
#define RAPATH %i          #define NVIDIA 0 / 1        #define ATI 0 / 1
#define SM20 0 / 1         #define SM30 0 / 1          #define NUM_LIGHTS 1
#define HASALPHA2MASK %i   HIGHTERRAIN   MIDTERRAIN    DEBUGTERRAIN
#define FILTER_STM_DIFF_MIN %s     #define FILTER_STM_DIFF_MAX_ANISOTROPY %i
#define FILTER_STM_NORM_MIP LINEAR / POINT
#define FILTER_BM_DIFF_MIN %s      #define FILTER_BM_DIFF_MAX_ANISOTROPY %i
```

and the shaders read them: `TerrainShader.fx` branches on `HIGHTERRAIN` and
`MIDTERRAIN`, `RaShaderSTM.fx` on `RAPATH` and `_FORCE_1_4_SHADERS_`,
`RaCommon.fx` on `PSVERSION`, and every sampler declaration on a `FILTER_*`.
So the settings are not decoration: they pick the code path we are porting,
and a port that ignores them is a port of one arbitrary quality level.

The commands are `VideoSettings.*` and they live in the player's profile
(`Game Files/Profiles/<name>/Video.con`). All ten are registered in
`BF2.exe` — `setTerrainQuality` at `FUN_00413d29` (0x00413d29), an `int`
argument, and its siblings beside it:

| control | command | argument |
|---|---|---|
| Terrain | `VideoSettings.setTerrainQuality` | int |
| Effects | `VideoSettings.setEffectsQuality` | int |
| Geometry | `VideoSettings.setGeometryQuality` | int |
| Texture | `VideoSettings.setTextureQuality` | int |
| Lighting | `VideoSettings.setLightingQuality` | int |
| Dynamic Shadows | `VideoSettings.setDynamicShadowsQuality` | int |
| Dynamic Lights | `VideoSettings.setDynamicLightingQuality` | int |
| Anti-aliasing | `VideoSettings.setAntialiasing` | int |
| Texture filtering | `VideoSettings.setTextureFilteringQuality` | int |
| View distance | `VideoSettings.setViewDistanceScale` | float |

**Stage 0, and it was a bug, not a stage.** `obf2::engine::Settings` bound
these to `renderer.set*Quality`, a name that is in neither binary — what
`BF2.exe` registers is `VideoSettings.*`. The older `game.set*` aliases,
bound further down, were carrying five of them by accident and the rest sat
at their defaults; two had no field at all, texture filtering and the
view-distance scale.

**Done.** Both spellings are bound, the two missing fields exist, and
`openbf2` prints all ten at start-up:

```
quality: terrain 2, effects 3, geometry 3, texture 2, lighting 2
         dynamic shadows 1, dynamic lights 2, antialiasing 0, filtering 2, view distance 1.00
```

Which spelling the game itself writes when the player saves the options is
not established — every profile on this machine is one we wrote.

## Where each one goes, and in what order

The order is by what is visible on screen now, not by the order in the
menu.

### 1. Lighting

What it does in the original is not established. The candidates are all in
reach: `RAPATH` (which of `RaShaderSTM`'s three paths is compiled),
`PSVERSION` (per-pixel against per-vertex lighting, and with it whether
`CEXP` doubles the constants), and the specular term `_USESPECULAR_`.

We draw the vertex path: no tangent frame, no normal maps, no specular.
That is the low end of the setting, and it is what our own picture should
be compared against until the paths are told apart.

Known items on this side, in the order they cost us picture:

* **Specular — done.** `RaShaderSTM.fx:394`, with `compNormals = (0,0,1)`,
  which is the tangent frame's own z: the frame is orthonormal, so the dot
  product is the same taken against the vertex normal in world coordinates
  and this path needs no tangent frame at all.

  ```
  halfVec  = normalize(toSun + toEye)
  specular = pow(saturate(dot(N, halfVec)), 32) * lightmap.g * gloss
  colour  += specular * StaticSpecularColor
  ```

  Both constants are measured rather than guessed: on Karkand `psc c1` is
  `0.65/0.60/0.52`, the level's own `staticSpecularColor` whole, and `psc c2`
  is `0.2` on 678 draws of one frame — `StaticGloss`. Where a material has a
  detail map and is not alpha-tested the detail's alpha replaces that gloss
  (`RaShaderSTM.fx:283`). Left: `GeometryTemplate.setSpecularStaticGloss`,
  which lets an object override the gloss per material and which we do not
  read.
* **`SinglePointColor` — done.** Uploaded by the engine, gated by the light
  map's red channel; zero on Karkand, 0.30 grey on some levels.
* **Normal maps.** Every `N*` channel of a material is parsed and unused —
  they need a tangent frame, which our `Vertex` does not carry. This is the
  big one left: it is also what turns the per-pixel path on, and with it the
  `dot(compNormals, skyNormal)` the sky term is supposed to have.
* **The hemi map.** `hemiMapManager.setBaseHemiMap` + `Lightmanager.hemilerpbias`,
  a top-down picture of the ground's colour. Not for static meshes —
  `RaShaderSTM.fx` never mentions it — but `RaShaderBM.fx` (vehicles) uses
  it 29 times, and so do the particles and the decals.

Measure: a frame of the original and a frame of ours from the same camera,
compared surface by surface rather than by eye.

### 2. Terrain

The one setting whose mechanism is already read: `HIGHTERRAIN` and
`MIDTERRAIN` pick between three techniques in `TerrainShader.fx`, and they
differ in real ways — the high path has the mountain blend and the second
detail plane, the mid path drops to `ps_1_4` and folds two lerps into one,
the low path draws the colour map with one detail texture.

We draw something between the two: the game's low-detail tri-planar
texture and the light buffer, without the near per-material detail.

Left here:

* the **near detail texture** per terrain material, which needs the
  material system — and that is in `terraindata.raw`, the compiled blob the
  game loads and we do not read (docs/research/12-renddx9.md);
* `terrain.farTopTilingHi` against `farTopTilingLow`: the engine picks by a
  flag at `+0x35e` (`RendDX9.dll`, 0x100d9c30) and that flag is this
  setting;
* the geometry itself: the game morphs a patch between LODs
  (`geoMorphPosition`), we draw one fixed grid.

Measure: the same patch, same camera, in both.

### 3. View distance

`VideoSettings.setViewDistanceScale` is a **float**, and the engine keeps
several: `ViewDistance`, `ViewDistanceFadeScale`, `ViewDistanceHeightScale`,
`ViewDistanceHeight2Scale`, `ViewDistanceStreamingScale`, `AGSViewDistance`,
and `Overgrowth.viewDistance` / `viewDistanceScale` for the undergrowth
(strings in `RendDX9.dll`).

**Half done.** The level says how far it lets anyone see, in its own
Init.con:

```
GameLogic.MaximumLevelViewDistance 140
```

and it is **not** the fog's end, which is what our far plane used to be.
The two are close on some levels and apart on others — every level in the
game, read out of its Init.con against its Sky.con:

| level | view | fog ends |
|---|---|---|
| Strike at Karkand | 140 | 135 |
| Mashtuur City | 200 | 200 |
| Gulf of Oman | 400 | **450** |
| Songhua Stalemate | 250 | **300** |
| Taraba Quarry | 575 | 535 |
| Dalian Plant | 610 | 610 |
| Operation Blue Pearl | 90 | — |

On Gulf of Oman the fog's end would draw fifty metres of world the game
never shows. The far plane is now `MaximumLevelViewDistance` times the
player's `setViewDistanceScale`, and the fog is left to do its own job.

Left: the rest of the family — `ViewDistanceFadeScale`,
`ViewDistanceHeightScale`, `ViewDistanceHeight2Scale`,
`ViewDistanceStreamingScale`, `Overgrowth.viewDistance` — and what the
slider's own range is, which is not established.

Measure: the horizon sits at the same distance in both, and the sky dome
needs no special case.

### 4. Geometry

The LOD distances: `getQualityLodDistanceSTM` / `…BM` / `…SKM` and
`setQualityLodDistanceALL` (strings in `RendDX9.dll`), plus
`renderer.globalLodRadius` and `globalLodRadiusScaleFactor`, which are in
`VideoDefault.con` and which we already parse.

We draw lod 0 of everything, always. That is the top of the setting and
also the reason a level costs us more triangles than it costs the original.

Measure: the number of triangles drawn on Karkand from a fixed camera,
against the original's own counter.

### 5. Texture

`setTextureQuality` picks which mip a texture is loaded from — the game
ships full chains and drops the top levels on the lower settings. We load
the whole chain.

Measure: the loaded texture memory at each level matches the original's.

### 6. Texture filtering

The most mechanical of the ten: it becomes the `FILTER_*` defines above,
which are pasted straight into every sampler declaration in the shaders
(`FILTER_STM_DIFF_MIN`, `FILTER_TRN_DIFF_MAX_ANISOTROPY`, and so on). Our
sampler is linear with repeat and no anisotropy.

Measure: the sampler state of a draw in our renderer matches the same draw
in a frame dump of the original.

### 7. Effects

`particleSystemShaderQuality` and the particle shaders
(`Particles.fx`, `NonScreenAlignedParticles.fx`, `PointSpriteParticles.fx`,
`MeshParticleMesh.fx`). We draw no particles at all, so this setting has
nothing to change yet.

### 8. Dynamic shadows

`dynamicShadowsEnable`, `TerrainDynamicShadow`, `ShadowMap`, and the
`_SHADOW_` literal of `RaShaderSTM.mfx` — the shadow map multiplies into
the light map's green channel (`RaShaderSTM.fx:494`). The terrain's own
version replaces the baked sun term with `min(shadow, lightmap.y)`
(`TerrainShader_Hi.fx:588`).

We have no shadow map. Everything shadowed is shadowed by the baked maps.

Measure: a soldier casts a shadow, and it lands where the original's does.

### 9. Dynamic lights

`DynamicLighting`, `POINTLIGHT`/`SPOTLIGHT` in the terrain shader, the
`_POINTLIGHT_` path of `RaShaderSTM.fx` and the second additive pass it is
drawn in. Muzzle flashes and explosions light the world with these.

We draw the directional pass only.

Measure: a light near a wall lights it, and only within its attenuation.

### 10. Anti-aliasing

`setAntialiasing` is the multisample count for the swapchain. SDL_GPU takes
it per pipeline (`SDL_GPUSampleCount`), so it is the one setting that costs
nothing but plumbing.

Measure: the edges of a mesh are smoothed, and the setting turns it off
again.

## The known defects this list has to close

* **Some objects glow and some are dark** (Dalian). Fixed: the placement's
  normals were not rotated with the placement, so a rotated building was lit
  as though it stood the way it was modelled.
* **The ground close up is smeared.** The near detail texture, above.
* **Purple smoke and deformed leaves under mtld3d** — a driver artefact,
  not ours (docs/research/03-frame-dump.md), and present on two of its
  versions.
