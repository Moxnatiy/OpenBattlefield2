# HUD animation: nodes appearing and disappearing

The HUD in Refractor 2 is not static. Every node appears and disappears **in
time**, and it is done not by separate HUD code but by the same MemeFile graph
as the menu (`docs/formats/hud-meme.md`).

## What is visible in the binary

The builder (`C:\dice\...\Code\BF2\Menu\Bf2HudBuilder.cpp` — the path is in the
binary itself) creates for a node a chain of graph nodes named by a template:

| Template | Where in BF2.exe | When it is created |
|---|---|---|
| `%sCullNode` | 0x79b2c0, 0x79db80 | together with the node; it is its "show or not" |
| `%sAlphaShowEffect` | 0x79db80 (string 0x933690) | `addNodeAlphaShowEffect` |
| `%sMoveEffect` | 0x79b2c0 | `addNodeMoveShowEffect` |

Both commands first check that no such node exists yet (otherwise
`already has an alphaShowEffect!` / `already has a MoveShowEffect!` go into the
log, strings 0x933670 and 0x93347c), then look for `%sCullNode` and attach the
new node to it. So **an effect is a child of the cull node**: cull says where to
go, the effect says by how much.

`addNodeMoveShowEffect <angle> <distance>` assembles two data nodes (0x82dfc0
for float, 0x82d8c0 for int) and glues them into a class with the RTTI name
**`dice::meme::Bf2MoveEffect`** (0x937a44, vtable 0x937ad8). Beside it in the
binary lies a separate class **`dice::meme::Bf2SinMoveEffect`** (0x937a60,
vtable 0x937b38) — and that is exactly why we treat ordinary movement as
**linear**: the sine curve does exist in the game, but it is a different class,
which this command does not create.

The times come from the pair `setNodeInTime` / `setNodeOutTime` (231 calls each
in the data), both with a single float argument.

## The direction of movement — verified from data, not guessed

`HUD/HudSetup/HudElementsLevelsList.con`: the map-voting panel stands at
y = 377..383 in the base 800x600 and has

```
hudBuilder.setNodeInTime  0.3
hudBuilder.setNodeOutTime 0.3
hudBuilder.addNodeMoveShowEffect -1.57 376
```

The angle -1.57 is -pi/2. The screen's y axis points down, so the formula
`dy = sin(a) * distance` would give a start at y ~ 1, that is **above** the
screen; while `dy = -sin(a) * distance` gives y ~ 753, so the panel drives in
**from below**, from beyond the edge of the 600-pixel screen. The second is what
is seen in the game.

A check elsewhere: `HudElementsPlayer.con` — the health bar (on the left) has
`3.14 53`, that is dx = -53, arriving from the left; the stamina bar (on the
right) has `0 53`, dx = +53, arriving from the right. The same rule.

So: `offset = (cos a, -sin a) * distance * (1 - progress)`.

All 94 calls in 18 files have exactly two arguments; the angles in the data are
`3.14`, `0`, `-0.7`, `-1.57`.

## What has been done from this

`src/hud/animation.h` + `animation.cpp`: `Animator` keeps a progress of 0..1 per
node, drives it to 1 over `inTime` and to 0 over `outTime` linearly; the
`alpha` effect multiplies the alpha by the progress, `move` shifts the node by
the formula above. A node the animator hears of for the first time is put
straight into its final state: otherwise the whole HUD would drive in at level
start.

A node **without** any effect has no transition: it simply appears and
disappears, as before.

## What is still missing

* `addNodeVariableMoveShowEffect` (string 0x92bd84) — movement whose distance
  comes from a variable. It does not occur in the data.
* `dice::meme::Bf2SinMoveEffect` — who creates it has not been found yet.
* `SetVariableSineAction` with a **non-zero** braking distance has not turned up
  in the data yet, so the sine branch has not been checked on the game — only by
  a test.

## The team's side: where the icon comes from

That is not HUD data but the level's. Every level's `Init.con` has

```
gameLogic.setTeamName 1 "CH"
gameLogic.setTeamName 2 "US"
```

and it is that string the game substitutes into the templates in place of `%s`:

