# The HUD's bottom right: the object, its variables, the movement machine

This is the object that holds ammo, the crosshair, reloading, the laser,
voice and the target marker — and it is also the one that travels along
the bottom right as `BottomRightAnimate`.

Addresses in `BF2.exe`:

| what | address |
|---|---|
| constructor | 0x7a5b10 |
| variable registration | 0x7a62c0 (writes the method table at 0x93407c) |
| update from the weapon | 0x7a85f0 |
| "hide" | 0x7a8280 |

## Three positions, not two

The constructor puts three numbers into fields +0x1c, +0x20, +0x24 — the
same as on the left (0x78c560, fields +0x178..+0x180):

```
+0x1c  0x43fb8000 = 503   hidden
+0x20  0x43a88000 = 337   on foot
+0x24  0x43250000 = 165   in a vehicle
```

The frame dump of the original gave 336.5 — that is the same 337: the dump
measures quad corners and the whole HUD is drawn with a half-pixel offset
(same file: `MapFrame` 595.5 against 596 in the data).

The animation's current endpoints sit next to them: +0x28
(`BottomRight_oldXPos`, the constructor puts 165 there) and +0x2c
(`BottomRight_newXPos`, 503).

## The movement machine

The engine does **not** move the region itself. It drives three variables,
and the region travels through the `Menu/Ingame` graph
(docs/formats/hud-meme-graph.md):

| graph variable | field | what it is |
|---|---|---|
| `BottomRight/BottomRight_oldXPos` | +0x28 | the shown position |
| `BottomRight/BottomRight_newXPos` | +0x2c | the hidden position |
| `BottomRight/BottomRight_direction` | +0x18 | show or not |
| `BottomRight/Alpha/BottomRight_alpha` | +0x10 | opacity (also `BottomRightAlpha`) |

The binding is done by 0x7a62c0: the first four calls create reference
data in the graph (`FloatRefData` is 0x14 wide, `BoolRefData` 0x10) with a
name and a path, and put **a pointer to the object's field** inside them.
So the graph's variable and the HUD's field are the same cell.

"Hide" is visible verbatim (0x7a8280):

```
[this+0x2c] = [this+0x1c];   // newXPos = 503
[this+0x18] = 0;             // direction = 0
[this+0x13a] = 0;
if (argument) [this+0xe9] = 0;   // CrosshairActive
```

What the graph makes of that is in the "The right-hand region" section of
docs/formats/hud-meme-graph.md.

## All the variables 0x7a62c0 registers

A hundred and twenty-one, from `PrimaryAmmo` to `HasMissileConnection`.
The list with types and field offsets lives in one place for every HUD
object: docs/functions/hud-variables.md, section "0x7a62c0".

## What is still missing here

* `BottomRightFadedAlpha` (+0x14) — where it is computed has **not been
  found**. On the left the formula is visible at the end of 0x78b600
  (`Faded = clamp(Alpha - (1 - MenuBackgroundAlpha), 0, 1)`); no such
  place has turned up on the right, so we do not compute it.
* Who sets `direction` to **one**. "Hide" was found (0x7a8280), its
  counterpart "show" was not. We show it when there is a player — the same
  rule as on the left (0x78b870).
