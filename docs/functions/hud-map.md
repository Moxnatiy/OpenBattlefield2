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

## The map node's own art

`BF2.exe`, 0x780180 — the map node's constructor (`Bf2MenuMapNode.cpp`). Every
picture the map draws is loaded here, in one place, through the resource
manager's `+0x8c` ("load texture by name"). The `.con` names none of them: the
map node is created with `createMapNode` and given only its positions, its sizes
and the capture-point font, so all of this is the engine's and not the data's.

| field | texture |
|---|---|
| +0x944 and +0x948 | `Ingame/Minimap/map_CombatArea32.dds` — both hold it; +0x940 is set to 0 beside them |
| +0x930 | `Ingame/Minimap/map_Shadow.tga` |
| +0x934 | `Ingame/Minimap/map_Frame.tga` |
| +0x92c | `Ingame/Minimap/map_Mask.tga` |
| +0x8a8 | `Ingame/Minimap/map_Line.tga` |
| +0x8ac, +0x8b0 | `SupplyLine/sl2_line.tga`, `SupplyLine/sl2_arrow.tga` |
| +0x928 | `Ingame/Minimap/Icons/request_circle.tga` |
| +0x8b4 … +0x900 | the order icons, twice over — plain and `*Commander.tga` |
| +0x950 … +0x964 | the spawn point's eight states: `spawn_Selected`, `spawn_UnSelected`, each with `Inactive` and `Squad` variants |
| +0x984 … +0x9b8 | the player icons: `mini_LocalArrow2`, `mini_Soldier`, `mini_Health`, `mini_headingArrow`, `mini_Medic`, `mini_MedicArrow`, `mini_Ammo`, `mini_Repair`, `mini_Revive` |
| +0x9c8, +0x9cc | `laserpaintTarget.tga` and its active form |
| +0x9d0 | `unitSpotted.tga` |
| +0x8a4, +0x8a0 | `Ingame/GeneralIcons/full.tga` and `empty.tga` |
| +0xa90 | `map_SatelliteScanLine.tga` |
| +0xad4, +0xad8 | `map_UAV.tga`, `Icons/icon_UAV.tga` |
| +0xb04, +0xb08 | `Icons/placedAirstrike.tga`, `Icons/icon_artillery.tga` |
| +0xb24 | `Icons/icon_supplies.tga` |

The same function registers the map's variables with the graph's variable
manager (`DAT_0098734c`): `%sDelayedMapAngle`, `%sSatelliteTimeUntilReloaded`,
`%sSatelliteActive`, `%sSatelliteReloading`, `%sUAVTimeUntilReloaded`,
`%sUAVState`, `%sUAVActive`, `%sUAVReloading`, `%sShowKitsOnMap`,
`%sZoomDisplay` and a family `%sMapFilter%iActive` — the `%s` is the node's own
prefix, field +0x24.

### The frame

The square map draws its own frame, and it is in the map's draw call rather than
in a node of its own: three strips of `full.dds`, four units thick, along the
top, the right and the bottom of the node's rectangle, in the colour the call is
tinted with — 0.48/0.47/0.39 at full alpha. The left one is absent on Strike at
Karkand because the whole node is cut on the left there (see the crop below).
The bottom strip ends where the DONE button begins, so the two read as one shape.

### The red hatch over the map

Between the map's picture and the atlas batch the original draws one more quad:
the node's square, 278.5,27.5 511.5x511.5, from a 512x512 A8R8G8B8 texture
sampled whole, tinted white at alpha 0.8. The texture is
`Ingame/Minimap/map_CombatArea32.dds`, which the constructor above loads into
+0x944 and +0x948 — a single colour, (139, 57, 39), carrying a diagonal hatch in
its alpha channel and solid along its border. Nothing else in the frame binds
that texture id, and no other 512x512 uncompressed texture the map node loads
would be drawn over the map whole.

A texture that ships with the game cannot know a level's combat area, and a
screenshot of the original settles what happens: **the hatch stands everywhere
except inside the combat area**, where the map shows through untouched, and the
hole has the polygon's own outline. So the engine writes the texture at level
load, and the smallest write that produces the picture is clearing the alpha
inside the polygon. The texture is uncompressed and therefore lockable, its
colour is one constant so only alpha need be written, and the node keeps two
handles to it with a third field zeroed beside them — but the code that does the
writing has **not** been found; what is measured is the picture and the texture
that makes it.

Two details the same screenshot settles:

* the overlay covers the map node's **whole** square and is not cut with the
  picture. On Strike at Karkand the sixty-four pixels on the left that the
  level's map cannot fill are hatched over the world behind them;
* the square of world the texture spans is the **uncut** crop square — the one
  centred on the combat area whose side is the area's larger side plus the map's
  margin. Its left edge is world x -603 there, while the picture beside it starts
  at -512 because that is where the picture begins.

In our code: `src/hud/src/combat_area.cpp` cuts the hole, `src/app/main.cpp`
builds the picture once per level and hands it to the texture resolver under
`#combatarea` the way the Flash menu's frames are handed over, and
`src/hud/src/render.cpp` draws it between the map and the frame.

### What stands on the map besides the flags

Three kinds of icon, all of them out of the objects' own templates rather than
out of the HUD's data, and all measured against the original's spawn screen on
Strike at Karkand (docs/research/spawn-screen-named.md):

| what | size | where the picture comes from |
|---|---|---|
| a capture point | 32 | `Ingame/Flags/Icons/Minimap/%s/miniMap_CP.tga` |
| a side's main base | 32 | the same path with `miniMap_CPBase.tga` |
| a vehicle spawner | 16 | the vehicle's `vehicleHud.miniMapIcon` |
| a strategic object | 19 | its `StrategicObject.intactIcon` |

`StrategicObject` is a component, and in the game's data it sits on the bridges
(`lrg_stonebridge`, the highway segments), the mobile radars, the air control
towers — the UAV — and the artillery pieces `ars_d30` and `USART_LW155`. It also
carries a `destroyedIcon`, and every one of those has a `_broken` twin in the
atlas; we never use it, because nothing tells us an object has been destroyed.

A spawner can put a strategic object on the field instead of a vehicle: that is
why the original's map shows a `Radar` and two `AirDef` where a vehicle icon
would otherwise be. So a spawner's template is asked for its strategic icon
first and its vehicle icon second.

Laid beside the original's dump, every icon it names now lands within about 1.3
pixels — the same residual the map itself carries. **One difference stands:**
the original draws one icon per bridge and we draw one per destroyable segment.
Karkand's north bridge is two `lrg_stonebridge` at world x -95.0 and -51.1, and
the original's icon sits at 644.5 — the midpoint of the two we draw, at 629.5
and 660.7. How the engine gathers a bridge's segments into one icon is **not
established**.

Which point is a base is not read out of the binary either: we go by the level's
own `unableToChangeTeam`, which on Karkand picks out exactly the one point the
dump shows a base icon for.