| Template | String address | Where it is filled |
|---|---|---|
| `Ingame/Flags/Icons/Minimap/%s/miniMap_CP.tga` | 0x925af8 | 0x74fb70 |
| `Ingame/Flags/Icons/Minimap/%s/miniMap_CPBase.tga` | 0x925ac4 | 0x74fb70 |
| `Ingame/Flags/Icons/Minimap/%s/miniMap_flag.tga` | 0x925b5c | 0x74fb70 |
| `Ingame/Flags/Icons/Hud/Score/%s/scoreBoard_Flag.tga` | 0x931030 | 0x787260 |
| `Levels/%s/Hud/Minimap/ingameMap.tga` | — | 0x74fb70 |

What is substituted is the result of `gameLogic->vtbl[0x48](team number)` — at
0x74fc58 the call with 1, at 0x74fca0 with 2. For the zeroth (neutral) side
0x74fb70 has a separate string with a ready-made `Neutral` (0x925b28).

The team names across all 22 levels of the game are exactly **CH, EU, MEC, US**,
and the icon directories in `Menu_client.zip` are **Ch, Eu, Mec, US, Neutral**.
So the directory is the side's name; the game has no mapping of its own from
"team" to "side", and none needs making.

For Dalian_plant that means **team one is Chinese and team two American** — we
had it the other way round, and the flags on the map stood at the wrong points.

### The tab's caption

`Team1NameString` / `Team2NameString` are filled by 0x787260 through 0x787110,
and that function is simply a list:

```
name = gameLogic.teamName(team)
"MEC" -> HUD_TEXT_MENU_SPAWN_ARMY_MEC
"US"  -> HUD_TEXT_MENU_SPAWN_ARMY_USMC
"CH"  -> HUD_TEXT_MENU_SPAWN_ARMY_CHINA
anything else, non-empty -> "HUD_TEXT_MENU_SPAWN_ARMY_" + name
empty -> an empty string
```

So EU falls into the general branch and gives
`HUD_TEXT_MENU_SPAWN_ARMY_EU`. The same function also sets the pair
`FriendlyFlagIconPathString` / `EnemyFlagIconPathString` — its first two
arguments are the player's team and the opposite one.

## The moving corner regions

The wide plate under the health is the node

```
hudBuilder.createPictureNode BottomLeftAnimateHud BottomLeftBar -103 -2 400 39
hudBuilder.setPictureNodeTexture Ingame/Bars/healthBackGround.tga
hudBuilder.setNodeAlphaVariable  MenuBackgroundAlpha
```

It has **no** show variable at all, and `MenuBackgroundAlpha` is 0.7 by default
(the constant 0x3f333333 at 0x46928a, and the same 0.7 stands on the slider in
`HudElementsPlayer.con`). So it cannot be hidden by a flag — and in the original
something else hides it: **the whole region driving away**.

In `Menu/Ingame` these regions' X is not a constant but a graph variable, and
what the file holds is precisely the hidden position:

| Variable | In the file | What it is |
|---|---|---|
| `BottomLeft/BottomLeft_XPos` | -295 | hidden |
| `BottomLeft/BottomLeft_nextXPos` | -295 | hidden |
| `BottomRight/BottomRight_XPos` | 503 | hidden |
| `BottomRight/BottomRight_newXPos` | 503 | hidden |
| `BottomRight/BottomRight_oldXPos` | 201 | shown |

They are driven by `SetVariableSineAction` at speed 600, under the condition
`AniPos && (BottomRight_alpha == BottomRight_oldAlpha || BottomRight_direction)`.

The 400x39 plate covers both the soldier part on the left and the vehicle part
on the right (`BottomLeftSecondaryHealth` in
`Vehicles/HudElementsVehicleBasic.con` starts at x = 149) — which is why on the
spawn screen it looked like a stretched vehicle variant. At X = -295 it is
entirely beyond the screen's edge.

