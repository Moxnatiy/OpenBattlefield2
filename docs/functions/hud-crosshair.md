# The crosshair

The on-foot crosshair is not one picture. It is a weapon's own HUD group — four
8×8 ticks around the centre — shown by `GuiIndex` and moved by four
`Crosshair*Pos` variables the engine computes from the weapon's deviation.
Addresses are `BF2.exe` unless named otherwise; the evidence IDs are REA's
(CLAUDE.md, "REA").

## What the game's data builds

`Menu_client.zip:HUD/HudSetup/Weapons/HudElementsGenericWeapon.con`:

```
createSplitNode   IngameHud GenericWeaponHud
setNodeLogicShowVariable EQUAL GuiIndex 5 / OR GuiIndex 52
  GenericHitIndication 384 284 32 32   vsp_HitIndicator.tga  — HitIndicatorIconShow/Alpha
  createSplitNode GenericWeaponHudtemp   EQUAL GuiIndex 5
    rgb CrossHairColorRed/Green/Blue, alpha CrossHairColorAlpha
    GenericCrosshairUp    398 285 8 8  vsp_CrossHair_single.tga        setNodePosVariable 1 CrosshairUpPos
    GenericCrosshairDown  393 315 8 8  rotation 180                    setNodePosVariable 1 CrosshairDownPos
    GenericCrosshairLeft  385 294 8 8  rotation 90                     setNodePosVariable 0 CrosshairLeftPos
    GenericCrosshairRight 415 299 8 8  rotation 270                    setNodePosVariable 0 CrosshairRightPos
```

The M4 (`GuiIndex` 4/53) and the Jackhammer (9) have the same four ticks under
their own groups; the other weapons (`HudElements*.con`) draw fixed pictures. A
weapon names its group with `ObjectTemplate.weaponHud.guiIndex` in its
`.tweak`; 128 of the handheld weapons set it.

## Who writes `GuiIndex`

`GuiIndex` is the player object's +0xa0 (registered at 0x789480,
docs/functions/hud-variables.md). It is written by **0x78b340**
(ev_b8feb88e…, called from 0x78b870, 0x78bfd0 and 0x78d940):

* no local player (`[0x00a08f60]->+0x30`), the object's +0x170 clear, or the
  player not alive (`+0x68`) → **0**;
* the player's controlled object (`+0x44`; when it is the player's own `+0xa0`
  object, `[0x00a10890]->+0x90` may stand in) has a HUD component at its +0x54
  (a vehicle) → that component's `getGuiIndex(flag)`, unless the soldier's
  `+0x130` says otherwise;
* otherwise (on foot) → the active item (`soldier->+0x74(0)`, skipped when bit
  0 of its +4 is set), its `WeaponHudComp` at +0x5c, `getGuiIndex(flag)`;
* `flag` is `!(this+0x244 == 0 && this+0x2ac != 0)` — purpose of the two
  fields not established.

It then copies the value to +0xa4, and when +0x2a0 is 1 or 2 sets the HUD
variable `CrossHairColorAlpha` to 1.0 through the information layer
(`[0x00a10890]->+0x244`, its +0x28) and clears +0x2a0.

`WeaponHudComp::getGuiIndex(bool)` (Linux 0x5c3b10): with the flag set and the
owner's vfn +0x258 true it returns 0; otherwise the template's `guiIndex`, or
under a zoom condition (owner +0x190, the parent's projectile template — not
taken apart) its `altGuiIndex`.

### `WeaponHudComp`, slot by slot

`tools/linuxded/vtable.py dice::hfe::world::WeaponHudComp --msvc`:
+0x38 `getGuiIndex(bool)`, +0x3c `getCrossHairIcon`, +0x40 `setLockState`,
+0x44 `playOverheatSound`, +0x48 `getShowOnCamMode`, +0x4c `getHudIcons`,
+0x50 `getHudInfo`, +0x58 `getLockState`.

### `WeaponHudCompTemplate`: the fields and their defaults

The constructor (Linux 0x5c4f00) and the setters give the order; the client
reads the same fields 0x3c further on (its `getHudInfo` block):

