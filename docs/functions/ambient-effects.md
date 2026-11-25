# Ambient effects placed by the level

Status: **sprite and mesh particle systems ported (both mesh branches, type-1
spawn and integrator, Box emitter, lighting); bundle sound played with an
undecoded falloff; distance fade not ported**.
Code: `obf2::app::AmbientEffects` (`src/app/src/ambient_effects.cpp`), loaded in
`src/app/main.cpp` from the level's placements and drawn in the frame loop
whether or not a server is connected.

Measure: FuShe Pass, looking at the dam,

    openbf2 --level FuShe_Pass --no-hud --width 800 --height 600 \
      --frames 120 --focus -196/82/160 --dist 45 --screenshot out.bmp

shows the spray falling from the dam's outlets and over the pool, where
without the effects the rock is bare. The log prints `ambient effects: 13
placements, 28 emitters (5 mesh), 10 sounds`. Not compared with
a retail frame (rule 13): no frame dump of the original at the dam exists.

## What the data says

`Levels/FuShe_Pass/server.zip:StaticObjects.con` places
`ambStat_Damn_waterFall` and `amdStat_Damn_WaterImpact` like any static object
(`Object.create`, `absolutePosition`, `rotation`). Each is a `Bundle`
(`Objects_client.zip:staticobjects/ambienteffects/static/*.tweak`) with one
child, an `EffectBundle` (`Effects/ambient/StaticFX/e_sAmb_Damn_*.tweak`):

| child of `e_sAmb_damn_waterfall` | class | state here |
|---|---|---|
| `S_e_sAmb_damn_waterfall_Start` | `Sound`, `waterfall.wav`, `loopCount 0`, min 10 / half 20 m | looped at the placement |
| `em_sAmb_damn_waterfall_detailedSpray` | `SpriteParticleSystem`, `emitterType Box` | drawn, Box shape |
| `em_sAmb_damn_waterfall_spray` | `SpriteParticleSystem` | drawn |
| `em_sAmb_damn_waterfall_fallingsmoke` | `SpriteParticleSystem` | drawn |
| `em_sAmb_damn_waterfall_mp_waterfall` | `MeshParticleSystem`, geometry `mp_waterfall`, `isStaticSystem 1`, `directionalScale 0.25`, `writeZ 0` | drawn (below) |

The bundle has `timeToLive CRD_NONE/-1/0/0` and `startAtCreation 1`, and every
system has `emitTime 0`, so `SpriteEmitter` never counts its emission down: the
effect runs for as long as the level is loaded.

`mp_waterfall.bundledmesh` (`Effects/geometry/meshparticles/meshes/`) is 26
geometry parts of 98 vertices each (`mesh_info`), about 1 x 0.1 x 1 m,
`BundledMesh.fx`, texture `mp_waterfall.dds`.

## Mesh particles (`MeshParticleSystem`), RendDX9.dll

Retail 1.50's `RendDX9.dll`, in a headless Ghidra project.

**The 26 parts are instance slots, not shapes.** `Shaders_client.zip:
MeshParticleMesh.fx` skins each vertex by its `BLENDINDICES[0]` into
`mOneBoneSkinning[26]` and reads `ageAndAlphaArray[26]` the same way; the
draw (0x10174740) fills slot n with particle n's matrix (record +4, set
through the effect's +0x4c) and `(age, alpha)` (record +0x5c, +0x6c, via
0x101b00f0), and draws the batch with 0x101b02a0 when the slots run out. So
every particle shows the same piece; we draw part 0 (vertices 0..97 of
`mp_waterfall`) once per particle.

0x101b02a0 picks the technique from 0x10174740: template +0x90 == 1 gives
`Additive`, else template +0x1f2 set gives `DiffuseWithZWrite`, else `Diffuse`
(alpha blend, no Z write, CullMode CCW). `blendMode` sets +0x90 (BF2.exe
descriptor vtable 0x8d43b8, forwarder 0x56a17a → BF2 template +0x124
0x64c9d0, which looks the string up in 0x976ed4 {AlphaBlend, Additive} and
passes the index to renderer +0x1e0, 0x101792c0). `writeZ` sets +0x1f2
(descriptor 0x8d44d8, forwarder 0x56a4b5 → 0x64cc10 → renderer +0x1d8,
0x1017a1b0). `meshLook` maps them; `Additive` is drawn as the renderer's
additive blend. `DiffuseWithZWrite` is drawn as `Diffuse`: the renderer's
blended ranges have no depth write. `MeshParticleMesh.fx` vertex shader:
position × `GlobalScale` × bone matrix; colour = lerp(color1, color2,
min(colorBlendGraph(age), 1)); alpha = `ageAndAlphaArray[n].y`.

