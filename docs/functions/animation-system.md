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

## Every trigger type, reversed

The factory is `TriggerManager::createTrigger` (`BF2.exe` 0x7fc620, the file
name in its own error is `Animation\BoneAnimation\TriggerManager.cpp`). It knows
twelve type names, each with its own constructor and vtable:

| type | client ctor | vtable | `update` | `applyAnimations` |
|---|---|---|---|---|
| `Trigger` | 0x7ffa00 | 0x941c60 | 0x7feec0 | 0x7fe940 |
| `PoseTrigger` | — | 0x941570 | 0x7fef20 | inherited |
| `RandomTrigger` | 0x7fb9d0 | 0x9415c0 | inherited | **0x7fefa0** |
| `MessageTrigger` | 0x7ffbb0 | 0x941408 | — | inherited |
| `SwitchMessageTrigger` | 0x7fba40 | 0x941610 | — | inherited |
| `MovementTrigger` | 0x7ffbe0 | 0x941468 | 0x7ff430 | 0x7ff490 |
| `ForwardTrigger` | 0x7fbab0 | 0x941660 | 0x7ff680 | inherited |
| `SideTrigger` | 0x7fbaf0 | 0x9416d0 | — | inherited |
| `UpTrigger` | 0x7fbb30 | 0x941740 | — | inherited |
| `TurnTrigger` | 0x7fbb70 | 0x9417b0 | — | inherited |
| `LookAroundTrigger` | 0x7fbbb0 | 0x941820 | — | own |
| `IdleTrigger` | 0x7ffb50 | 0x941cb0 | — | inherited |

In the vtable `update` is slot 18 (offset 0x48), `applyAnimations` slot 19
(0x4c) and `isWithinRange` slot 27 (0x6c). The dashes are the ones read in the
Linux server instead, where the same classes carry their names:
`dice::anim::<type>::update(AnimationSystem&)` — `Trigger` 0x6cd480,
`LookAround` 0x6cd510, `Turn` 0x6cd530, `Up` 0x6cd580, `Side` 0x6cd5e0,
`Movement` 0x6cd640, `Message` 0x6cd6d0, `Forward` 0x6cd740,
`SwitchMessage` 0x6cd9e0, `Idle` 0x6ce370, `Pose` 0x6ce520.

### The state the conditions read

The conditions all read one object, the animation system itself. Its fields, by
the client's offsets (32-bit; the Linux ones are 4 bytes further on from `pose`):

| offset | what |
|---|---|
| +0x04 | pose: 0 stand, 1 crouch, 2 prone, 3 swim |
| +0x08 | the message mask of this tick |
| +0x0c | the message mask of the previous tick |
| +0x14..+0x1c | the direction, a unit vector: x side, y up, z forward |
| +0x20 | the speed |
| +0x24..+0x2c | a second vector, taken instead when the trigger has `useDirection` |
| +0x34 | the turn value |

`AnimationSystem::setTriggerMovement(float, Vec3 const&, float, Vec3 const&)`
(Linux 0x6b1920) is what fills them; who calls it in the client, and in what
frame the direction is measured, is **not established** yet.

### The conditions

