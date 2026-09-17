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

`GameServer::simulateFrame` (Linux 0x45af70) runs every player's soldier in
three passes, in this order:

1. **The input** — `simulatePlayersUpdate` (0x454d40) → `simulatePlayerUpdate`
   (0x4549e0) → `Soldier::handlePlayerInput` (Linux 0x54f160, `BF2.exe` 0x5adea0):
   the angles above, the jump, and `Soldier::updateSoldierSpeed` (Linux 0x54ec40,
   `BF2.exe` 0x5a7c50), which smooths the axes and, on the ground, hands
   `speed * axes` along the movement matrix to the response physics as its
   **surface speed** (`SoldierResponsePhysics::setSurfacePositionalSpeed`,
   0x6f8470, `+0xcc`). That matrix still holds the previous tick's look.
2. **The node** — `simulatePlayersPhysics` (0x454370) → `performMobilePhysicsUpdate`
   → `SoldierPhysicsNode::updatePhysics` (0x6f2130), whole below.
3. **The collision** — `Game::updateWorldCollision` (0x40f140), then
   `simulatePlayersCollisions` (0x4543e0): the ground and the walls push the body
   out and `SoldierResponsePhysics::addFriction` (0x6f3390) turns the surface
   speed into the node's friction for the **next** tick's step (below).

So the velocity the input asks for on tick N is the velocity the node reaches on
tick N + 1, damped once.

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

The velocity asked for **does** travel in the state, as the friction it became:
the controlled layout's 0x200 is the node's positional friction, `(surface speed −
velocity) × 30` (below), and the client applies it with `setPositionalFriction`.
Our earlier replay rebuilt the request from the axes and our own record of the
look; that stand-in is what the jump pass replaced.

### The physics node, whole

`SoldierPhysicsNode` fields (Linux; the accessors name them):

| offset | field | set by | read by |
|---|---|---|---|
| `+0x2c` | positional speed | `setPositionalSpeed` 0x6ddd80 (capped at `g_maxSpeed`), `addPositionalSpeed` 0x6dde60 | `getPositionalSpeed` 0x6f1060 |
| `+0x38` | positional acceleration | `setPositionalAcceleration` 0x638110, `addAccelerationAtRelativePosition` 0x6de5d0 | `getPositionalAcceleration` 0x637a90 |
| `+0x44` | local linear speed, added before the step | `setLocalLinearSpeed` 0x637e50 | `getLocalLinearSpeed` 0x637b30 |
| `+0x5c` | drag | the template's `drag` (soldiers: 1.0) | 0x6f2169 |
| `+0x60` | mass | the template's `mass` (soldiers: 100) | 0x6f19a1 |
| `+0x64` | gravity modifier | | 0x6f21d8 |
| `+0x68` | share under water | | 0x6f2182, 0x6f1f26 |
| `+0xac` | positional friction | `setPositionalFriction` 0x6f24d0, `addFrictionAtAbsolutePosition` 0x6f1630 | `getPositionalFriction` 0x6f2270 |
| `+0xb8` | friction contacts this tick | 0x6f1630 counts, 0x6f21e3 clears | |

Every setter zeroes components under 1e-6 (0xb84754).

```
updatePhysics(step)                                                 0x6f2130
  if drag > 0:  updatePositionalDragAdvanced(0.25, 0.25, 0.9, getDragMod(min(water, 1)))
  updatePositionalPhysics(step)
  acceleration.y += basicPhysicsSystem->getGravity() * gravityModifier   0x6f21d8
  frictionContacts = 0

updatePositionalDragAdvanced(forwardCoef, upCoef, rightCoef, mod)    0x6f1890
  w = speed * mod - wind                      wind: basicPhysicsSystem vtable 0x50
  k = -drag * |w| / mass
  d = sum over the node's rows (vtable 0x78): row0 * rightCoef, row1 * upCoef,
      row2 * forwardCoef, each times k * dot(w, row)
  d *= 1/30 (0xb2fcc0), each component no larger than the speed's own
  acceleration += d * 30
getDragMod(w) = w * 25 + 1 - w                                        0x6f0ef0

updatePositionalPhysics(step)                                         0x6f1de0
  |acceleration|² > 1e6 → scaled to 1000;  |friction|² > 62500 → zeroed
  speed += localLinearSpeed
  old = speed
  speed += (friction + acceleration) * step
  speed *= (1 - water) * p-pos-damp + water * p-pos-damp-water
  position += (old + speed) * 0.5 * step
  acceleration = friction = localLinearSpeed = 0
```

