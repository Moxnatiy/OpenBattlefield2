# The soldier animation system

All of it is **described in the game's data**, not in code:
`soldiers/Common/Animations/AnimationSystem3p.inc` (728 lines) plus
`ValueHolders.inc` (66). It is ordinary `.con`, so our own interpreter
reads it.

For Dalian Plant that gives **78 animations, 57 bundles, 62 triggers, 31
ranges**, and exactly one root — `completeTree`.

## The tree

```
completeTree
  root
    pose            [PoseTrigger]  -> stand | crouch | prone | swim
  specialMoves      -> proneToStill, stillToProne, reviveOnBack ...
  postRoot -> faceRoot -> face_neutral, face_anger [MessageTrigger] ...
  hit               -> hitFrontHead [RandomTrigger] ...
  die               -> standDie, crouchDie, face_dead ...
```

## How it is walked (reversed)

`Trigger::update` asks its children first and then adds its own bundles.
The types differ only in the condition:

**`PoseTrigger::update` (0x08353040)** — takes the child **by pose
number**:
```
index = pose; if index >= child count: index = count - 1
if children[index]->update() returned false -> apply nothing
```
The children's order in the data is `stand, crouch, prone, swim`, so the
pose numbers match those in physics
(`SoldierResponsePhysics::getSoldierHeight` takes the crouch height for
pose 3 — which is swimming).

**`MovementTrigger::update` (0x08353d70)** — first the message masks (one
required, another forbidden), then `isWithinRange(speed)`, and only then
the ordinary walk.

**`MovementTrigger::isWithinRange` (0x08353d00)**:
```
no range -> yes
a == b -> yes
a >= 0 -> bounds [a, b], otherwise [b, a]
outside the bounds -> no
```
So **the first two numbers of `AnimationValueHolder.values` are the
bounds**, and the engine uses the third separately. Negative ranges are
written the other way round (`3p_turn -1 -3 -10`), and it is the sign of
the first bound that tells them apart.

## The numbers agree with physics

| Range | Values | Compare with |
|---|---|---|
| `3p_stand_walk` | 0.1 .. 1.5 | `phy-soldier-walk-speed` = **1.5** |
| `3p_stand_run` | 0.1 .. 3.9 | `phy-soldier-run-speed` = **3.9** |
| `3p_sprint` | 5.5 .. 6.3 | `phy-soldier-sprint-speed` = 7 |

The animation bounds are the same speeds we pulled out of the engine
earlier (`docs/functions/soldier-physics.md`). Two independent routes gave
the same numbers.

## Checking

```bash
anim_info "Game Files/mods/bf2" objects/soldiers/Common/Animations/AnimationSystem3p.inc 0 0
anim_info "Game Files/mods/bf2" objects/soldiers/Common/Animations/AnimationSystem3p.inc 0 3.9
```

At zero it picks `stand_rightFootBack` (`3p_stand.baf`); at 3.9 `stand_run`
is added to it — exactly as it should be.

## What is still missing

Conditions we do not model yet: messages (`MessageTrigger`), random choice
(`RandomTrigger`), idling (`IdleTrigger`), direction (`ForwardTrigger` and
`SideTrigger` look at a component of the velocity, not its magnitude).
Because of that the selection sometimes includes stray bundles such as
`skydive` — those are exactly what those conditions filter out.

Time is missing too: bundles have `fadeInTime`/`fadeOutTime` and their own
length, and playback with transitions is driven by `BundlePlayer`.
