# Soldier physics: the constants and the collision model

Everything below was read out of the Linux server, not guessed.

## Where the numbers came from

The engine registers its variables through
`dice::hfe::Vars::getFloat(name, default)`. In the code that is a pair of
instructions — the string's address and a float value — so every default
can be extracted mechanically:

```bash
python tools/dwarf/dump_vars.py "Game Files/OtherFiles/linuxded/bin/ia-32/bf2" soldier
```

The game's data may override them (`objects/soldiers/common/common.con`
sets `phy-soldier-deceleration 0.4` instead of the default 0.2), and then
the data wins — exactly as in the original.

## The soldier's shape is a column of spheres

`SoldierResponsePhysics::getSoldierHeight` (0x08378330) returns the height
and **the number of spheres** for a pose:

| Pose | Height | Spheres |
|---|---|---|
| standing | `coll-soldier-stand-height` = **1.7** | **5** |
| crouching | `coll-soldier-crouch-height` = **1.4** | **3** |
| prone | `coll-soldier-prone-height` = **0.8** | **1** |

The step between centres is `(height - 2 * radius) / (count - 1)`. The
radius is `coll-soldier-radius` = **0.25**.

For standing that gives spheres at heights 0.25, 0.55, 0.85, 1.15, 1.45.
It is the column of spheres that lets a soldier step onto a stair: the
lowest sphere meets the rise and is pushed **up** rather than sideways. A
single sphere "at chest level" cannot do that in principle.

The original's file is `Physics/SoldierResponse.cpp` (visible in a debug
string).

## The constants

| Variable | Value | What it is |
|---|---|---|
| `coll-soldier-radius` | 0.25 | sphere radius |
| `coll-soldier-stand/crouch/prone-height` | 1.7 / 1.4 / 0.8 | height per pose |
| `coll-soldier-pivot-height` | 1.0 | height of the pivot point |
| `coll-soldier-collision-test-count` | 8 | how many times to push out per tick |
| `coll-soldier-extend-ray` | 0.9 | how far to extend the ray under the feet |
| `phy-soldier-feet-level` | -0.04 | where "the floor" is relative to the feet |
| `phy-soldier-feet-contact-normal` | 0.5 | minimum normal Y for a surface to hold (slopes up to 60°) |
| `phy-soldier-walk-speed` | 1.5 | walking |
| `phy-soldier-run-speed` | 3.9 | running |
| `phy-soldier-sprint-speed` | 7 | sprinting |
| `phy-soldier-crouch-speed` | 2 | crouched |
| `phy-soldier-crawl-speed` | 0.8 | prone |
| `phy-soldier-swim-speed` | 2.1 | swimming |
| `phy-soldier-swimcrawl-speed` | 3.645 | swimming fast |
| `phy-soldier-inair-speed` | 2 | control in the air |
| `phy-soldier-start-float` | 0.99 | the fraction of height in water at which the soldier floats up |
| `phy-soldier-stop-float` | 0.9 | and at which he stands on the bottom again |
| `phy-soldier-sprint-limit` | 0.5 | stamina reserve |
| `phy-soldier-sprint-dissipation-time` | 10 | how many seconds it takes to spend |
| `phy-soldier-sprint-recover-time` | 120 | and to recover |
| `phy-soldier-friction` / `elasticity` / `resistance` | 1 / 0 / 0.02 | collision response |
| `soldier-drown-damage` | 8 | damage per second underwater |
| `soldier-prone-inwater-limit` | 1.1 | the depth beyond which going prone is not allowed |

The full dump is `docs/reference/soldier-vars.txt`.

## The look multipliers

`Soldier::handlePlayerInput` multiplies the look input by two global
factors (Linux server, 0x54f63c and 0x54f666):

```
turn_x = input_x * g_soldierLookAroundX
turn_y = input_y * g_soldierLookAroundY
```

Both are read from `Vars` in a static initialiser (0x5476f8 and 0x54770a)
with a **default of 5.0** (the constant at 0xb34b64):

* `phy-soldier-look-factor-x`
* `phy-soldier-look-factor-y`

The game's data does not contain these variables, so the default 5.0
stands.

Separately, `Settings/Controls.con` has `ControlMap.mouseSensitivity` —
1.7 for the infantry map and 3 for another.

### Mouse sensitivity — from the client

`ControlMap` keeps the sensitivity in field **+0x50** and the keyboard
sensitivity in **+0x54**. That is visible where the engine **writes**
`Controls.con` back out (`BF2.exe`, 0x6b03ed onwards): it skips a line
when the value equals the default, and that comparison names the default:

```
006b03ed CMP dword ptr [EBX + 0x50], 0x3f800000   ; mouseSensitivity, default 1.0
006b0401 PUSH 0x912504                            ; "ControlMap.mouseSensitivity "
006b041c CMP dword ptr [EBX + 0x54], 0x3dcccccd   ; keyboardSensitivity, default 0.1
```

In the game's data `mouseSensitivity` is set for two maps: 1.7 for
infantry and 3 for the helicopter. But it is **not** the one that turns
mouse movement into the soldier's look: `c_PIMouseLookX/Y` are bound to
the mouse in `defaultPlayerInputControlMap` (`Settings/Controls.con`,
248–251), and no `mouseSensitivity` is set there — so the default **1.0**
applies.

