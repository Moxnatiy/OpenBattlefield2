# The renderer's cameras, and how a road keeps on top of the ground

Everything here is in `RendDX9.dll` (image base 0x10000000), not in `BF2.exe`:
the renderer owns the projection, and the game hands it only a view.

## The near plane is the renderer's, set every frame

The renderer object (constructor 0x1002dc40, its pointer kept in the global
0x1023d098) holds two cameras and four numbers for them:

| field | default (constructor) | what it is |
|---|---|---|
| +0xf8 | made at 0x1002e0b0 (`new`, 0x101c3260) | the world's camera; the vtable's +0x48 and +0x50 both return it (0x1000c9c0, 0x100056d0) |
| +0xfc | made at 0x1002d24f | the first-person camera — the weapon in hand; vtable +0xd4 returns it (0x1000c9d0) |
| +0x590 | `0x3d27ef9e` = **0.041** (0x1002dd16) | the world camera's near plane — console `renderer.tweakNearPlane` (0x1005a060, its setter 0x1005a1b0) |
| +0x594 | `0x3c23d70a` = **0.01** (0x1002dd20) | the first-person camera's near plane — `renderer.tweakNearPlane1p` (0x1005a200) |
| +0x598 | `0x41700000` = 15.0 (0x1002dd2a) | the first-person camera's far plane is this **minus** +0x594: 14.99 |
| +0x59c | `0x40c00000` = 6.0 (0x1002dd34) | purpose not established |

