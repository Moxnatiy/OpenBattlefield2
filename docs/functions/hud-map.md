# The map: size, position and where `MapFullSize`/`MapMinSize` come from

The map in the HUD is not "a minimap plus a separate big map" but **one node
with its own target/current pair**. The HUD's state number sets the target,
and the size travels towards it by smoothing; both show variables are derived
from the current size, not from the state.

Because of that, in the original the minimap ↔ big map transition is smooth,
and the frames (`MinSizeAlpha`, `MapFrameOpen`) do not switch instantly:
while the size is in transit **neither** of the two variables is on.

Sources: `BF2.exe`, 0x777dc0 (state → target) and 0x77c330 (the step and the
derivation). In the code — `src/hud/include/obf2/hud/map_node.h`.

## The map node's fields

| offset | what it is | from |
|---|---|---|
| +0x68c | `MapFullSize` | derived from the size, 0x77d3f8 |
| +0x68d | `MapMinSize` | the same place |
| +0x68e | commander mode, also the index into the zoom tables | |
| +0x690 | spawn-screen mode | 0x777f9a |
| +0x691 | commander mode | 0x7781b0 |
| +0x692 | menu mode (states 0x11/0x12) | 0x778299 |
| +0x694 | `1 - target height / screen height` (the other way round for the commander) | 0x77cf10 |
| +0x698 | the current zoom; +0x6d0 is its index | |
| +0x776 | whether the size has arrived (as of last frame) | |
| +0x778, +0x77c | target: width and height | |
| +0x780, +0x784 | target: X and Y | |
| +0x788, +0x78c | `setMiniPos` | 0x75f319 |
| +0x798, +0x79c | `setMiniSize` | used at 0x777e4f |
| +0x7a8, +0x7ac | `setCommanderPos` | |
| +0x7b0, +0x7b4 | `setCommanderSize` | |
| +0x7b8, +0x7bc | `setMaxiSize` | 0x75f423 |
| +0x7c0, +0x7c4 | `setMaxiPos` | 0x75f555 |
| +0x7f8 | "snap at once", without smoothing | |

The current position and size live not in the node itself but in shared cells
(`FUN_0065ddd0` — position, `FUN_00659990` — size). Half of an 800×600 screen
is added to the position: `setMiniPos 197/-300` is (597, 0), and the
`MapFrame` frame stands at (596, 0).

## HUD state → target (0x777dc0)

| state | target |
|---|---|
| 0 — combat | `setMiniPos` / `setMiniSize` |
| 1 — spawn screen | `setMaxiPos` / `setMaxiSize`, fixed alpha (1.0, icons 0.5) |
| 2 — big map | the same, but the alpha from the settings `MenuMapAlpha`, `MenuMapIconAlpha` |
| 15 (0xf) — commander | `setCommanderPos` / `setCommanderSize`, only if the mode is not on yet |
| 17, 18 (0x11, 0x12) — squad and commander menus | `setMaxiPos` / `setMaxiSize`, alpha 0.9 |
| everything else, 19 (`MapMenuShow`) included | leave the target alone |

State 19 is the quick zoom menu (`HudElementsMapMenu.con`: the `MapZoom`
button and a label), and it lies **on top of** the map. In the data it holds
only two nodes, and that is not a build error.

## The animation step (0x77c330)

```
value += (1 - e^(-6*dt)) * (target - value)
```

The exponent is clamped to [-10, 10] (`FUN_00402ee0`); closer than 0.1 to the
target it simply snaps. The same speed of 6.0 drives the position, the size
and the alphas.

## Deriving the show variables (0x77d3a0)

```
if size >= maxiSize - 3 on both axes   -> MapFullSize
else if size <= miniSize + 3           -> MapMinSize
else                                   -> neither (the size is in transit)
```

In commander mode (state 0xf) the comparison is against `setCommanderSize`.

## What we still do not know

* The outline: the minimap is round, the big one square — **where the engine
  gets the round mask is not established**. We take the outline from
  `MapMinSize`.
* The alphas `+0x700`, `+0x704`, `+0x70c`, `+0x710`, `+0x718` (map, icons,
  highlight) are driven by the same animation, but we do not reproduce them
  yet.
* In the original the derivation block does not always run: it is skipped
  while the minimap is still turning the compass (+0x774). We do not track
  that flag, so we compute it every frame.
* `MapFullSizeAndNotSpawnShow` at 0x4668f0 has a third condition as well —
  `[0xa10890]->vtbl[0x24c]()->+0x24f`; what that field is has not been
  established.

## The compass angle

`MinimapDelayedMapAngle` is field **+0x760** of the map node; the name is
glued together at 0x780a49 from the node's name and the string
`%sDelayedMapAngle` (0x930c70). In the data a single node uses it —
`MapCompass` (`setPictureNodeRotateVariable`).

Two smoothers in a row drive it, both at speed 9.0 (the constant -9.0 at
0x930334, used at 0x77c542 and 0x77c68b):

```
+0x770 = atan2(direction.x, direction.z)      0x751d8b -> 0x772470
+0x75c -> driven towards +0x770               0x77c4f4..0x77c569
+0x764 = wrapToPi(+0x75c)                     0x77c57f
+0x760 -> driven towards +0x764               0x77c632..0x77c6b2
```

The approach goes by the shortest arc: the direction comes from
`FUN_00772310` (0x772310) with a threshold of 0.001, the length from
`FUN_00772410` (0x772410), and the wrap into (-pi, pi] from `FUN_007722d0`
(0x7722d0).

The wobble after a sharp turn (fields +0x724, +0x758, +0x768, +0x775, +0x777
and a sine at frequency 4.5 from 0x930330) does exist in the original, but
**we do not reproduce it**: it has not been cross-checked.

## The minimap's centre and zoom

The minimap does not show a fixed piece of the picture: it **follows the
player** and zooms in.

The centre is computed at 0x751d55 in fractions of the world's size:

```
u = (sizeX/2 + playerX) / sizeX
v = (sizeZ/2 + playerZ) / sizeZ           (negated in the client: -1/sizeZ)
```

and handed to 0x773630, which writes the target into +0x740/+0x744. Then
0x77cd10 drives +0x748/+0x74c towards it — at the same speed of 6.0 as the
size and the position. The world's dimensions come from
`GLSWorldSizeX`/`GLSWorldSizeZ`.

The zoom is an integer 0..2 in field +0x6d0. The console command
`MiniMap.setZoom` writes it (0x57a97e — it simply puts the argument into that
field), and in the data it hangs on the `MapZoom` button
(`HUD/HudSetup/SpawnInterface/HudElementsMapMenu.con`). The smoothed value
+0x698 is driven towards the index itself, and the magnification is

```
pow(2.3, +0x698)
```

where 2.3 sits as a double constant at 0x930150 and is taken at 0x77397c.
Three tables of three numbers each (+0x6d8, +0x6e4, +0x6f0) are selected by
0x77cf6a according to the mode; what exactly is in them is **not
established**, but the equality of +0x698 and `(float)+0x6d0` at 0x77d4ad
says these are the indices themselves.

At zoom 0 we take the window as a square around the combat area — the one
measured from the original's frame dump. That the engine shows exactly that
at zoom 0 is **not proven**: it is our inference from the button being called
"zoom in".

How the engine blends the minimap's centre with the second centre
(+0x7e8/+0x7ec) by the weight +0x694 — 0x773870 — has **not been worked out
yet**; for now we simply draw the big map as the whole combat area and the
minimap following the player.