**The source has not been found:** who exactly writes `BottomLeft_nextXPos` and
`BottomRight_direction`. The binding is visible (0x789480 links them to the HUD
object's fields by the template "group + node name"), but the place of the write
is not. For now the regions travel under the same condition as the combat HUD
itself.

### The left region's extended position is not measured

On the right it exists: the original's frame dump gives **336.5**, and three
different nodes agree on it. Note that this is **not** the same as
`BottomRight_oldXPos = 201` in the file — so 201 is some other state (most
likely the one widened for vehicles), while 336.5 is ordinary infantry combat.

On the left there is nothing measured. For the left region the file holds
**only the hidden** position: both `BottomLeft_XPos` and `BottomLeft_nextXPos`
are -295 there. The value -1 that stood in the code was taken from the static
layer `BottomLeftStatic` — it has nothing to do with the moving one, and with
it the `healthBackGround` plate (400x39, shifted by -103 in the node) stretches
to x = 296, covering the space meant for the vehicle bars. On screen that looks
like a vehicle HUD on infantry.

Mirroring the right side is not allowed: the regions are of different widths
(600 against 400) and hold different contents, so carrying the number over
would be fudging.

**What is needed for it:** one frame dump of the original (`Ctrl+Shift+D`) in
ordinary infantry combat. In it we need the rectangle with the
`healthBackGround` texture — its left edge gives the region's X:
`X = left_edge + 400 + 103`, because the dump's coordinates are centred
(`screen = 400 + x`) and the node itself is shifted by -103.

## The corner panels are moved by the graph, not by our progress over inTime

That is the main divergence from the original, and it is not in the curve but in
the **mechanism**.

`Menu/Ingame` (`tools/meme_dump.py Ingame`) holds not only nodes but variables
with actions over them:

```
classes:  SetVariableAction, SetVariableSoftAction, SetVariableSineAction,
          ActionListAction, CullVariableActionNode, AlphaFadeEffect, ...

variables: BottomLeft/BottomLeft_XPos      BottomLeft/BottomLeft_nextXPos
           BottomRight/BottomRight_XPos    BottomRight/BottomRight_NextPos
           BottomRight/Alpha/BottomRight_alpha ... _nextAlpha, _newAlpha, _oldAlpha
           AniPos, BottomLeftAnimate, BottomRightAnimate
```

So the HUD's corner panels travel like this: the file holds a **variable** with
the current X, a second one with the target, and an action drives the first
towards the second. There is no `setNodeInTime` anywhere in that chain — that
one governs the showing of **elements** (`addNodeMoveShowEffect`,
`addNodeAlphaShowEffect`), not the corner layers.

Our `Animator` drives the progress linearly over `inTime`/`outTime` — and that
is not an assumption: that is how `CullNode::iterateUpdate` computes it, see
below.

### What is already known about `SetVariableSoftAction`

`BF2.exe`, the class's factory at **0x833190**:

```
a 0x10-byte object
  +0x0   pointer to the class (0x94cf98)
  +0x4   the variable being driven      (zero at creation)
  +0x8   the target                     (zero at creation)
  +0xc   **Speed**, default 100.0        (0x42c80000)
```

The field's name is not a guess: the class's serialisation (0x8329f0) writes
`+0xc` under the name `"Speed"`. In `Menu/Ingame`'s data the number 10 stands
next to the variables — that is the speed for the corner panels.

The neighbouring `SetVariableSineAction` is a separate class (factory 0x8331c0,
a 0x14-byte object, also with `Speed` defaulting to 100.0).

### The actions' formulas — read from `MemeDll.dll`

Both actions were found by their full C++ symbols in the mod's own library.

`dice::meme::SetVariableSineAction::onEvent` — **0x10001050**:

```
distance = |target - value|
if distance >= "Braking distance":
    step = speed * dt
else:
    step = cos(1.57075 - (distance / braking) * 1.57075) * speed * dt
the value moves towards the target by step, but no further than it
```

`cos(pi/2 - x)` is `sin(x)`: near the target the step decays as a sine, hence
the class's name. The fields: +0xc "Speed", +0x10 "Braking distance"
(`onStream`, 0x1000459d).

`dice::meme::SetVariableSoftAction::onEvent` — **0x10004d2c**: the same thing
**without** the braking stretch, that is linear movement at a constant speed.

**In `Menu/Ingame` the braking distance is zero** for both actions —
`meme_read.py` does not print zero fields, and only `Speed` is visible in both
records. So the corner regions travel linearly, and that is exactly how we drive
them (`obf2::hud::approachVariable`).

### The show progress — `CullNode`

`dice::meme::CullNode::iterateUpdate` (**0x10004a57**) and `iteratePaint`
(**0x1000141a**). The fields: +0xc "In time", +0x10 "Out time", +0x14 the
progress.

The progress is not simply 0..1 but a number with marker states:

```
-4  just created, not started yet
 0..1  showing: progress += dt / "In time"
 2  fully shown
 1..0  hiding: progress -= dt / "Out time"
-1  hidden
```

A zero `In time` makes the node appear instantly (the progress goes straight to
2); the same for `Out time`.

Drawing (`iteratePaint`):

```
progress >= 1  -> the children draw with the parent's pipe, **unchanged**
progress <= 0  -> the children do not draw
otherwise      -> a new pipe, in it alpha = parent's * progress,
                  and the direction (+0x10) = +1 while showing, -1 while hiding
```

From which something important follows: **the alpha is multiplied by the cull
node itself**, not by `addNodeAlphaShowEffect`. A node with `setNodeInTime` but
no alpha effect still fades — simply because it is under a cull node. Until now
we multiplied by the progress only when an alpha effect was present, and, for
example, the time bar (`TimeItems`, which has only a move effect) drove in for
us but did not fade in.

## What exactly is animated — read from `Menu/Ingame`

`tools/meme_read.py Ingame` takes the file apart completely (3673 of 3673
bytes), and every number comes from there, not from screenshots:

**The corner regions — movement.** Both are driven by `SetVariableSineAction` at
speed **600**:

```
SetVariableSineAction {Speed: 600}
  Variable: FloatData "BottomLeft/BottomLeft_XPos"     -295
  Data:     FloatData "BottomLeft/BottomLeft_nextXPos" -295

SetVariableSineAction {Speed: 600}
  Variable: FloatData "BottomRight/BottomRight_XPos"    503
  Data:     ToggleData "BottomRight/BottomRight_NextPos"
              Toggle: BoolData "BottomRight_direction"
              Data 1: FloatData "BottomRight_newXPos"   503
              Data 2: FloatData "BottomRight_oldXPos"   201
```

The hidden position on the right is 503, and it really does come from the file.

**But 201 is not the extended position.** It is the initial value of a variable
the game rewrites while it runs, just as on the left it rewrites
`BottomLeft_nextXPos`. The extended one was measured from the original's frame
dump (`Ctrl+Shift+D`) and equals **336.5**: three different nodes agree on it
(BottomRightBar 301 -> 637.5, ShotSelect 449 -> 785.5, an unnamed 16x10
431 -> 767.5), and it is the same in all three captured frames.

I tried replacing it with 201 "because that is what the file says" — and that
was a mistake we had already made once: with 201 the ammo plate sits 135 pixels
further left than in the original. **A measurement of the original outranks a
variable's initial value in the file**, because the game rewrites the variable.

On the left both fields in the file are -295: the extended position is not
written there, the game writes it itself into `BottomLeft_nextXPos`. That
variable is registered by the HUD's code (`BF2.exe`, 0x789480 — the same place
has `BottomLeft_XPos`, `BottomLeft_nextXPos`, `BottomLeft_alpha1/2`,
`BottomLeft_nextAlpha1/2`), but **the place of the write has not been found
yet**, so the left extended position stays unmeasured.