The arguments' order is in the call at 0x6f2190: `xmm0` and `xmm1` 0.25
(0xb4355c), `xmm2` 0.9 (0xb34a78). Drag and mass are the soldiers' own
`ObjectTemplate.drag 1.0` and `ObjectTemplate.mass 100` (`Objects/Soldiers/*/*.tweak`).

Measured on the live server, a standing jump: the vertical velocity of every state
from the jump to the landing agrees with this step to 0.001 m/s, and the height to
0.0002 m; without the drag the height drifts 7 mm off by the apex (the states
printed by `--trace-own-state`).

### What the controlled state carries of it

`SoldierNetworkable::updateStateMask` (Linux 0x5db130) fills the update from the
node, and `setNetUpdate` (0x5dd769) puts it back:

| mask | update | from | applied with |
|---|---|---|---|
| 0x80 | `+0x14` | `getPositionalSpeed` (0x5db9b5) | `setPositionalSpeed` (0x5dd7d6) |
| 0x100 | `+0x20` | `getPositionalAcceleration` (0x5dba14) | `setPositionalAcceleration` (0x5de45d) |
| 0x200 | `+0x2c` | `getPositionalFriction` (0x5dba74) | `setPositionalFriction` (0x5de440) |
| 0x40000 | `+0x38` | `getLocalLinearSpeed` (0x5dbad4) | `setLocalLinearSpeed` (0x5de47a) |
| always, bit D | `+0x98` | response physics `+0x11a`, on the ground (0x5dbb9a) | written back to `+0x11a` (0x5dd90b) |
| always, bit A | `+0x7d` | soldier `+0x4b0` (0x5db3f8) | |
| always, bit B | `+0x7e` | soldier `+0x48c`, the response physics' `+0x11b` copied by the input: in water (0x54fa01) | |
| always, bit C | `+0x7f` | soldier `+0x590` (0x5db43d) | |
| 0x400, 0x800 | `+0x64`, `+0x68` | soldier `+0x3c4`, `+0x3c0`: the smoothed forward and strafe axes | |

After those three the apply zeroes the rotational friction, speed and
acceleration (0x5dd846..0x5dd8cd).

### The jump

`Soldier::handlePlayerInput`, Linux 0x54fb7a..0x54fe1d (`BF2.exe` 0x5ae9xx, the
block under `*(+0xfa) != 0 && local_50 != 0`):

```
at the start of every input that is not a replay (flag 4 clear):          0x54f1b1
  noControl (+0x460) > 0:  -= step, at or below 0 becomes -1 (0xb2f3c0)
  airTime   (+0x464) > 0:  -= step, at or below 0 becomes -1
jump = the action's axis under mask 0x200, zero under 0.001               0x54f457
       zero while jumpDelayAfterProne (+0x5a8) > 0                         0x54f72c
       zero in water (+0x48c)                                              0x54f747
       (BF2.exe 0x5ae9xx: zero too unless the pose +0x230 is standing)
onGround (+0x90 → +0x11a) and jump:                                       0x54fb83
  fireDelay (+0x5a0)  = fire-delay-after-jump                              0x54fbbb
  proneDelay (+0x5a4) = prone-delay-after-jump
  sprintRecharge (+0x5b4) = sprint-recharge-delay-after-jump
  airTime = 2.0                                                            0x54fbd9
  ground normal.y (+0xe8) < 0.8 (0xb34b6c):  noControl = 0.5               0x54fbf3
  forward, right = the movement matrix's rows 2 and 0, flattened, normalised
  v = node->getPositionalSpeed()
  J = forward * dot(v, forward) * jumpLength + right * dot(v, right) * jumpLength
  |J| > getSoldierSpeed(7) * jumpLength:  scaled to it                    0x54fd5a
  J.y = 6.0 (0xb355c8) * phy-soldier-jump-factor                           0x54fda6
  node->setLocalLinearSpeed(0); setPositionalAcceleration(J); setPositionalSpeed(J)
  stamina = max(0, stamina - template->SprintLossAtJump (+0x26c))          0x54fe1d
then, still on the ground: updateSoldierSpeed(false, ...)                  0x550514
in the air, not in water, not skydiving (+0x4b0): updateSoldierSpeed(true, ...)  0x550595
```