```
Trigger::update(sys):                                   0x7feec0
    if fadeInTime (+0x28) > 0: sys.setFadeInTime(this)
    for every child: if not child.update(sys): return false
    this.applyAnimations(sys); return true

Trigger::applyAnimations(sys):                          0x7fe940
    for every bundle: sys.playBundle(this, bundle, +0x24, -1.0, 1.0)

RandomTrigger::applyAnimations(sys):                    0x7fefa0
    plays one bundle, rand() % count

PoseTrigger::update(sys):                               0x7fef20
    child = children[min(sys.pose, count - 1)]
    if not child.update(sys): return false
    this.applyAnimations(sys); return true   // its own children are not walked

MessageTrigger::update(sys):                            Linux 0x6cd6d0
    mask = this (+0x30); if mask and (sys.messages & mask) == 0: return true
    else: Trigger::update(sys)

SwitchMessageTrigger::update(sys):                      Linux 0x6cd9e0
    with two or more children: the child is [1] when (sys.messages & mask) == 0,
    otherwise [0]; if its update returns false, so does this
    then, with a bundle: if (mask & sys.messages) == (mask & sys.previous
    messages) nothing happens; otherwise the bundle is played forwards (1.0) or
    backwards (0.0) by whether the flag is set — the same clip run in reverse

MovementTrigger::update(sys):                           0x7ff430
    holder = this (+0x30)
    if holder.required mask (+0x10) and not (sys.messages & it): return true
    if holder.forbidden mask (+0x0c) and (sys.messages & it): return true
    if not isWithinRange(sys.speed): return true
    else Trigger::update(sys)

ForwardTrigger::update(sys):                            0x7ff680
    value = triggerOnAcceleration (+0x34) ? sys.second.z : sys.speed * sys.direction.z
SideTrigger:   value = triggerOnAcceleration ? sys.second.x : sys.speed * sys.direction.x
UpTrigger:     value = triggerOnAcceleration ? sys.second.y : sys.direction.y
    each of the three takes |value| when `useDirection` (+0x35) is set,
    then the same isWithinRange and Trigger::update
TurnTrigger:   value = sys.turn (+0x34), then isWithinRange

IdleTrigger::update(sys):                               Linux 0x6ce370
    fields: minimum +0x30 = 5, maximum +0x34 = 10, next +0x38 = 5 (the ctor,
    0x7ffb50)
    if sys.idleTime != 0 and minimum < maximum:
        if next > idleTime or idleTime > maximum: return true
        next = minimum + (rand() % 100) / 100 * (maximum - minimum)
        play one bundle, rand() % count
    then Trigger::update(sys)

LookAroundTrigger::update: nothing of its own; its applyAnimations (Linux
0x6cd7d0) blends by the system's +0x34 and +0x38 — not reversed in full.
```

Two things are worth keeping in mind while reading this. **A false answer is
rare**: a trigger whose condition does not hold returns *true* and simply plays
nothing, so the tree goes on. Only a child that answers false stops its parent —
which is what `PoseTrigger` and `SwitchMessageTrigger` use to pick a branch.
And **`isWithinRange`** is the range of the trigger's value holder
(docs above): no holder, or equal bounds, means "always".

The two flags are named the other way round from what they do — the client's own
`.con` writer (0x7ff330) prints `triggerOnAcceleration` for +0x34, the flag that
switches to the second vector, and `useDirection` for +0x35, the one that takes
the absolute value. Neither appears in a soldier's or a weapon's third-person
system: in the whole of `Objects_client.zip` `useDirection` is set only on the
ladder's `UpTrigger`s and `triggerOnAcceleration` only on the first-person
`TurnTrigger`s (four each).

## A tick walks two triggers, not the whole tree

`AnimationSystem::update` (`BF2.exe` 0x7f7390, Linux 0x6b3820):

```
if no active trigger yet:
    "startup" is looked up and played once, if the file has one
    the active trigger becomes "root"
the post trigger is "postRoot", looked up once
messages |= onceMessages; changed = (messages != previous)
root.update(sys)
postRoot.update(sys)                       // with a flag set while it runs
messages &= ~onceMessages; previous = messages; onceMessages = 0
then every bundle player is updated, and one that answers false is dropped
```

So a tick reaches only what hangs under `root` and `postRoot`. `hit`, `die` and
`specialMoves` sit under `completeTree` beside them and are **never** walked by a
tick: something has to play them by name (`playTrigger`, `playPostTrigger`). That
is the gate the tree itself does not carry — and why a standing soldier does not
die and get hit every frame.

The idle time the IdleTrigger reads is counted in the same place: while exactly
one player of a kind is alive, a timer grows by the frame's step; two or more
reset it to zero. So "idle" means "nothing but the base pose is playing".

## Playing a bundle