| `.con` property | Linux | client (`getHudInfo` +) | default |
|---|---|---|---|
| `weaponHud.guiIndex` | +0x50 | +0x8c | 5 |
| (`altGuiIndex`, by position) | +0x54 | +0x90 | −1 |
| `weaponHud.deviationFactor` | +0x58 | +0x94 | 10 |
| `weaponHud.maxVisualDeviation` | +0x5c | +0x98 | 1000 |
| `weaponHud.minVisualDeviation` | +0x60 | +0x9c | 0 |
| `weaponHud.crosshairIconSize` | +0x64 | +0xa0 | 32 |

The client's bools at +0xb4..+0xbb (has HUD, `hasRangeFinder`, …) are read by
0x7a85f0 below; their names by position are not established.

## The ticks: 0x7a5690

The bottom-right object (constructor 0x7a5b10, docs/functions/hud-bottom-right.md)
is updated by 0x7a9980, which calls **0x7a85f0** (ev_1272f1ae…) with the
player's controlled object. 0x7a85f0 copies `deviationFactor`, `max` and
`minVisualDeviation` into its +0x100, +0x104, +0x108 and calls **0x7a5690**
(ev_061770d6…) with the weapon (`IWeaponObject`, IID 0x94c0):

```
d   = trunc(ceil(weapon.getDeviation() × factor))   // IWeaponObject +0xc4, ceil = msvcrt import 0x87f5f4
cx  = trunc(ceil(hudView.width  × 0.5))             // 400 — the HUD's view, +0x54
cy  = trunc(ceil(hudView.height × 0.5))             // 300 — +0x5c
s   = trunc(+0xf8)                                  // 8.0, constructor 0x7a5c66: the tick's size
off = min + d, but max when that is larger          // 0x7a5752: cmp (min + d) against max
CrosshairUpPos    (+0x10c) = cy − s − off
CrosshairDownPos  (+0x110) = cy + off + 1
CrosshairLeftPos  (+0x114) = cx − s − off − 1
CrosshairRightPos (+0x118) = cx + off
```

The branch that does not clamp (0x7a57a6) writes the same with the literals
300, 301, 399 and 400 in place of `cy`, `cy + 1`, `cx − 1` and `cx`.

`hudView` is the HUD manager's +0x678 (read by 0x7ad390): a `RenderView`
(`RendDX9.dll`, the same camera class as docs/functions/render-camera.md,
created through `[0x9ff960]->+0x30` with CID 0xc844d / IID 0xc83e9 at
0x7ad067). 0x7ad450 makes it orthographic and sets near 0.01, far 1.0, width
800.0 (+0x50 → +0x34) and height 600.0 (+0x58 → +0x38): the HUD's own
800×600 space, not the window.

`getDeviation` is `GenericFireArm::getDeviation()`
(`tools/linuxded/vtable.py dice::hfe::world::GenericFireArm --msvc`, the
`IWeaponObject` vtable: +0xc0 `updateDeviation(PlayerInput, bool)`, +0xc4
`getDeviation`, +0xc8 `getMinDeviation`, +0xcc `getMaxDeviation`).

## Everything else 0x7a85f0 fills from the weapon

| field | variable | from |
|---|---|---|
| +0x17c | `PrimaryWeaponNameString` | `getHudInfo` +0x4 (string) |
| +0x1a0 | `CrosshairIconPathString` | `getCrossHairIcon` |
| +0x154 / +0x158 | `PrimaryAmmoBarFront/BackPathString` | `getHudIcons` +0xac / +0xc8 |
| +0x150 / +0x14c | `PrimaryAmmoIconPathString` / `PrimaryClipPathString` | `getHudIcons` +0x100 / +0xe4, chosen by whether the weapon has magazines |
| +0x1a4 | `FireRateIconPathString` | 0x7a5560 with the weapon's fire rate (`+0x14`) when `getHudInfo` +0xb6 |
| +0xe4 | `FiringIndex` | `getGuiIndex` again, when the weapon can fire (`+0x1c`) |
| +0xb4 | `PrimaryAmmo` | rounds ÷ magazine size (`+0xac` / `+0xa8`) |
| +0x144 | `PrimaryAmmoString` | the round count, formatted |
| +0xf4 / +0xed | `PrimaryHeatValue` / `PrimaryHeatShow` | `+0x24` |
| +0xea | — | `+0x108` non-zero |