The jump speaks to the node three times, and each matters: the speed is `J`, the
acceleration is `J` too — so the next step adds `J / 30` on top — and whatever
friction the ground left stays, because nothing clears it.

| variable | registered (`BF2.exe`) | default | data |
|---|---|---|---|
| `phy-soldier-jump-factor` | 0x853b00, `0x9ec334` | 1.0 | 1.0 (`Soldiers/Common/Common.con`) |
| `phy-soldier-jump-length-factor` | 0x853b20, `0x9ec30c` | **0.98** (`0x3f7ae148`) | — |
| `fire-delay-after-jump` | 0x854400, `0x9ec2d8` | 1.0 | 0.7 |
| `prone-delay-after-jump` | 0x854420, `0x9ec2d4` | 1.0 | 0.3 |
| `jump-delay-after-prone` | 0x854440, `0x9ec338` | 1.0 | 0.8 |
| `sprint-recharge-delay-after-jump` | 0x8544a0, `0x9ec318` | 1.0 | 0.7 |
| `ObjectTemplate.SprintLossAtJump` | string 0x8ae294 | | light kits 0.15, heavy 0.2 |

The delays count down in `Soldier::handleFrameUpdate` (Linux 0x548f1d..0x548fd8),
each only while above zero; `Soldier::handleUpdate` hands `sprintRecharge` to
`SprintState::handleUpdate` (0x54b6f5).

Measured on the live server (`--trace-own-state`, light kit): a standing jump's
first state is 0.2022 m up at 6.135 m/s and the stamina drops 1.0 → 0.843
(0.85, seven bits); a sprint jump's first state, from 6.788 m/s on the ground
with friction 2.1701 in the state before, is 6.133 up and 6.872 forward. The
step above gives 6.1336 and 6.8719.

### In the air

`Soldier::updateSoldierSpeed(inAir, forward, forwardDir, strafe, rightDir, step)`,
Linux 0x54ec40, whole:

```
strafe = 0 if getIsSprinting (vtable 0x468) or +0x48c                     0x54ec75
speed = getSoldierSpeed(7), ramped while a pose change runs (+0x494/+0x498/+0x49c)
limit = 0
inAir and noControl < 0 and airTime > 0:                                  0x54f0a5
  speed = limit = airTime * 0.5 * phy-soldier-inair-speed (2.0)
each axis: axis += (input - axis) * (input != 0 ? acceleration : deceleration),
           under 0.001 becomes 0 — in the air too
dir = forwardDir * forwardAxis + rightDir * strafeAxis, / length when over 1
not inAir:  response->setSurfacePositionalSpeed(dir * speed * speedFactor)  0x54f093
inAir and noControl < 0 and airTime > 0:
  old = |node speed.xz|
  node->addPositionalSpeed((dir.x, 0, dir.z) * speed * speedFactor)       0x54ef98
  new = |node speed.xz|
  old > limit and new > old:  speed.xz scaled back to old, y kept         0x54f038
```

So in the air nothing asks for a velocity; the input only steers, with a strength
that falls from 2 m/s to nothing over the jump's two seconds, and it can add speed
only while the soldier is slower than that strength. On flat ground `noControl`
is never set, and steering starts on the first tick in the air.

Measured: pressing forward five ticks into a standing jump, the state after the
first steering tick moves at 0.3547 m/s with the axis at 0.195 — 0.195 × (2 −
5/30) × 0.99 = 0.3539.

### The ground contact and the friction