`AnimationSystem::playBundle(trigger, bundle, priority, startTime, speed)`
(`BF2.exe` 0x7f7a20): the system keeps its players in a list ordered by priority
(the trigger's +0x24). A bundle that is already playing is **not** restarted —
the call finds its player, sets the parameters on it and marks it as touched this
tick. Otherwise a player is made and inserted before the first one of a higher
priority.

`setParameters(startTime, speed)` (0x7fdf40): a start time of 0 or more only
takes effect on a looping bundle; the speed goes into the player unless the
bundle has `jumpToLastAnimationAtStop`, and a non-looping bundle asked to play
with a negative speed starts at its own end.

`MovementTrigger::applyAnimations` (0x7ff490) is where a movement bundle gets its
playback speed and its blend:

```
if the trigger has no value holder and exactly two bundles:
    one of them is picked by the system's phase (+0x44) — the left or the right
    foot forward — and played
otherwise:
    speed = holder ? sys.speed / holder.values[2] : 1      // the third number!
    player = playBundle(trigger, bundle[0], priority, -1, speed)
    if the bundle has exactly four animations:
        factor = asin(clamp(|direction.z|, -1, 1)) * 2/pi   // 0.63661975
        a = direction.x >= 0.01 ? 2 : 3
        b = direction.z < -0.01 ? 1 : 0
        setBlendAnimations(factor, a, b)                    // 0x7fe120
        sys.phase (+0x44) = player.time / bundle.length
```

**The third number of `AnimationValueHolder.values` is the speed the clip was
animated at.** `3p_stand_run 0.1 3.9 5.8` plays the run clips at `speed / 5.8`,
so the legs' cadence follows the ground speed instead of running on the spot.

**A bundle's animations are indexed from the end.** `Bundle::getAnimation(int)`
(Linux 0x6b6460) walks the list forward from its head, and the list is built by
pushing to the front — the client's own `.con` writer (0x7fa930) walks it the
other way to print the file back in its original order. So for

```
animationBundle.addAnimation .../3p_strafeLeft.baf
animationBundle.addAnimation .../3p_strafeRight.baf
animationBundle.addAnimation .../3p_runBackward.baf
animationBundle.addAnimation .../3p_runForward.baf
```

the indices are 0 runForward, 1 runBackward, 2 strafeRight, 3 strafeLeft — which
is what makes the pair choice above read straight: `b` is the run clip by the
sign of the forward component, `a` the strafe clip by the sign of the side one,
and `factor` weighs the run clip against the strafe one.

## What is still missing

`BundlePlayer::update` (Linux 0x6b9150) in full: how the time advances through the
bundle's own length, the fades in and out (`fadeInTime`, `fadeOutTime`), the
events, and `playForever`, `abruptPlayback`, `jumpToLastAnimationAtStop`. And the
blend of several players on one skeleton — `applyAnimationsOnSkeleton` with two
animations and two floats, plus `getSlerpFactor`.

Who fills the state is not established either: `setTriggerMovement`'s caller in
the client, the frame the direction is measured in, and where the idle time comes
from.

One message is named by the data itself. In `soldiers/Common/Animations/
ValueHolders.inc` the walking range demands bit **4** and the running range
forbids it:

```
AnimationSystem.createValueHolder 3p_stand_walk
AnimationValueHolder.values 0.1 1.5 1.7
AnimationValueHolder.passOnMessage 4
AnimationSystem.createValueHolder 3p_stand_run
AnimationValueHolder.values 0.1 3.9 5.8
AnimationValueHolder.stopOnMessage 4
```

so bit 4 is "walking" — the two ranges overlap over 0.1..1.5 and nothing else
separates them. What bits 1 and 2 are (the soldier's `face_anger` watches 1, a
weapon's triggers 1, 2 and 4) is not established.

## What the selection gives now

`anim::System::select` walks the tree with every condition above
(`src/anim/system.h`, tests in `tests/test_anim_system.cpp`). For a soldier
standing it answers `stand_rightFootBack`, for one running `stand_run` beside it,
and the parachute, the random hits and the idle faces no longer come out all at
once.

What it still answers with, and should not, is the one-shot subtrees: `hit`,
`die`, `proneToStill`, `reviveOnBack`. They hang under `completeTree` as plain
triggers with no condition at all, so the gate cannot be in the tree — it is in
`playBundle` and `BundlePlayer`, which are the next thing to read.