**The corner regions — alpha.** It is driven by a different action,
`SetVariableSoftAction` at speed **10**, four of them:

```
SetVariableSoftAction {Speed: 10}  BottomLeft_alpha1  <- BottomLeft_nextAlpha1
SetVariableSoftAction {Speed: 10}  BottomLeft_alpha2  <- BottomLeft_nextAlpha2
SetVariableSoftAction {Speed: 10}  BottomRight_alpha  <- ToggleData(newAlpha 1.0, oldAlpha)
```

So in the original a region does not merely travel — it also **fades in**, and
along two different curves: `Sine` for the movement, `Soft` for the alpha. We do
not animate the corner regions' alpha at all.

## The formulas — from `MemeDll.dll`, which has full symbols

`MemeDll.dll` sits in the mod's directory next to the editor and **exports full
C++ symbols**. So the animation curves need not be guessed — they can be read
directly.

### The show progress: `CullNode::iterateUpdate` (0x10004a57)

`CullNode`'s fields: `Data` (the show condition), **`In time`**, **`Out time`**
— exactly what `setNodeInTime` / `setNodeOutTime` write. The progress is kept in
the node itself, and it moves **linearly**:

```
showing: progress += dt / "In time"    until 1, then set to 2.0
hiding:  progress -= dt / "Out time"   until 0, then set to -1.0
progress <= 0 — the node is not drawn at all
```

The markers 2.0 and -1.0 mean "already shown" and "already hidden"; on them the
node sends events (0x11/0x0d at the start of showing, 0x0e at the end,
0x12/0x0f at the start of hiding, 0x10 at the end).

So our linear progress over `inTime`/`outTime` is **correct**.

### Movement: `MoveEffect::picturePaint` (0x10001b27)

```
angle    = "Move direction"
length   = "Move length"
progress = EffectPipe+0x18
offset   = (1 - progress) * length
dx = -cos(angle) * offset
dy = +sin(angle) * offset
```