At the top of every frame (0x1002c2e0, the renderer's draw) the world camera is
given +0x590 as its near plane — twice, once through each of the two getters
(0x1002c2eb–0x1002c30e) — and the first-person camera +0x594 as its near and
+0x598 − +0x594 as its far (0x1002c315–0x1002c341). Whatever the game set
before is overwritten, so the near plane in a battle is **0.041**, unless the
console command changes it. No file of the game's data does: the archives
carry no `tweakNearPlane` at all.

The two `tweak` commands write their value into the renderer through the
global, and nothing else reads the fields that way (`tools/pe_field_users.py
"Game Files/RendDX9.dll" 0x1023d098 0x590`: the command's get and set only).

### The camera class

vtable 0x1021c968, constructor 0x101c3260:

| field | default | setter (vtable slot) | getter |
|---|---|---|---|
| +0x24 | 60° in radians (`0x10222834` 60.0 × `0x1023b418` π/180) | +0x1c, 0x101c2bd0 — also keeps +0x1c (a zoom: tan of the first field of view set over tan of this one) and +0x20 (its inverse) | +0x20, 0x101c2cb0 |
| +0x28 | 0.01 | +0x30, 0x101c2cd0 — **the near plane** | +0x34 |
| +0x2c | 1000.0 | +0x38, 0x101c2d10 — the far plane | +0x3c |
| +0x30 | 0.75 | +0x40, 0x101c2d50 — the aspect | +0x44 |
| +0x18 | 0 | +0x48, 0x101c2d90 | +0x4c |

Every setter marks the camera dirty (+0x428 = 0xf, +0x42c and +0x42d = 1).
The field of view is the **vertical** one. The camera's projection is built
lazily (0x101c2f20, while its +0x42d is set) by its frustum at +0x23c
(0x1007ab50, with the field of view, +0x30, near and **far + near** —
0x101c2f66..0x101c2f80), and the frustum's matrix is
`D3DXMatrixPerspectiveFovLH(+0x1cc, 1 / +0x1d0, +0x1dc, +0x1e0)` (0x10079e10):
that function's angle is vertical and its aspect width over height, so +0x30 is
height over width.

Slot +0x18 (0x101c2b80) sets the field of view through the aspect: at 0.5625
or less (16:9 and wider) the angle is multiplied by `aspect × 16/9`
(0x10222830, 0x1022282c), at 0.8 or more (5:4 and narrower) by `aspect × 4/3`
(0x10222828, 0x101dd260), and in between it is left alone; then `setFov`
(+0x1c) takes it. (An earlier reading here had the second test the wrong way
round: `fcomp` against 0.8 skips when C0 is set, which is below 0.8.)

### Who sets it in a battle — `BF2.exe` 0x453270

The game's frame hands the renderer its camera here:

```
demo playback (0x54b8f0):  world->+0x1c(0x44cca0()), +0x60(the recording's matrix),
                           +0x40(0x44cc90())   — the two numbers' meaning not read
otherwise, with a local player and his camera (+0x17c):
  matrix = the camera's (0x4531c0)
  world->+0x18(player->getFov())                                  0x45337f, 0x453398
  world->+0x60(matrix)
then: world->+0x38(GSViewDistance × renderer+0x74()->+0x28())    the far plane, 0x4533ae..
```

`Player::getFov` is the player's +0xe0 (Linux 0x49a4f0). `Player::setFov(fov,
time)` (Linux 0x495e00) moves it toward `fov` over `time`, and a `fov` of 0
(0xb2d910) means the camera's own: `getActiveFOV` (0x5773c0), the camera
template's +0x354 or +0x358 by its mode (+0x2cc) — `insideFOV` and `worldFOV`,
1.1 both for `SoldierCamera` (`objects/soldiers/common/soldiercamera.tweak`).
Which call sets the player's fov on foot is **not read**; the slot (+0x118) is
shared with other classes and the callers found so far were not it.

Ours: through the soldier's eye the world's field of view is his camera's
`insideFOV` through `aspectFieldOfView` (`app/camera`); anywhere else — the spawn
screen, the debugging cameras, `--hosted` — it stays 1.05, **not measured**.

The first-person camera's 0.82 (0x1002d277) and its near and far are in
docs/functions/soldier-model.md, "Stage 4".

## A road is pulled towards the eye by a depth bias

`RoadCompiledRenderer` (its effect loaded at 0x10144b90, technique
`roadcompiledFull`) draws every compiled road in 0x10144e70. Before the loop it
calls 0x10145700, which ends by setting two of the device's render states from
its own fields:

```
SetRenderState(0xc3 D3DRS_DEPTHBIAS,           this+0x90)   // 0x10145949
SetRenderState(0xaf D3DRS_SLOPESCALEDEPTHBIAS, this+0x94)
```

and the constructor puts `0xb727c5ac` = **−1e-5** in both (0x10145524). The
shader's own pass leaves them out — they are commented out in
`RoadCompiled.fx:199` — so this is the only place they come from.

Which pass draws is `BeginPass(mode == 2)` (0x10144e70 → 0x1001dd60, whose
+0x100 is `ID3DXEffect::BeginPass`). The mode is the renderer's +0x28: 3 for a
frame — it binds the terrain's lighting texture (`TEXLAYER2`, 0x10145600) and
draws pass 0, `NV3x`, the one with `ZEnable = TRUE, ZWriteEnable = FALSE`;
2 draws pass 1, `DirectX9`, with the depth test off, into a target of its own
(0x10005890 with the renderer's +0x780).

So a road in the frame is lifted a centimetre in the vertex shader
(`RoadCompiled.fx:95`), tested against the depth without writing it, **and**
biased by −1e-5 of the depth range plus −1e-5 of its slope. D3D9 defines both
in units of the depth buffer's own range: the offset is
`slope × max|∂z/∂screen| + bias`, added to z in [0, 1].

The bias is what keeps a road over a ground mesh lying on it — a pavement, a
square — and over the terrain far away, where a centimetre is less than the
depth buffer can tell apart. Its reach in metres grows with the square of the
distance and shrinks with the near plane: at `d` metres −1e-5 of depth is about
`1e-5 · d² / near` metres: 2.4 cm at 10 m, 0.61 m at 50 m with the near plane of
0.041 — and two and a half times less with a near plane of 0.1. That is why the
two belong together.

## Ours

* the near plane is 0.041 whenever a level is drawn (`src/app/main.cpp`); the
  model viewer keeps its own, proportional to the model, because it is not the
  game;
* the road pipeline carries the bias (`src/gfx/src/mesh_renderer.cpp`). SDL_GPU,
  like Metal and Vulkan, takes the constant part in units of the smallest
  resolvable depth step `r` rather than of the depth range. For a `D24` buffer
  `r` is 2⁻²⁴; for `D32_FLOAT` it is 2^(e−23) with `e` the exponent of the
  primitive's largest depth, which is 2⁻²⁴ too for every depth in [0.5, 1) —
  everything past twice the near plane. So D3D9's −1e-5 is −1e-5 · 2²⁴ ≈ −167.8
  steps; the slope factor is unitless in both APIs and stays −1e-5.

The first-person camera is drawn with since the weapon in hand
(`ScenePass::FirstPerson`, docs/functions/soldier-model.md, "Stage 4"): field of
view 0.82, near 0.01, the projection's far 15. What the original does to the depth
before that pass is not measured.
