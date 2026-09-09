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