**Both of our signs were the opposite** — it stood as `(+cos, -sin)`, derived
from the reasoning "the voting panel has to drive in from below". The reasoning
did not survive the check: the elements flew in from the opposite side to the
original's. Fixed.

### Driving a variable: `SetVariableSoftAction::onEvent` (0x10004d2c)

```
current = Variable->getFloat()
target  = Data->getFloat()
step    = dt * "Speed"
move towards the target by step, clamped at the target
```

So "Soft" is a **linear approach**, not an exponential decay.

### `SetVariableSineAction::onEvent` (0x10001050)

It inherits `SetVariableSoftAction` and adds the field **"Braking distance"**:

```
remaining = |target - current|
if remaining >= "Braking distance":  step = dt * "Speed"
else:                                step = sin(remaining/"Braking distance" * pi/2) * "Speed" * dt
```

So linear while far away, and a sine decay on approach.

**In `Menu/Ingame` "Braking distance" equals zero** (the reader hides zeroes,
and the file parses completely — 3673 of 3673 bytes). So the corner regions
travel at a flat 600 units per second, without decay — exactly as we do it.

### Alpha: `Bf2AlphaShowEffect` (`BF2.exe`, 0x834f70)

This class is not in the DLL — it is in the game itself, and its drawing comes
down to one thing:

```
alpha *= EffectPipe+0x18     (that is, by the show progress)
```

Four times in a row, one per vertex of the quad (offsets 0x58, 0x78, 0x98, 0xb8
in `PaintData`). The field `EffectPipe+0x18` is the same one `MoveEffect` takes
its progress from.

Our `alpha *= progress` is correct, and now that is not an assumption.

The effect is created by `hudBuilder.addNodeAlphaShowEffect` (0x79db80): a
4-byte object with the table 0x933400, attached to `%sCullNode`.

## Summary: what of the animation is now cross-checked against the original

| what | source | state |
|---|---|---|
| the show progress is linear over `In time`/`Out time` | `CullNode::iterateUpdate`, 0x10004a57 | matches |
| the offset `(-cos a, +sin a) * length * (1 - progress)` | `MoveEffect::picturePaint`, 0x10001b27 | **had the opposite signs, fixed** |
| `alpha *= progress` | `Bf2AlphaShowEffect`, 0x834f70 | matches |
| corner regions: flat movement at 600 u/s | `SetVariableSineAction::onEvent`, 0x10001050, "Braking distance" = 0 | matches |
| corner regions' alpha: approach at 10 u/s | `SetVariableSoftAction::onEvent`, 0x10004d2c | **not reproduced** |

The last row is what is left: in the original the corner regions do not merely
travel, they also fade in, and four separate actions drive them
(`BottomLeft_alpha1`, `_alpha2`, `BottomRight_alpha`). The HUD itself writes
their targets into `nextAlpha*`, and **where it takes the values from has not
been found yet**.

## The plates' alpha: the mechanism is found, the values are not

Nodes take their alpha from a variable — the `setNodeAlphaVariable` command in
the HUD's data. How many times each variable is used (`Menu_client.zip`,
`HUD/HudSetup/*.con`):

```
34  HitIndicatorIconAlpha
16  BottomLeftVehicleFadedAlpha
14  BottomRightAlpha
11  BottomLeftVehicleAlpha
 6  BottomLeftHealthAlpha        6  BottomRightFadedAlpha
 5  BottomLeftHealthFadedAlpha
25  MenuBackgroundAlpha          (the plates, from the player's profile)
```

All these variables are registered by the HUD's code (`BF2.exe`, 0x789480) as
fields of its object:

```
BottomLeftHealthAlpha        -> +0x18c
BottomLeftVehicleAlpha       -> +0x190
BottomLeftHealthFadedAlpha   -> +0x194
BottomLeftVehicleFadedAlpha  -> +0x198
```