`GlobalScale` is MeshParticleMesh +0x5c..+0x68, which the constructor
0x101b0700 sets to (1, 1, 1, 1); 0x101b02a0 only uploads it. So it is 1,
unless one of the other +0x5c stores in the 0x101b… range (0x101b2ad7,
0x101b75db, 0x101bd850, 0x101bdd86, not traced to a class) belongs to this
class.

### Lighting (`calcParticleLighting`)

`psDiffuse` (both Diffuse techniques; not `psAdditive`):
rgb ×= lerp(1, lerp(EffectShadowColor, EffectSunColor, saturate(lut.a +
o)), lightFactor) (`FXCommon.fx`), with lut the hemi map at the particle and
o = saturate(clamp((y − HemiShadowAltitude)/10, 0, 1) + LightmapIntensityOffset).

* `LightmapIntensityOffset`: 0x10174740 uploads the template's virtual +0x1b4.
  For the mesh template that is 0x101793b0, returning [0x101e14d0] = −1; its
  setter +0x1b0 (0x101793a0) is empty. So the data's
  `lightMapIntensityOffset` is ignored, and o = saturate(clamp(…) − 1) = 0.
* `lightFactor` is `m_color1AndLightFactor.a`, template +0x1b8 (the vector at
  +0x1ac). 0x101794a0 (vtable +8) sets it to +0x1a8 when that is ≥ 0, else to
  light manager [0x1024b914] +0x270. +0x1a8 is `lightAffectionFactor`
  (descriptor 0x8d5b28, forwarder 0x56d2c6 → 0x64f650 → renderer +0x18c,
  0x10179410). The +0x270 getter sits beside the manager's EffectSunColor /
  EffectShadowColor getters (+0x260/+0x268, m4-effects.md), and is taken as
  `Lightmanager.defaultEffectLightAffectionFactor`, which the level reads.
* The hemi map is not sampled; its
  alpha is taken as 1 (sunlit). This is where the port is not exact.

The factor is constant per template and multiplies `particleColor`.
FuShe Pass sets EffectSunColor = EffectShadowColor = 1/1/1, so it is 1 there
whatever the hemi map says, and the dam frame does not change.

Sprites: the port draws Particles.fx's Low technique (`psParticleLow`), which
has no `calcParticleLighting` (sprite-particles.md). Which of
Low/Medium/High retail picks is a quality setting, not established. So
sprite lighting is not added.

Template is type 1 (`ParticleSystemManager` 0x100d5280; vtable 0x1021f898).
Field slots, from the BF2.exe descriptor's +0x5c forwarder to the renderer
template's virtual:

| field | BF2.exe descriptor / forwarder | renderer slot | setter | offset |
|---|---|---|---|---|
| directionalScale | 0x8d64f0 / 0x56e77d → 0x64f8f0 | 0x1b8 | 0x1017a160 | +0x1ec f32 |
| alignRotationToSpeed | 0x8d6588 / 0x56e892 → 0x64f110 | 0xa4 | 0x1017a140 | +0x1f0 u8 |

Per particle per frame, 0x10175ac0 (called from 0x10174c00), record `r`,
template `t`, age `a` = r+0x5c:

1. If `alignRotationToSpeed` is 0 and `directionalScale` is 0: angles
   r+0xa8..+0xb0 += clamp01(graph t+0xfc..+0x108 (a)) × frame time
   (manager +0x1c20) × r+0x9c..+0xa4; matrix = 0x10078e20(−z, −y, −x),
   translation r+0x50.
2. Otherwise, v = r+0x84..+0x8c, s = |v|. If s < [0x1021f0a8] = 1.1920929e-7:
   identity rotation at r+0x50. Else D = v/s; if |D.x| or |D.z| ≥
   [0x1021f0e4] = 1e-4:
   X = normalize(up × D), up = (0,1,0) (0x1007b720 is `out = a × b`,
   a in EDX, b pushed; 0x10175d3e), Y = D × X (0x10175d66). Straight up or
   down: X = (1,0,0), D = (0,∓1,0), Y = (0,0,±1) for D.y ≤ 0 / > 0.
   Rows X, Y, D, position (0x101745a0). With `directionalScale` > 0 the
   record is diag(1, 1, s × directionalScale) × that basis (0x10027c40,
   0x10077fa0, 0x10175900, 0x10079bc0).