## The colour

`CrossHairColorRed/Green/Blue/Alpha` are the information layer's +0x178..+0x184.
Its constructor (0x468dd0, the same function that registers the variables)
puts **1.0, 1.0, 0.0, 1.0** there (0x469294..0x4692b4; `ebx` is zeroed at
0x468e1b): yellow. 0x78b340 above sets the alpha back to 1.0 when its +0x2a0
asks; who else writes the four is not established.

The variables sit on the **group** (`GenericWeaponHudtemp`, `M4CrossHud`), and
the ticks have no colour of their own. They are yellow because a node's colour
is a graph node slotted above everything the node holds:

* `setNodeColor` (the console handler at 0x76abff → 0x76ac49),
  `setNodeRGBVariables` (0x79bc40) and `setNodeAlphaVariable` (0x79ba80) all look
  up `<node>ColorEffect` (format string 0x932fc4) through the graph's name table
  (0x82a300) and type-check it (0x795350: `dice::meme::Bf2EffectNode`, type
  object from 0x7c20d0; 0x795410: `dice::meme::Bf2ColorEffect`, from 0x7c2440);
* when there is none, the two variable commands first call **0x79a210** with
  (1, 1, 1, 1) — `setNodeColor`'s own body — and try again. 0x79a210 finds the
  node's own graph object by its name (0x82a300, cast by 0x794f10), makes a
  `Bf2ColorEffect` (0x7c2110, 0x30 bytes) wrapped in a `Bf2EffectNode` named
  `<node>ColorEffect` (0x7c2040), takes what the node held in its slot 1
  (`+0x44(1)`), hangs it under the effect (`effect->+0x4c(0, it)`) and puts the
  effect in the slot (`node->+0x4c(1, effect)`). From then on the whole subtree
  is painted through it.

`Bf2ColorEffect` (vtable 0x936020; ev_1b758138…):

| slot | address | what |
|---|---|---|
| +0x40 | 0x7c2190 | `setColor(r, g, b, a)` → +0x04..+0x10 |
| +0x4c | 0x7c21f0 | paint (six arguments, the fifth the paint target) |
| +0x50 | 0x7c22c0 | paint (four arguments) |
| +0x58 | 0x7c21d0 | `setAlphaVariable(float*)` → +0x18 |
| +0x5c | 0x7c2380 | `setRGBVariables(r*, g*, b*)` → +0x1c, +0x20, +0x24 |

Both paints do the same thing: start from the colour at +0x04..+0x10; when the
alpha variable is set and its value is **0 or more**, multiply the alpha by it;
when a red, green or blue variable is set and its value is 0 or more, **replace**
that channel with it (the test is `fcomp` against 0.0 at 0x93c850, skipped when
below). The result goes to 0x771fa0, which **writes** it into the paint target's
four corner colours (+0x4c, +0x6c, +0x8c, +0xac, a stride of 0x20), and the base
paint (0x82d810 → 0x82d730, 0x82d840 → 0x82d790) goes on to the children with
that target. So the nearest colour effect up the tree decides a node's colour
whole — alpha included, not multiplied with an outer one — and a node with no
effect anywhere above it keeps the target's starting colour.

What this does to the rest of the HUD (a walk of `Menu_client.zip`'s `HUD/`,
1661 nodes): 87 drawn nodes have no colour of their own under one of 20 effects.
Besides the crosshairs (`GenericWeaponHudtemp`, `M4CrossHud`, `JackhammerHud`,
`M203Hud`) they are the commander's radio text (0.82, 0.82, 0.61), the map menu
buttons (alpha 0.7), the selected kit rows (white) and two groups on
`MenuBackgroundAlpha`: `VoipIcon` and `MinSizeAlpha` — the minimap's frame and
compass, which therefore draw at the HUD transparency of the player's profile.