`SoldierResponsePhysics` fields: `+0xcc` surface speed (the input's), `+0xd8` the
contacts' velocity (`addAdjustVector` in `internalImpulseOn`, 0x6f2803), `+0xe4`
the ground normal, `+0xf4` friction, `+0xf8` elasticity, `+0xfc` resistance — each
the mean of the two materials' (`internalImpulseOn` 0x6f2930..0x6f28cf; material
7001 `Slippery_For_Soldier` sets the friction to 0), `+0x100` flags, 0x40 =
sticking, `+0x11a` on the ground, `+0x11b` in water.

`reset` (0x6f2580) clears on the ground, the contacts, and sets the normal to
`(0, phy-soldier-feet-contact-normal, 0)`. It does **not** clear the surface speed.

```
addFriction(node)                                                     0x6f3390
  not on the ground and not in water:  flags = 1; return   (surface speed kept)
  n = normal; ny5 = n.y^5
  staticLimit  = friction * 7.2 (0xb95a48) * 9.82 (0xb94c0c) * ny5 / 30
  dynamicLimit = friction * 4.8 (0xb95a4c) * 9.82 * ny5 / 30
  rel = contacts - surface;  rel.y += gravity / 30
  t = -(rel - n * dot(n, rel) / dot(n, n))           the tangential part, turned
  resistance > 0:  node->addAccelerationAtRelativePosition(0, t * resistance)
  sticking:      |t| > staticLimit  → t to staticLimit, sticking off,
                                       and then |t| > dynamicLimit → t to dynamicLimit
  not sticking:  |t| > dynamicLimit → t to dynamicLimit;  otherwise sticking on
  node->addFrictionAtAbsolutePosition(position, t * 30)
  surface = contacts = 0; contact count = 0
addFrictionAtAbsolutePosition(p, f)                                   0x6f1630
  friction = (friction * count + f) / (count + 1); count += 1
```

With the node step above, on the ground `speed' = (speed + (surface − speed) +
drag / 30) × 0.99`: the velocity the input asked for, damped, one tick later —
the relation the prediction used before, now with its source.

Measured: running and sprinting steadily, `friction / 30` is `surface − velocity`
for a forward axis of 0.980 at both speeds (3.9: 0.03938; 7.0: 0.07233). A
standing jump's landing at 1.49 m/s sideways states friction 44.78 — the
dynamic limit times 30 for friction 0.95, the mean of `Human_body` 1.1 and a
ground of 0.8 (`Common/Material/materialManagerDefine.con`). The resistance term
is `t` times 0.035 on the sprinting ground and 0.045 where the jump landed —
means of 0.01 and 0.06 (`Grass`, `Sand`) and 0.08 (`Gravel`).

### The terrain push-out