3. Size: clamp01(sizeGraph(a)) × `particleMaxSize` (t+0x1e8) + r+0x68,
   applied to all three axes (0x10077fa0 on the record).
4. Alpha: r+0x6c ×= clamp01(transparencyGraph(a)) (t+0x13c..+0x148).

Branch 1 runs only when the record's "moved" bit (r+0x60 bit 7, set by the
integrator) is set, and clears it. The angle step is clamp01(rotationGraph at
t+0xfc (ageFactor)) × manager +0x1c20 × r+0x9c..+0xa4. 0x10078e20(p2, p3, p4)
is called with p2 = −r+0xb0, p3 = −r+0xac, p4 = −r+0xa8 (stack at
0x10175b9d..0x10175bbd). It converts each from degrees (× [0x10238738]) and
builds A = Rz(p2) = [[c,−s,0],[s,c,0],[0,0,1]], B = Rx(p3) =
[[1,0,0],[0,c,−s],[0,s,c]], C = Ry(p4) = [[c,0,s],[0,1,0],[−s,0,c]].
It returns A·(B·C); 0x1007c9b0 is this·arg (out[i][j] = Σ this[i][k]·arg[k][j]),
called B.mul(C), then A.mul(that). The rows go to the record as in branch 2,
and steps 3–4 follow. Ported in `drawMesh`, but not seen on screen: no
level-placed system takes this branch (the only other ambient mesh system,
`em_dAmb_frogjump_mp_frog`, has `alignRotationToSpeed 1`).

### Type-1 spawn (0x101850e0) and integrator (0x10184450)

Mesh systems run `SpriteEmitter` with `meshType`, which switches to these two.

Spawn, per particle, in draw order: ttl = unit × t+0x98 + t+0x9c; r =
symmetric (2⁻²³ at 0x10220030), size term = r × t+0x16c; intensity term =
symmetric × t+0x170. One draw s gives the start angles r+0xa8..+0xb0 =
s × t+0x174..+0x17c (`randomRotation`; setter 0x1017a3c0 stores the degrees
raw). One draw s2 gives the spins r+0x9c..+0xa4 = s2 × t+0xac..+0xb4
± t+0xb8..+0xc0 (minus when s2 < 0). Then the shape by t+0x214
(0 → 0x10186370, 1/2 → 0x101864a0, 3 → 0x10186690). Then three symmetric
draws turn the direction by X, Y, Z (× t+0x184..+0x18c). Speed =
clamp01(emitSpeedGraph t+0x15c (1 − remaining/emitTime)) × (t+0x190 −
r × t+0x180) × instance +0x138. With `isLocalSystem 0` the position goes
through the emitter's matrix (0x10079d00) and the direction through
0x100a6960. The velocity then gets instance +0x1c..+0x24 added. The particle
is caught up as for type 0: round(Δ × 100) steps of 0.01 (0x10220050,
0x1023a4e8), then the remainder.

The field names on t+0x16c..+0x18c and +0xac..+0xc0 are matched by layout
and by the vector type of the three rotation fields. Only +0x174 (setter
0x1017a3c0) was traced to its setter.

Integrator: die when ttl ≤ steps × step + age. Drag k = clamp01(airResistanceGraph
t+0x11c (ageFactor at entry)) × t+0xa8 × step. Then, per step: age += step,
ageFactor = age × (1/ttl); g = clamp01(gravityGraph t+0x10c (ageFactor)) ×
step × t+0xa4. rel = v − wind × t+0xcc (0 here: no wind manager), normalized in
place by 0x10077e90 (zeroed when |rel|² < [0x10208958]), dv = −rel̂ × |rel|² × k
+ (0, g, 0), pos += (v + dv × 0.5) × step, v += dv. Then collision (t+0xec),
the r+0x50 copy, and `forceToWaterSurface` (t+0xd0, + t+0xd4).

Compared with the type-0 port, line by line, two differences:
* gravity's graph is read at each step's own ageFactor (type 0 reads it once);
* no velocity component is zeroed afterwards (type 0 zeroes below 0.001).

Both are in `integrateMesh`. The spawn's draw order differs from type 0's (no
start-frame draw, vector angles and spins), and that is in `spawnMesh`. Wind,
collision and the water surface are not ported, as for type 0. Instance
+0x138 and +0x1c..+0x24 are taken as 1 and 0: their writers were not traced.

Emission interval: type 1 uses 1 / (t+0x10 × max(inst+0x12c, t+0xe4) ×
max(inst+0x130, t+0xe4)), times t+0x1a0 when manager +0x7b48 is set. +0x1a0 is
0.5 / 0.7 / 1 by `lodDistance` index +0x64, from 0x101794a0. The instance
factors and the manager flag are not established. The port keeps type 0's
1 / max(freq, minimum rate).