## The deviation: `SoldierDeviationComp::updateDeviation`, 0x60ad60

`GenericFireArm::getDeviation` hands over to its deviation component (Linux
+0x2c8). A soldier's handheld weapon has a `SoldierDeviationComp`; the client's
is found by its `getMaxDeviation` (0x60a810, the one function summing five
template fields and multiplying by a sixth) and the `IWeaponDeviationComp`
vtable around it (0x8f6784): +0x88 `updateDeviation` **0x60ad60**
(ev_d6197e8c…), +0x98 `getMinDeviation` 0x60a800, +0x9c `getMaxDeviation`
0x60a810, +0xa0 `getDeviation` 0x60a7a0 — the same slots as the server's
(`tools/linuxded/vtable.py dice::hfe::world::SoldierDeviationComp --msvc`,
the second vtable).

### The template, `ObjectTemplate.deviation.*`

The client's template is the component's +0x40. Its fields are the server's
minus 8 (the server's setters: `setFireDev` Linux 0x5ef230, `setTurnDev`
0x5f1060, `setSpeedDev` 0x5f1080, `setMiscDev` 0x5f10a0; `getMaxDeviation`
0x5f1030 against the client's 0x60a810):

| property | argument | client field | what 0x60ad60 does with it |
|---|---|---|---|
| `setFireDev a b c` | a | +0x10 | in `getMaxDeviation` only |
| | b | +0x08 | added per shot (`postFire`, not taken apart) |
| | c | +0x0c | the shot accumulator's fall per update |
| `minDev` | | +0x14 | the floor of the sum |
| (`subProjectileDev`, by position) | | +0x18 | not used here |
| `setTurnDev a b c d` | a | +0x2c | the turn accumulator's ceiling |
| | b | +0x20 | × \|mouse Y\| (axis 5) |
| | c | +0x24 | × \|mouse X\| (axis 4) |
| | d | +0x28 | its fall per update |
| `setSpeedDev a b c d` | a | +0x3c | the speed accumulator's ceiling |
| | b | +0x30 | added while throttling (axis 3, W/S) |
| | c | +0x34 | added while strafing (axis 0, A/D) |
| | d | +0x38 | its fall per update |
| `setMiscDev a b c` | a | +0x48 | the misc accumulator's ceiling |
| | b | +0x40 | added while the first flag is set |
| | c | +0x44 | its fall per update |
| `devModStand` | | +0x4c | pose 0 |
| `devModCrouch` | | +0x50 | pose 1 |
| `devModLie` | | +0x54 | pose 2 (and `getMinDeviation` = `minDev × devModLie`) |
| `devModZoom` | | +0x58 | while zoomed (`setZoom`, the component's +0x34) |

The names of the four mods and `minDev` are by position: they have no setters
of their own on the server, and their order in the template is the order the
three getters and 0x60ad60 read them in.

### One update

Arguments: the soldier's `PlayerInput` by value (an axis counts only when its
bit is set in the input's mask: 1 = axis 0, 8 = axis 3, 0x10 = axis 4, 0x20 =
axis 5), a first flag, and a second — `GenericFireArm::getFireState() != 0`
(Linux 0x5e2fc0 → 0x5e2d90). Who passes the first is **not established**.

Only when the weapon's owner is a soldier (`0x71ad80`, its +0x14 class 0x9493):

```
speed  : add = (|throttle| > 0.01 ? T.speedB : 0) + (|strafe| > 0.01 ? T.speedC : 0)
         add == 0      → acc −= T.speedD, not below 0
         acc ≥ T.speedA → acc −= T.speedD, not below T.speedA
         otherwise     → acc += add, not above T.speedA
turn   : add = |mouseY|·T.turnB (when > 0.01) + |mouseX|·T.turnC (when > 0.01)
         add == 0      → acc −= T.turnD, not below 0
         acc ≥ T.turnA → acc −= T.turnD, not below T.turnA
         otherwise     → acc += add − T.turnD, not above T.turnA, not below 0
misc   : flag clear or T.miscB == 0 → acc −= T.miscC, not below 0
         acc ≥ T.miscA → acc −= T.miscC, not below T.miscA
         otherwise     → acc += T.miscB − T.miscC, not above T.miscA, not below 0
pose   : the soldier's pose (+0x15c, +0x38): 0 → devModStand, 1 → devModCrouch,
         2 → devModLie (3, swimming, falls to stand); "Unknown pose type" past 3
zoom   : zoomed ? devModZoom : 1
```

Then, owner a soldier or not: unless the second flag (firing) is set the shot
accumulator falls by `T.fireC`, not below 0, and

```
deviation = (T.minDev + misc + speed + turn + shot) × zoom × pose
```

plus the component's +0x3c when its owner's +0x170 → +0x64 → +0x60 says so
(an AI's deviation, `setAIDeviation`, by its name on the server).

The accumulators live at the component's +0x24 (speed), +0x20 (turn), +0x28
(misc), +8 (shot); the pose mod at +0x2c, the zoom mod at +0x30, the result
at +4 — all relative to the `IWeaponDeviationComp` part the vtable is called
on.

### Who calls it, how often, and with what

The client's `GenericFireArm::updateDeviation` is 0x5e1260 (its
`IWeaponObject` slot +0xc0): it copies the input, pushes
`getFireState() != 0` and its own flag, and calls the component's +0x88. Its
callers are two (found by the by-value copy of the 0x118-byte input before a
`call [reg+0xc0]`):

* **0x5af516, the end of `Soldier::handlePlayerInput`** (0x5adea0, 5.9 KB,
  ev_4e9f62a6…): the soldier's +0x1b8, its +0xc, the weapon — once for every
  input the soldier plays, which is once per tick;
* 0x5f8254, with the flag 0 (0x5f81d0, not taken apart).

The flag `handlePlayerInput` passes is `[ebp+0x17] || [ebp+0xf]`:

* `[ebp+0x17]` — the jump: `c_PIAction` (axis 9) when the mask carries it, zeroed
  while the jump timer +0x3dc runs and when the soldier's +0x2d4 is set
  (0x5ae2a3..0x5ae2db);
* `[ebp+0xf]` — `c_PILie` (axis 37, the mask's second word) non-zero (0x5ae2fa);
* both cleared when the soldier's +0x28 is set (`local_45`, 0x5ae304) — by its
  use elsewhere, the vehicle he sits in.

So the misc term is the jump and the lie-down. The axis numbers are
`tools/control_constants.py`'s.

## Which weapon is out after a spawn

The server never ghosts our own soldier's state to us — a live run counted 0
of his records — so his 0x1000 never arrives. The weapon is the one
`Soldier::pickupKit` makes active (Linux 0x54da70): when nothing is active yet
(+0x358 == -1) it walks the inventory from slot 3 up and activates the first
occupied one (`enableItem`, then `setActiveWeapon(item, 0)` when it is a weapon,
0x54dbe7..0x54dc24); with nothing from 3 up, the first of 0..2. For US
Specops that is slot 3, `usrif_m4`.

## Ours

`obf2::game::weapon_hud` (the templates, 0x60ad60, 0x7a5690) and
`obf2::app::Crosshair` (the weapon out, one update per action we send, the HUD's
numbers); `test_weapon_hud`. On `--connect` the ticks stand on screen around the
centre, spread while running and close again after (`docs/screenshots`, the
crosshair pass), and they are yellow: the colour effect is `Node::colorEffect`,
resolved down the tree by `Builder::finish` and read in `buildNode`
(`testColorEffectColoursTheSubtree`); `--hud-rects` prints each piece's tint,
and on the live server the M4's four ticks carry (1, 1, 0, 1).

Not done:

* weapon switching (`c_PIWeaponSelect*`), zoom, firing and `postFire`, the
  jump's vetoes, `altGuiIndex`, and unlocked weapons (`ItemContainer`) — a kit's
  slot is its plain child's;
* `--hosted` has no crosshair: our own server does not run the weapon.
