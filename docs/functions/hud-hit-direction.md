# The hit direction

Retail shows where a hit came from with one red arc turned around the crosshair.
It never tints the whole screen. The node is in the game's data:

```
HUD/HudSetup/HudElementsHit.con (Menu_client.zip, included from HudElementsLast.con)
hudBuilder.createPictureNode        IngameHud HitDirection 210 -270 370 600
hudBuilder.setPictureNodeTexture    Ingame/Player/Icons/Hud/hitIndicator.tga
hudBuilder.setPictureNodeRotateVariable HitDirectionAngle
hudBuilder.setPictureNodeCenterPoint 0 0
hudBuilder.setNodeColor             1 0 0 1
hudBuilder.setNodeShowVariable      HitDirectionShow
hudBuilder.setNodeInTime            0.1
hudBuilder.setNodeOutTime           2
hudBuilder.addNodeAlphaShowEffect
```

`HitDirectionShow` is the information layer's bool +0x1a8 and `HitDirectionAngle`
its float +0x1a0 (hud-variables.md, 0x468dd0).

## The chain

| step | where | what |
|---|---|---|
| server | Linux `Player::updateHitFromDirection` 0x496160 | +0x210 := fmod(atan2(x, z), 2π) of the hit vector, (x, 0, z) normalised; dirty +0x214 := 2 |
| wire | Linux `Player::getStateSync` 0x4a16a0, from 0x4a1781 | while +0x214: 1 bit set, then 8 bits = clamp((a / 2π (0xb355b8) + 1 (0xb2f3bc)) × 0.5 (0xb2f234), 0, 1) × 255 (0xb355bc); else the bit clear |
| client | `Player::setStateSync` `BF2.exe` 0x5018a0 | presence bit; 8 bits b; s = b × 1/255 (0x8ac480) × 2 − 1 (0x87fe78); s := 0 when \|s\| ≤ 1/127.5 (0x8ac490); +0x1b8 := s × 2π (0x8ac48c) |
| | same | if the counter +0x1bc is 0: HUD event `(*0x9ff964)->+0x10(2, 0x34, 0, 0)`; +0x1bc := 2. After the block, every sync: +0x1bc −= 1 while not 0 |
| | same, next field | presence bit, 8 bits × 1/255 × 0x8ac094 → +0x1c0, handed to the camera (`getCamera` +0x17c, its +0x4c) when +0x1c4 is 0: the camera shake — **not applied** |
| HUD | 0x465b50, case 0x34 (dispatcher 0x465ce0, type 2) | +0x19c := +0x1a4; 0x464aa0(1, 0) |
| HUD | layer frame 0x4668d0, call at 0x467548 | 0x464aa0(+0x1a8, dt) |
| HUD | 0x464aa0 | below |

`getHitFromDirection` is the player's vtable +0x1e0 (`tools/linuxded/vtable.py
dice::hfe::Player --msvc`: Linux +0x3c8; it returns +0x210, 0x494db0).

### 0x464aa0(show, dt)

```
+0x1a8 := show
if show and (player := *0xa08f60 vfn +0x30) and (vehicle := player vfn +0x44 getVehicle)
       and vehicle +0x3c != 0:                         -- +0x3c: purpose not established
    m  := (player vfn +0x17c getCamera) vfn +0x10 (0)  -- a matrix
    f  := (m +0x20, 0, m +0x28), normalised by 0x4639b0 (zero stays zero)
    +0x1a0 := fmod(atan2(f.x, f.z), 2π (0x89e618)) − player vfn +0x1e0 ()
if +0x19c ≤ 0 (0x93c850):  +0x19c := +0x1a4; +0x1a8 := 0
elif +0x1a8:               +0x19c −= dt
```

The constructor (0x468fd9..0x468fef) zeroes +0x19c, +0x1a0 and +0x1a8, and sets
+0x1a4 to **0.1** (0x3dcccccd).

So the arc comes up over 0.1 s, the HUD shows it for 0.1 s, and the node then
fades it out over 2 s. While the HUD shows it, the angle follows the camera.
While it fades, the angle stays where it was. A cue only comes after one
player sync without a hit direction (the +0x1bc counter).

## The pivot

`setPictureNodeCenterPoint` (0x7aca50..0x7aca7f) stores its two integers as
floats at the picture's +0x10/+0x14 and sets +0xc. The draw (0x7acab0) turns the
quad by 0x800be0 about that point. Without the flag it turns it about rect +
size × 0.5 (0x8ee510). The turn itself is x′ = dx·cos + dy·sin,
y′ = dy·cos − dx·sin, the same as ours (`hud/render.cpp`, `quad`).

**Not established:** the space the corners are in when 0x7acab0 turns them, and so
where (0, 0) is. We take it as the virtual screen's centre (400, 300). The texture
is the evidence. `hitIndicator.tga` is 256 × 256, 32-bit, and its opaque pixels
are rows 127..138, x 66..196. On the node's rectangle that puts the arc at y
28..53, x 305..493, top centre. About (400, 300) it circles the crosshair at
about 260. About the rectangle's own middle (395, 30) it would spin in place. The
aircraft's banking ladders, the only other `0 0` centre points (e.g. F15 sidebars
at x 265 and 520, y 172..428), straddle the same point.

## Ours

* `net::bf2::decodeHitFromDirection` and `SoldierPlayerSync::hitFromDirection`
  (bf2_events.h): the byte.
* `RemoteWorld::applySoldierSync`: +0x1b8 and +0x1bc (`ownHitFromDirection`,
  `hitDirectionSyncs`), and a cue count for event 0x34 (`hitDirectionCues`).
* `app::HitDirection` (hit_direction.h): the layer's +0x19c..+0x1a8 and 0x464aa0.
  `InfantryView::updateHud` feeds it the camera's forward and writes
  `HitDirectionShow` / `HitDirectionAngle`.
* `hud::Node::hasCenterPoint` and the pivot in `render.cpp`.

Not taken: the vehicle +0x3c condition (we compute whenever the HUD shows it),
the camera shake, and the second sender of event 0x34 at 0x5e601c (in
0x5e5da0, called from 0x4e0b20, the weapon/damage side; it fires when a
template's +0x50 is 0xff — purpose not established).

In this tree: `net::bf2::decodeHitFromDirection` and `app::HitDirection`. The
session's `applySoldierSync` fields, `InfantryView::updateHud` and the
`render.cpp` pivot named above come with the session and HUD wiring.

## The measure

`test_hit_direction`: the decode against the server's packing, the angle against
the camera, the 0.1 s and the freeze while fading. **Not yet seen on screen**: no
live hit was captured and nothing was compared with a retail frame.