**What is still missing.** The chain "pixels → axis value → angle" has one
more link: a factor of 5.0 cannot be degrees per tick (in the captured
traffic the mouse axis reaches 169). We need to find what
`Soldier::handlePlayerInput` **in the client** does with the product —
multiply by frame time, divide by a constant, or add it as a rate. Until
then our factor stays **unmeasured**, and a full sweep of the mouse does
not give a full turn.

## The look over one tick

`Soldier::handlePlayerInput` in the client (`BF2.exe` 0x5adea0), block
0x5ae72b..0x5ae960, then `FUN_005a8630`. The action is copied to the stack:
axis *i* sits at `EBP - 0x1c0 + 4i` under mask bit `1 << i` (`EBP - 0xc0`).

| field | offset | state bit | what |
|---|---|---|---|
| body yaw | `+0x224` | 0x2 | the movement follows it (with the aim, see below) |
| aim offset | `+0x240` | 0x4 | the camera's yaw off the body |
| turn left | `+0x248` | 0x8 | what the body still turns to take the offset back |
| pitch | `+0x238` | 0x10 | positive looks down |

```
delta = axis4 * phy-soldier-look-factor-x (0x9ec248)      ; mouse X, 0x5ae205
F     = clamp(axis3, -1, 1); moving = |sign(F) * F^2| > 0.01 (0x892adc)  ; 0x5ae0f7, 0x5ae7af
bodyDelta = delta
moving:   aim != 0 -> turnLeft = aim                       ; 0x5ae7c1
standing: aim += delta                                     ; 0x5ae7e8
          aim >= max or aim <= -max -> turnLeft = aim      ; 0x5ae805, 0x5ae826
          otherwise bodyDelta = 0                          ; 0x5ae839
turnLeft != 0:                                             ; 0x5ae842
  step = sign(turnLeft) * soldier-lookSideRestore (0x9ec2cc), no further than turnLeft
  turnLeft -= step; aim -= step; bodyDelta += step
body += bodyDelta, wrapped into (-360, 360)                ; 0x5ae8c6
FUN_005a8630: aim clamped to ±max.x, pitch to ±max.y (after += axis5 * look-factor-y)
```

`max` is `soldier-lookMaxAngle`, a vector registered at 0x854280 as
**(40, 85, 0)**; `soldier-lookSideRestore` at 0x854260 is **4.0**. The game's
data sets neither. The yaw limits of a seat (`+0x208`, `ESI` at 0x5ae801),
the sight's zoom multiplier (0x5a61a0) and recoil (0x5a5f40, 0x5a5fa0) are not
modelled.

Measured on the live server, strafing left with the mouse at -5 degrees a tick:
aim -36, 0x8 -36, body -271.5 → aim -37, 0x8 -37, body -280.5; at the limit
aim -40, 0x8 -41, body -9 a tick (the mouse's 5 and the restore's 4). Code:
`src/server/soldier_look.h`, test `tests/test_soldier_look.cpp`.

## One tick

The order the server runs a soldier's tick in, read from the live server's
states of our own soldier and confirmed in the binary:

1. **The physics node** — `SoldierPhysicsNode::updatePositionalPhysics`
   (Linux server 0x6f1de0). The new velocity is the old one plus the
   accumulated change, times `p-pos-damp` (0.99, `BF2.exe` 0x8607a0) or
   `p-pos-damp-water` (0.9, 0x8607c0) mixed by the share under water (`+0x68`).
   The position then moves by **the average of the old and the new velocity**
   (`(old + new) * 0.5`, the 0.5 at 0xb2f234) times the step.
2. **The input** — 0x5adea0: the angles above, then
   `Soldier::updateSoldierSpeed` (0x5a7c50) smooths the axes and asks the physics
   (`+0x84`) for `speed * axes` along the matrix of the soldier's object
   (`*(+0x1d0)->+0xc->+0x80`: row 2 forward, row 0 right, in the decompilation of
   0x5adea0). That matrix still holds the previous tick's look.

What the states show (forward held, spinning 12 degrees a tick):

| state | position x | velocity | axis 0x400 | body yaw |
|---|---|---|---|---|
| 333 | -121.7630 | 0, 0 | 0.195 | -78.05 |
| 334 | -121.7756 | -0.757, 0 | 0.352 | -66.05 |
| 338 | -121.9807 | -1.891, 1.701 | 0.723 | -24.05 |
| 339 | -122.0396 | -1.641, 2.257 | 0.774 | -12.05 |

* the velocity's length is the **previous** state's axis: 0.757 = 3.9 × 0.196 × 0.99;
* its heading is the look of **two** states back: -90 at 334, when the body was
  -90 at 332;
* the position moves by the average: 339 − 338 = (−1.891 − 1.641) / 2 / 30 = −0.0589.

Strafing with the aim offset at -40 the heading is body + aim − 90 of two
states back, so the matrix is the whole look, not the body alone.

The velocity asked for does not travel in the state. After a correction our
prediction rebuilds it from the state's axes, the played action's speed and our
own record of the look that tick's input read; how the original client restores
it is **not established**. The air branch of 0x5a7c50 and the jump's place in
this order (the input adds the impulse, `*0x9ec334 * 6.0` in 0x5adea0; which
tick's physics takes it is not measured) are not reversed.

Before this, the prediction smoothed the world velocity and turned it with the
look on the same tick. On the live coop server a run while spinning at 12 degrees
a tick drew 30 corrections of 5–17 cm in 20 seconds; with the order above, none
(`--move-at 900:600:1:0 --look-at 900:600:60:0`: "divergence on average 0.00 m").