**Of these we know only two** — `MenuBackgroundAlpha` and `MenuMapAlpha` (they
come from the player's profile). The rest return "I do not know", and the node
stays fully visible. The consequence is visible on screen: the health bars and
the vehicle bars are drawn **at the same time**, because neither is dimmed.

What is missing: **who writes those fields and when**. That is no longer the
graph or the data but the HUD's logic in the client, and we have not read it
yet. Until then we do not invent values: putting "in a vehicle Health = 0"
without a source is exactly the fudging we were broken of here.

Incidentally that explains the long-standing complaint "the background plate
under the health is extended as if I were in a vehicle": it ought to be dimmed
by a variable we do not compute.

## Who drives the left region: `BF2.exe`, 0x78b600

That is the very logic we were missing. The region has **three** positions, and
all three are set by the HUD object's constructor (0x78c560):

| field | value | what it is |
|---|---|---|
| +0x178 | `0xc3938000` = **-295** | hidden |
| +0x17c | `0xc3090000` = **-137** | on foot |
| +0x180 | `0x42580000` = **54** | in a vehicle |
| +0x184 | | `BottomLeft_XPos` — the current one |
| +0x188 | | `BottomLeft_nextXPos` — the target |
| +0x18c | | `BottomLeftHealthAlpha` |
| +0x190 | | `BottomLeftVehicleAlpha` |
| +0x194 | | `BottomLeftHealthFadedAlpha` |
| +0x198 | | `BottomLeftVehicleFadedAlpha` |

The binding to the graph's variables is visible at 0x789480:
`LEA EDX,[ESI+0x188]` before registering `BottomLeft_nextXPos` (0x7895f5) and
`LEA EDX,[ESI+0x184]` before `BottomLeft_XPos` (0x78963f).

The function 0x78b600 picks the target position every frame:

```
if XPos == hidden            state = 0
if HealthAlpha  == 1         state = 1
if VehicleAlpha == 1         state = 2

state 0: if VehicleAlpha == 0 -> target = on foot;
         not arrived and HealthAlpha == 0 -> target = hidden
state 1: not arrived at "on foot" -> target = on foot; otherwise show the health
state 2: arrived at "on foot" and HealthAlpha == 1 ->
             not arrived at "in a vehicle" -> target = in a vehicle
```

And in the same place — the formula for the dimmed alphas:

```
base = (a menu is up and state != 0) ? MenuBackgroundAlpha : 1.0
HealthFadedAlpha  = clamp(HealthAlpha  - (1 - base), 0, 1)
VehicleFadedAlpha = clamp(VehicleAlpha - (1 - base), 0, 1)
```

**What this fixed.** Our left extended position was the placeholder -1, and the
`BottomLeftBar` plate (400 wide, offset -103) stretched to x = 296 — its full
width, as if the player were in a vehicle. The correct on-foot position is
**-137**, and the plate reaches 160.

What is left: the flags `BottomLeftHealthAlpha` / `BottomLeftVehicleAlpha`
themselves — who sets them to 0 and 1 has not been worked out yet. Until then we
take the "on foot" position and do not switch to the vehicle one.

## An effect belongs to the subtree, not to its node

An effect command does not decorate the node it is written under. The builder
(`Menu/Bf2HudBuilder.cpp`, 0x79b2c0 and 0x79db80) looks up that node's
`<name>CullNode` and hangs a graph node on it, and everything below a cull node
draws **through** it:

* `dice::meme::CullNode::iteratePaint` (`MemeDll.dll`, 0x1000141a) — at progress
  1 the children draw with the parent's pipe unchanged, at 0 they do not draw at
  all, and in between they get a new pipe whose alpha is the parent's times the
  progress;
* `dice::meme::MoveEffect::picturePaint` (0x10001b27) offsets that pipe.

So a move effect on a split node moves everything under it, and two nested
groups multiply their alphas and add their offsets.

This is not a corner case — it is where nearly every effect in the game's data
lives. Of the 84 `addNodeMoveShowEffect` and 137 `addNodeAlphaShowEffect` calls
in `Menu_client.zip`, almost all sit on a group: `SpawnInfo`, `Kit0NotSelected`,
`Kit0Selected`, the corner regions, the vote panel. A picture with an effect of
its own is the exception.

While we applied an effect to its own node and no further, **almost nothing on
the screen moved or faded**. The plain case: `SpawnInfo` carries
`addNodeMoveShowEffect -1.57 50` and `setNodeInTime 0.3`, and the bar under it,
`TopMiddleBar`, stayed at y 0 through every rebuild — twenty-five of them in a
sixty-frame run.

In our code `updateAnimator` (`src/hud/src/render.cpp`) walks the tree carrying
what the ancestors add up to, and `Animator::compose` puts the two together:
alphas multiply, offsets add, and the progress stays the node's own, because
that is what decides whether the node is drawn at all — the walk has already
stopped at an ancestor that is not.