### Emitter shapes

`emitterType` is an index into BF2.exe 0x976ec4 {Standard, RadialDirection,
Radial, Box}; type 0 stores it at +0x264, type 1 at +0x214.

* 0 Standard: 0x101813d0 / 0x10186370, the existing shape-0 formula.
* 3 Box: 0x101816f0 / 0x10186690. The offset is (CRD X, CRD Y, CRD Z) from
  `emitRangeX/Y/Z` (type 0 +0x24/+0x38/+0x4c, type 1 +0x20/+0x34/+0x48),
  sampled Z first, then Y, then X; `emitRadius` is not used.
* 1, 2 Radial: 0x10181500, not ported; emitted as 0.

CRD sampling, 0x1017a680: with the mirror flag (+0xc), a unit draw ≤ 0.5
([0x1020d7d0]) flips the sign. Then 0x1017a620 by type: 0 → a; 1 → a + (b −
a) × unit (0x1009f8f0); 2 → −ln(unit) × a (0x1017a530); 3 → a + b × normal
(0x1017a560, polar Box–Muller with the spare kept in globals
[0x102619a0]/[0x102619a4]; we keep it per generator). Mapping CRD_NONE /
UNIFORM / EXPONENTIAL / NORMAL to 0..3 is read from that arithmetic, not from
the BF2.exe parser.

Both types turn the offset by the emitter's matrix when `isLocalSystem 0`
(0x10079d00 in 0x101801c0 and 0x101850e0). `SpriteTemplate::emitterAxes`
carries the placement's rotation for that. Before this, the port added the
offset unturned.

### Distance fade (0x10174c00), not ported

Per particle: d = dot(position, camera plane manager +0x1c24..+0x1c30). A
particle is kept when d ≥ the camera's near value (virtual +0x34). Its
alpha r+0x6c = r+0x70 when d ≤ manager +0x8bec[i], else
(1 − min((d − +0x8bec[i]) × +0x8be0[i], 1))² × r+0x70. It is drawn when
alpha ≥ alphaCull (+0xc4). i is the template's `lodDistance` index (+0x68
on the system). 0x100d39e0: lod[i] = settings +0x8bac[i] × view distance
(+36000), start[i] = +0x8bf8 (default 0.85) × lod[i], +0x8be0[i] =
1/(lod[i] − start[i]). View distance (0x100d3d20) = max([0x102197dc], camera
factor × `GSViewDistance` (default 500) × settings[0]). The per-quality
settings struct that fills +0x8bac.. is passed in from outside. Its values
are not established, so there are no distances to fade by.
`nearCullingEnable` (descriptor 0x8d3520, forwarder 0x568213 → 0x64c260)
calls renderer +0x270. For the mesh template that slot is 0x101c4bce,
outside the class's own functions: not established.
## Placement

The world matrix is the one `placeObjects` gives a building: translation,
then `rotationYawPitchRoll` of the placement's angles, times the part's
accumulated `setPosition`/`setRotation` (`game::flattenObject`). With
`isLocalSystem 0` the particles live in the world, so each emitter's
`emitDirection` is turned once by that matrix.

## Not ported

* `isStaticSystem` (descriptor 0x8d3488, forwarder 0x5680e8 → BF2 template
  +0x11c 0x64f940 → renderer +0x1c0 0x1017a220) stores a byte at template
  +0x218. Its only reader is the getter +0x1c4 (0x1017a230), and the
  `call *0x1c4` sites in RendDX9.dll call other classes. Within the renderer
  it has no effect found. Not established beyond that store.
* Distance fade and near culling: decoded above, but not ported. The fade
  distances come from a per-quality settings struct whose values are not
  established.
* `emitterType` RadialDirection/Radial (0x10181500).
* The hemi map's alpha in mesh-particle lighting (taken as 1); `DiffuseWithZWrite`'s
  depth write.
* Sprite lighting: the Low technique the port draws has none (see Lighting).
* Type-1 emission rate factors (instance +0x12c/+0x130, t+0xe4, t+0x1a0
  gate): not established, so type 0's interval is used.
* Sound: BF2Audio.dll's distance falloff, pan, `pitchEnvelope` and
  `reverbLevel` are not decoded. The loop plays mono with SmokeFlare's
  falloff (full to `minDistance`, halving every `halfVolumeDistance` −
  `minDistance` after it).