The soldier's collision mesh is not a file: `ObjectTemplate.collisionMesh
Soldier_CollisionMesh` (`Objects/Soldiers/*/*_soldier.tweak`) is recognised by name
(`BF2.exe` 0x6fc730) and built in code (`FUN_00707070`, 0x707070, the branch with no
stream). Its vertices, relative to the pivot (`coll-soldier-pivot-height` 1.0 above
the feet), in order:

| index | vertex | |
|---|---|---|
| 0 | (0, -1, 0) | the feet |
| 1–4 | (∓0.2, -0.6, ∓0.2) | a square 0.4 m above the feet |
| 5–12 | (±0.4, 0, ±0.2), (±0.2, 0, ±0.4) | an octagon at the pivot |
| 13–16 | (0, 0.8, ±0.4), (±0.4, 0.8, 0) | a diamond 0.8 above the pivot |

and every face's material is 24, `Human_body` (the last argument of each
`FUN_00705bf0`).

`SoldierResponsePhysics::internal_checkVsTerrain(step, scale)` (Linux 0x6f5e30,
`checkVsTerrain` passes a scale of 1.0 at 0x6f67a0), the terrain half:

```
for i in 0..4:                                   the mesh's first five vertices
  w = position + matrix * vertex[i] * scale
  h, n, material = heightmapCluster->getHeightAndNormalInWorldCoords(w.x, w.z)  0x6f6128
  depth = w.y - h
  depth <= 0:
    v = node->getTangentSpeed(contact - position)   the node's speed (0x6f0ec0)
    internalImpulseOn(n, v, depth * n.y, vertex material, terrain material, i == 0)
    i == 0 and n.y > groundNormal.y:  groundNormal = n; onGround = 1       0x6f6433
then the water check against getWaterLevelInWorldCoords (vtable 0x130): not read
```

`internalImpulseOn(n, v, strength, ...)` (0x6f26f0) collects, through `setAdjust`
(0x6dfd20: an empty value is taken, one of the same sign keeps the larger, one of
the opposite sign is added):

```
positionAdjust (+0xb4) <- n * -strength
speedAdjust    (+0xc0) <- -(dot(v, n) / |n|²) n        (zero when |n|² is under epsilon)
contacts       (+0xd8)  = running mean of v (addAdjustVector 0x6dfef0), count +0xf0
friction, elasticity, resistance = means of the two materials'
```

and `solveImpulse` (0x6f7690) then moves the node by `positionAdjust` (0x6f77b7)
and, when `speedAdjust` is not zero, hands it times one plus the elasticity and
`phy-imp-mod` (1.0, registered 0x6f3209) to `addRelativeImpulse` (0x6de3d0), which
adds it to the local linear speed. The collision test loop for objects that
follows (`coll-soldier-collision-test-count` 8) is not reversed.

A push of `-depth * n.y` along a unit normal puts the feet back on the terrain's
plane exactly, measured vertically, and the velocity keeps its part along the
slope: walking downhill the soldier stays on the ground because nothing takes his
downward speed away, only its part into the slope.

The terrain's height and normal are `Heightmap::getHeightAndNormalInLocalCoords`
(0x6fbc00): two triangles a cell, split along the diagonal from (x+1, z) to
(x, z+1) — the formulas are in `level.h`, `groundContactAt`. `getMaterialFromGrid`
(vtable 0xf8, 0x6fbc7b) reads the level's `HeightmapPrimary.mat`
(`heightmap.loadMaterialData`, Linux 0x6fc1c0: one byte a cell times
`heightmap.setMaterialScale` squared); **we do not load it yet**, so the ground's
friction and resistance stand in (physics.h).

What the states show of it: the velocity is left as it was and the local linear
speed takes back its part along the normal — 0.486 up while standing, 6.449 on a
landing, (0.149, 0.799, 0.391) on a slope.

**Not reversed yet:** `checkSoldierVsMesh` (0x6f44a0), the objects; the terrain
material map; the water half of `internal_checkVsTerrain`.

Measured with all of the above on the live server (`--trace-own-state`, a run
with a standing jump, steering in the air, a sprint jump and a run jump): of 590
states, 6 differ from our prediction of the same tick by more than 1 cm, against
59 of 441 before. What is left is the spawn, contacts with objects, and two
states in the air whose vertical speed is one tick of gravity past ours — what the
server played on those ticks is not established.

Before this, the prediction smoothed the world velocity and turned it with the
look on the same tick. On the live coop server a run while spinning at 12 degrees
a tick drew 30 corrections of 5–17 cm in 20 seconds; with the order above, none
(`--move-at 900:600:1:0 --look-at 900:600:60:0`: "divergence on average 0.00 m").

## Sprint

**A sprinting soldier does not strafe.** `Soldier::updateSoldierSpeed` begins
with `if (isSprinting || +0x2d4) strafe = 0` (`BF2.exe` 0x5a7c50, the interface at
`+0x154`, slot 0xf8; Linux 0x54ec75, slot 0x468 = `getIsSprinting`, the byte
`+0x516`). The speed comes from the table at 0x9ec428, indexed by the speed state
`+0x2d8` (`Soldier::updateSpeedState`, Linux 0x54e8d0: standing → sprint 4 when
sprinting, walk 2 or run 3 otherwise; crouch 1; prone 0; swimming → 6 or 5):

| index | variable | registered | default |
|---|---|---|---|
| 0 | `phy-soldier-crawl-speed` | 0x853c40 | 0.8 |
| 1 | `phy-soldier-crouch-speed` | 0x853c20 | 2.0 |
| 2 | `phy-soldier-walk-speed` | 0x853c00 | 1.5 |
| 3 | `phy-soldier-run-speed` | 0x853be0 | 3.9 |
| 4 | `phy-soldier-sprint-speed` | 0x853bc0 | 7.0 |
| 5 | `phy-soldier-swim-speed` | 0x853c60 | 2.1 |
| 6 | `phy-soldier-swimcrawl-speed` | 0x853c80 | 3.645 |

(copied into the table at 0x8547b0). A change of pose ramps the speed from the old
state's to the new one's over `pose-<from>-<to>` seconds (`executePoseChange`,
Linux 0x54eb70; the 7×7 table at 0x9ec468 / 0x108e920; defaults at 0x853d20..0x8541a0:
0.25 between walk, run, sprint and crouch, 0.5 to or from crawl and swimming).
A sprint starting or ending is not a pose change and switches at once — measured:
3.78 → 6.79 between two states.

**The sprint itself** is `SprintState` (Linux: constructor 0x43de70, `setConstants`
0x43e0e0, `handleTMSprint` 0x43deb0, `handleUpdate` 0x43ded0):

| offset | field |
|---|---|
| +0x0 | dissipation time — `ObjectTemplate.SprintDissipationTime` (light kits 10, heavy 8) |
| +0x4 | recover time — `SprintRecoverTime` (light 17, heavy 20) |
| +0x8 | limit — `SprintLimit` (0.05) |
| +0xc | drain scale, 1.0; halved on a no-vehicles server (0x43e002) |
| +0x10 | stamina, 1.0 |
| +0x14 | wants: set by a message, cleared by every update |
| +0x16 | sprinting |

```
handleTMSprint(goOnOnly):  wants = 1; if goOnOnly and not sprinting: wants = 0
handleUpdate(blocked, rechargeDelay, step):
  blocked also when dissipation time <= float epsilon (0xb2bfb0)
  sprinting:  stamina = max(0, stamina - scale * step / dissipation)
              ends unless wants and stamina > 0 and not blocked
  otherwise:  recover <= 0 -> stamina = 1
              rechargeDelay <= 0 -> stamina = min(1, stamina + step / recover)
              wants and stamina >= limit and not blocked -> starts
  wants = 0
```

The messages come from the tick (`BF2.exe` `FUN_005c0460`): for every player whose
sprint is on (player slot 0x1b8, Linux `Player::getSprintState`, `+0x1e3`), 0x2a
when it was off the tick before (slot 0x1c0, `getSprintStateLastTick`, `+0x1e4`),
0x29 otherwise; `Soldier::handleMessage` (0x5a7570) passes both to
`handleTMSprint(message == 0x29)`. The code that sets the player's sprint state is
**not found**. Measured on the live server, it is the sprint key with the throttle
forward: shift with strafe alone never raises the flag, and letting go of forward
drops it while the smoothed forward axis still reads 0.979.

The soldier state's 0x4000 carries the stamina and the flag. What the states show:

* the action that lets go of sprint and presses strafe has its strafe dropped; the
  state after it has the flag down and the strafe axis at its first step — so a
  tick's input reads the flag the previous update left, and the update after the
  input takes this tick's action;
* the stamina drains 0.992 → 0.661 over 101 ticks (1/300, dissipation 10) and
  recovers 0.661 → 0.732 over 37 (≈ 1/510, recover 17).

Not modelled: `handleUpdate`'s blocking argument (Linux 0x54b655: `isWalking` and a
field at `+0x50`), the recharge delay after a jump (`sprint-recharge-delay-after-jump`),
`SprintLossAtJump`, the kit's own constants. Unexplained: one state at the start of a
sprint reports 5.53 m/s, between run and sprint; a full stop out of a sprint
decelerates slower than the axes give (5.24, 3.70 m/s against 2.27, 1.36).

Before this, the prediction treated the shift key as sprint and kept the strafe.
Running forward with sprint while pressing A or D, the server pulled us back by
6–24 cm on every state. With `SprintState`: W+shift held, A/D toggled ten times,
the mouse turning — no correction beyond the spawn.
