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

**What Retail draws (25 Nov).** The direction comes from the caller:
0x751c10 takes it as three floats, passed on from BF2HudManager's slot at
0x926158 (0x756162..0x75617b). Who calls that slot, and with what vector, is
not taken apart. On Retail the ring stays north-up at every heading, the
texture's own orientation (`map_Compass.tga` has N at the top):

* on foot facing south;
* in the AH-1Z facing north-west.

Source: stills of the original on 25 Nov (spawned, outside,
entered, hovering). What turns is the white view cone at the player.
So the port drives the ring toward 0, not toward the look angle. The earlier
"+π, north at the bottom of the atlas" turned it with the view, which Retail
never shows. The port does not draw the view cone yet.

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
(`HUD/HudSetup/SpawnInterface/HudElementsMapMenu.con`). The magnification is

```
pow(2.3, +0x698)
```

where 2.3 sits as a double constant at 0x930150 and is taken at 0x77397c.

**The index is not the exponent** (an earlier version of this note said it was,
from a comparison at 0x77d4ad; read whole, the code says otherwise). The
constructor fills three tables of three floats (0x781ad5..0x781b16):

| table | when | index 0 | 1 | 2 |
|---|---|---|---|---|
| +0x6d8 | map collapsed (+0x68e off) | 0.5 | 1.5 | 2.5 |
| +0x6e4 | map expanded (+0x68e on) | 0.0 | 1.0 | 2.0 |
| +0x6f0 | commander's screen (HUD manager +0x1ec) | 0.0 | 1.0 | 2.0 (rewritten from the commander camera's height) |

0x77cae2..0x77cbe7 drives +0x698 towards `[map + ((0x92 + +0x68e) * 3 +
index) * 4]` (or the +0x6f0 row when +0x1ec says so): snapped when closer than
1e-4 (0x8ebdfc), otherwise `+= (1 − e^(−6·dt)) · (target − value)` (−6.0 at
0x930328, the exponent clamped to ±10). `getZoomScale` (0x77395a) reads the
same cell. The index starts at **1** (0x781a88 → +0x6d0 at 0x781ac9).

+0x68e is "the map is expanded", written only by `setExpanded` (0x776a7b on,
0x776a90 off):

* expanding stores the flag, centres the view on the combat area and, if the
  spawn screen's +0x690 or the commander's +0x691 is on, asks `setZoom(0)`
  (0x776a72..0x776a8c); otherwise the zoom is left as it was (the big map, M);
* collapsing clears the flag first and then asks `setZoom` for the zoom the
  local vehicle type saved — and `setZoom` (0x777980) does nothing unless
  +0x68e is on, so the index survives the collapse.

The zoom key's own handler is still not found; the HUD's `setZoom` command with
no argument is `cycleZoom` (executor 0x76bcbb → 0x7779a0 → `applyZoom(-1)`,
0x776670): index + 1, past 2 back to 0.

So after a spawn the corner map sits at index 0 of the collapsed row, 2.3^0.5 ≈
1.52 times the combat area's square, and the big map at index 0 shows the
square whole. Ours: `hud::MapNode` (`kMapZoomExponents`, `cycleZoom`), and the
combat-area hatch is drawn from the part of its square the zoomed window shows.

At zoom 0 we take the window as a square around the combat area — the one
measured from the original's frame dump. The combat area, the capture points
and the spawners are the round's own layer on a live server (the
`GamePlayObjects.con` of the mode and size it runs, `RemoteWorld::roundGameplay`,
handed in as `hud::Manager::Setup::gameplay`); without one, Conquest 16. Until
24 Nov 2026 the HUD always read Conquest 16, so a 32-player Gulf of Oman showed
four points the server does not have, with other ids. That the engine shows exactly that
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
pixels — the same residual the map itself carries. **One difference stood:**
the original draws one icon per bridge and we drew one per destroyable segment.
Karkand's north bridge is two `lrg_stonebridge` at world x -95.0 and -51.1, and
the original's icon sits at 644.5 — the midpoint of the two we draw, at 629.5
and 660.7. How the engine gathers a bridge's segments: Linux server 1.5,
`GameLogic::addStrategicObject` (0x482e00). The key is the template's
`strategicObjectName` plus one character, a running index `n` (0, 1, 2, …), in
a map at `GameLogic+0x2c0` of `StrategicObjectHolder`. For each `n` whose key
exists, the new object joins that holder when the template is not
`isOnePiece` (vtable +0xd0 false) and the distance from the holder's first
object to it is less than `2 × (r_new + r_first)` (each `r` from the object's
vtable +0x130, `getPhysicBoundingRadius`, below). Otherwise `n` goes up. With
no match, a new holder is made under that key with index `n`. This is the
server's copy; the client's is 0x4f1750 (same key and merge).
The holder's position is the mean of its objects' absolute positions
(`StrategicObjectHolder::getPosition`, Linux 0x5be230). The radius is
`Object::getPhysicBoundingRadius` (Linux 0x687770, vtable +0x130): the
field at `Object+0x80`'s vtable +0xf0 plus `getBoundingRadiusModifier`
(+0x138), widened to cover child objects. Traced whole (22 Nov), every slot named
from the Linux vtables (`Object`, `DestroyableObject`, `StrategicObjectComp`):

| step | address | what |
|---|---|---|
| `Object+0x80` | `Object::setComponent` 0x688fc3 | the `IID_ICollisionMesh` component's interface, stored there, then `setBoundRadiusIsDirty` |
| who makes it | `SimpleObject::SimpleObject` 0x58a860 | `CollisionManager` vtbl+0x58 (mesh name from template vtbl+0x138, part from template +0x78), then `setComponent(0xcaa32)`; "Couldn't create collision mesh" on failure |
| template +0x78 | `SimpleObjectTemplate::setCollisionPart` 0x507830 | `ObjectTemplate.collisionPart` |
| `+0x80` vtbl+0xf0 | `CollisionMesh::getMaxBoundingRadius` 0x718a90 | its template (+0x18) → `CollisionMeshTemplate::getMaxBoundingRadius` 0x719a70 |
| 0x719a70 | | first geom with a non-empty lod vector (`isGeomValid` 0x718f00); in it the first type t = 0 .. lods-1 that `isLodValid(geom, t)` (0x7194e0) accepts; returns `getValidLod(geom, t)` +0xec; 0 when none |
| lod +0xec | `CollisionMeshTemplate::load` 0x71f600 | copied from +0xc8 after the lod's 0x18-byte box (+0xb0, min then max, read from the file) goes through `LocalBoundingBox::calcRadius` 0x72cb50 |
| +0xc8 | 0x72cb50 | sqrt of the largest squared length of the box's eight corners: distance of the farthest corner from the mesh origin |
| vtbl+0x138 | `Object::getBoundingRadiusModifier` 0x4bd530 | 0. Overridden by `PlayerControlObject` (0x536af0, a float at `[+0x250]+0x3d0`, not taken apart) and `ZiplineRope` (0x572460) |
| children | vtbl+0x1e0 `getChild`, link `+0x58` | each child whose own `getPhysicBoundingRadius` > 0 widens r to that plus the length of `getRelativePosition` (vtbl+0x1c0) |
| early out | `Object+8` bit 0 | radius 0; what sets the bit is not established |
| position | vtbl+0xd8 | `getAbsolutePosition` |
| `isOnePiece` | comp vtbl+0xd0 0x5be5d0 → template vtbl+0xc8 0x5bf0a0 | the byte at template +0x44, `StrategicObject.isOnePiece`, 0 in the constructor 0x5be840 |

The merge (0x482e00): for n = 0, 1, … while key `name + n` exists: a one-piece
object skips; a holder with no first object takes it; otherwise it joins when
`r_new + r_first + r_new + r_first > distance` (the engine's test is
`reach <= distance` → next n). The distance is to the holder's **first**
object, not its mean. The bridge templates (`woodsteel_bridge_segment`,
`lrg_stonebridge`, `highway_bridge_low_segment`) are `DestroyableObject` with a
`collisionMesh` of their own name, no `collisionPart`, and no `isOnePiece`;
two templates in the game set `isOnePiece 1`.

Ported: `CollisionMesh::maxBoundingRadius` (src/mesh), `groupStrategicObjects`
(src/hud/strategic_objects.h), the radius walk in `manager.cpp`
(`physicRadiusOf`); test `test_hud_strategic_objects`. The spawners'
strategic objects (artillery, radars, UAV towers) join the same holders after
the placed objects, with mesh part 0 (`ObjectSpawner`, 0x52f8e7). The
`PlayerControlObject` modifier (0x536af0) is the template's float +0x3d0,
`ObjectTemplate.boundingRadiusModifierWhenOccupied`: the console command that
writes it is at 0x8f1b6e, its name string is registered at 0x8ca949, and the
constructor sets it to 0 (0x53ba88).

**Turn and alpha (client, 23 Nov).** 0x77e500 walks the first object's
children with the filter at vtable 0x92ff4c (0x6273a0). That filter keeps
children whose template answers class id 0x9c42 (0x91c394), which is the Linux
server's `CID_RemoteControlledObjectTemplate`.
* With no such child, the icon turns by
  `map(+0x76c) − atan2(m[+0x20], m[+0x28]) + π/2` (0x77e689..0x77e69d, π/2 at
  0x92fe8c), from the first object's matrix (vtable +0x80, row 2). Our Gulf of
  Oman render turns the bridges the same way as
  `retail-spawn-map-gulf-of-oman-3/north-zoom.png`.
* With such a child, the icon stays upright and is skipped for another team.
  The data never builds that case: the only `RemoteControlledObject` template
  in `Objects_server.zip` is `ArtilleryControlObjectMEC`, and no template adds
  it as a child (`addTemplate`). Ours keeps such an icon upright and does not
  take the team test.

The alpha is the node's +0x6fc. It eases (0x77c330, `1 − e^(−10·dt)`) toward
+0x704. That target is 1.0 from the constructor (0x7818aa) and 0.5 on the
spawn screen (0x777fe8). In game (0x777eef) and on the big map (0x7780fd)
the target is the setting `MenuMapIconAlpha` (string 0x89ed54), and the map's
own alpha (+0x700/+0x718) is `MenuMapAlpha` (0x89ed68). Ours: `Context::mapIconAlpha`,
the settled 0.5 on the spawn screen and the constructor's 1.0 elsewhere; the
setting and the ease are not ported. The other layer draws that read +0x6fc:
0x783f90 walks the game-logic object list (`DAT_0099ef80` vtbl+0x198) and
draws vehicles by kind (0x778680: 0xf, 0x12, 0x24); 0x783440 and 0x782480
(layer 0x26, kinds 0x11/0x16) walk the player list (`DAT_00a08f60` vtbl+0x40)
and draw soldiers. So our vehicle spawners (`MapMarker::atIconAlpha`) and the
live icons (`MapContact`, below) take the node alpha too. The flags (layer 3,
0x77e720) do not read it and stay at full alpha.

**The original draws strategic icons outside the combat area too.** On Gulf of
Oman Co-op 16 (a capture of retail 1.50's spawn screen, 22 Nov, zoomed on the
north) the `bridge` icons stand in the hatched ground north of Construction
Site: faint (the node's alpha at +0x6fc), turned along each bridge and **one
per bridge**. An earlier reading at 1× missed them against the hatch and was
wrong. A live read of the running game's map node agrees: the spawn map node
is view 1, zoom 0, and its row `000001308001845b` has layer 0x10 on. Ours
draws them the same way: one per holder, turned and at 0.5.

What the binary shows so far: the map node's vtable is 0x930188 and its
draw is slot 26, **0x784a80**, which calls one sub-draw per layer flag
(`vtbl+0x104(id)`). Layer 0x10 is **0x77e500**, the strategic icons. It walks
the map returned by the global at `DAT_0099ef80`, `vtbl+0x1a4` (a map of
holders; on the server that is `GameLogic::getStrategicObjectMap`, Linux
0x475640). For each holder:

* it collects the first object's children of one kind (`FUN_0071b670` walks the
  child tree, `vtbl+0xec` / `+0x2c`, with a filter object at vtable 0x92ff4c);
  when there is one and its team (component 0xc4c5, `vtbl+0x98`, via
  `FUN_006004f0`) is not the local player's (`vtbl+0xe4`), the icon is
  **skipped**; when there is none the icon is turned by the object's heading
  (`fpatan` of the +0x20/+0x28 axis, plus pi/2, less the map's +0x76c);
* the icon is the intact one (`vtbl+0x44`) when the holder's count at
  `+0x28 → +0x14` is below 1, else the destroyed one (`vtbl+0x48`);
* size 19 × 19 (0x41980000), alpha from the node's +0x6fc, draw `FUN_0077d450(0x10, …)`.

**There is no combat-area test in 0x77e500**, which matches the capture: the
bridges outside the area are drawn. What was traced on the way (22 Nov):

* **Registration is unconditional.** Server `StrategicObjectComp::init` (Linux
  0x5be100) calls `gameLogic->addStrategicObject` at once. The client's copy
  is **0x4f1750** (same `name + '0'+n` key, same `2 × (r1 + r2)` merge, a
  `new(0x28)` holder), and its only early-out is `GameLogic+0x1fc`. The level
  load (0x4f30d0) sets that flag to 1 around one object-creation pass after
  `Triggerables.con` (0x4f4e2c) and clears it again (0x4f4ea9). It is not an
  area test. So every Gulf of Oman bridge (all `StrategicObject`,
  `Bridge.tga`, in their `.tweak` files) is in the holder map on Co-op 16.
* **The shared icon draw does not clip.** `FUN_0077d450(layer, icon)` only
  checks `vtbl+0x104(layer)` and `vtbl+0x108(layer)`, scales by a per-layer
  factor, and pushes (`FUN_0077c1f0`). `FUN_00776af0` is world → map.
* **Layer visibility depends on the view and the zoom.** `vtbl+0x104`
  (0x776fd0) reads bit `layer` of a 43-bit set at
  `node + 0x390 + (view×3 + zoom)×8`, where view is the byte at +0x68e and
  zoom is the int at +0x6d0 clamped to 0..2. `vtbl+0x108` (0x777030) reads a
  second table at +0x4e8 behind the flag at +0x7f9. Read live on
  the Gulf of Oman Co-op 16 spawn screen (`map-layers.json`): one map node,
  view 1, zoom 0. Its +0x390 rows are, for (view, zoom) 0..8:
  `308001045b`, `1308001845b` ×5, `7f000000ff`, `308001045b`, `1308001845b`
  (43-bit sets, high word first). Its +0x4e8 rows are 0, all, all, 0, 0, all.
  Who writes them is still not taken apart (the constructor 0x780180 does not
  decompile cleanly).

Which point is a base is not read out of the binary either: we go by the level's
own `unableToChangeTeam`, which on Karkand picks out exactly the one point the
dump shows a base icon for.

## The live icons (players and vehicles)

`app::visitMapContacts` (map_contacts.h) gathers them from the session's world
each frame and `hud::Manager::setMinimapContacts` hands them to the map nodes
(`Context::minimapContacts`, drawn over the markers by `buildNode`).

* **Our team's players**, from OpenBF2 (`Menu/Bf2MenuMapNode.cpp`):
  `collectIconFamily783440` paints every living player of our team where his
  object is; `populatePlayerMapIcon` picks the icon (`mini_Soldier`, a squad
  leader's `mini_Squad_<n>_small` and the commander's `mini_commander2` at 32,
  both over `mini_headingArrow`, 32, black, turned by his yaw) and
  `colorMapIcon` the colour: our squad `squadChatColors[2]`, the rest of the
  team `mapObjectColors[2]` — the own-team entry of table 0xa1ccc8, which
  0x7788b0 indexes by `2 − (team differs)` — the commander white. A player in
  a vehicle is drawn once, as the vehicle. Ourselves is not drawn: the local
  arrow is not ported. A soldier's 16 px is the size these icons always had
  here; the original's is not measured.
* **The server's vehicles**: every live `PlayerControlObject` (not the level's
  spawners) with its template's icon, `StrategicObject.intactIcon` (or
  `destroyedIcon`) at 19, else `VehicleHud.miniMapIcon` at 16, turned with the
  vehicle. Its colour is its occupants': our squad's, our team's, or white when
  empty. Once one of them is known the level's spawner icons give way.
* **Enemies** show only under a kind-2 mark (`applySweptMark`, below) or a
  kind-0 spot. An enemy soldier is taken only while his ghost updated in the
  last 1.5 s (not measured).
* **Our orders** (`ownOrders`, not the requests): `orderMove`, `orderAttack`,
  `orderDefend`, `orderRepair`, `orderDestroy`, with `Commander` for the
  commander's, by the number the squad leader's rose sends (command-rose.md).
  That number → texture table in the binary is not read; another number is not
  drawn. Their tint is not measured: the textures are drawn as authored.
* **The commander's assets** (`commanderEffects`): `icon_UAV`,
  `icon_artillery`, `icon_supplies`, `icon_vehicledrop`, untinted (not
  measured), and the UAV's circle (layer 0x20, below).
* **Spots** (`visibleSpots`): a position-only spot (`unitSpotted`) blinks with
  the node (layer 0x1e, below); a spot on an object we hold draws that object's
  icon in the enemy colour.

**Layer 0x1e (0x7820e0): position-only spots.** The node keeps a list at
+0x9fc (entries of 0x34 bytes: object id +0x4, kind +0x10, timers +0x14/+0x18,
x/z +0x2c/+0x30). The adder is 0x781ff0, which takes the kind as its 19th
argument and skips the duplicate check for kind 3. The timers count down in
0x77a900, and the vehicle and soldier draws look objects up in the list
(0x77a9a0). The HUD event handler 0x782990, case 0x2b (the spot report, our
event 43 / `readUnitSpot`), adds a kind-3 entry at the reported x/z when the
object isn't known locally and the report is for the local team. Layer 0x1e
draws kind 3 at the node alpha, only while the blink +0xa18 is on. The blink:
+0xa30 adds dt; it is on at or below 0.5 s (+0xa24, 0x781d88), off above, and
resets past 1 s (+0xa28) (0x77c43c..0x77c471); the constructor sets the timer
and the flag from `ebx` = 0 (0x781d82, 0x781da2, 0x780189). Ported:
`MapNode::spotsVisible`, `MapContact::blinks` for a spot whose target is not a
ghost we hold (`UnitSpot::followsObject`). Test: `test_map_contacts`.

**Layer 0x20 (0x77fc30): the commander's UAV.** 0x77a0d0 (called from the
event handler and 0x752950) starts it: centre +0xab0, elapsed +0xaac = 0,
spin +0xabc = 0 at +0xac0 = 4 rad/s, duration +0xae4. The radius +0xad0 is
`GameLogic::getUAVRadius` (vtbl +0x294; 100 in Linux `GameLogic::GameLogic`,
0x477813). The texture +0xad4 is `Ingame/Minimap/map_UAV.tga` (0x781928). It
is drawn 2 × radius wide, at the node alpha times a ramp: up over the first
10% of the duration, down over the last 10%. The spin wraps at 2π
(0x77b2b3). Ported as the UAV commander contact (`MapContact::worldSize`,
`angle`, `alpha`). The UAV event is HUD event 0x28 (40): case 0x28 of 0x782990
calls 0x77a0d0 with duration `GameLogic` vtbl +0x288, `getUAVActiveTime`.

**The per-object UAV circles (map +0xb38).** 0x5ac9f0 (slot 0x28 of the
object-event vtable 0x8e7f80, installed at 0x5ab0b9) calls 0x77bfe0 for a
same-team object whose Object +0x64 is set. 0x77bfe0 adds the object when
that component's vtbl +0x38 → +4 is above 0. Object +0x64 is the ability
component: Linux `Object::setComponent` stores `IID_IAbilityGLComp` at
+0xc8 (0x6891c8). The radius is `ObjectTemplate.ability.radarRadius`
(`AbilityGLCompTemplate::getRadarRadius`, an int, 0 in the constructor
0x5ae736). That the +4 the client reads is this field is inferred from it
being the template's only int radius. No stock template sets `radarRadius`,
so stock data never draws these circles. Ported: the circle is 2 × radius, at
the object, at the node alpha without a fade, local team only, dropped when
destroyed. Its spin phase against the original is not measured.

**Spotted known enemy (kind 0).** 0x7788b0 picks colour index
`2 − (team differs)`. For an ordinary enemy (not selected, which uses table
0xa1cd48; and not in the commander or squad branches) that is table 0xa1ccc8
entry 1. The static initialiser copies (0.87, 0.094, 0.094, 1) from 0x97dea0
into it at 0x86525d. `MapContact::enemy`: the vehicle's
`VehicleHud.minimapIcon`, or the soldier icon, in that colour.

Our map is baked geometry: the map nodes are rebuilt whenever the contacts
change, and the spawn screen, which bakes the map with the rest of the
screen, at most four times a second (a rebuild costs a frame) — a choice of
ours, not the original's rate, which paints every frame. Not done: the local
player's arrow, the kit icons (`showKitsOnMap`), enemies seen from a vehicle,
the
map's turn (+0x76c is 0 here).

**The control points' owners.** Each point's flag follows its
`ControlPointNetworkable` team (bit 0x01, `setTeam`, control_point_state.h)
once a record has come: `app::updateLiveHud` hands the level's
`controlPointId` → team to `hud::Manager::controlPointTeams`, which re-picks
the combat map's flag (`controlPointIcon`) and rebuilds the spawn screen. A
capture is not yet seen on screen.

**What looked like the map "reloading" on the spawn screen's first second** was
the dynamic spawn groups (192..255, the squads'): `CreateSpawnGroupEvent` carries
no squad, only the group's ghost does, and until it came such a group was drawn as
everyone's — a circle at some squad leader's feet that vanished a moment later.

## Timed marks on objects (the +0x9fc list)

**Spot kinds in the +0x9fc list.** The adder is 0x781ff0 (kind = argument 19;
duration +0x14/+0x1c = argument 15; flash timer +0x18 = argument 18).
* The spot report (case 0x2b) adds kind 0 for an enemy it knows: duration
  +0xa2c = 12 s (0x781d98), flash 0. In 0x77a9a0 kind 0 only lets the enemy's
  own icon draw (soldier or vehicle, coloured by vtbl +0xf0 = 0x7788b0 with
  id 0xf), and it would blink while its flash timer runs. With a flash of 0
  it is a steady 12 s. An enemy it doesn't know becomes kind 3 (above).
* The adder returns when the id is already listed (except kind 3): the first
  mark on an object wins. 0x77a900 counts the flash timer +0x18 down first and
  the duration +0x14 only after it; the entry goes when both are spent and
  +0x28 (duration below 0 at the add: permanent) is clear. The entry's +0x0 is
  a texture: when set, vtbl +0xec (0x775720) puts it and the entry's size
  (+0x8/+0xc, 16 × 16 when given as 0) on the object's icon.
* **Kind 1, a teammate's radio message.** HUD event 60 has no enum name (the
  registration 0x74e6xx..0x74e8e4 skips 53–56, 60 and 63). It is not sent by
  the server: the client posts it itself. A radio message from a player reaches
  0x4441d0 (from the `RadioMessageEvent` path 0x4c4f10 at 0x4c515b, and from
  0x444640 at 0x444702), which passes channel message type 3 through 0x4424a0
  to 0x440c00. Case 3 there, for a sender other than the local player whose
  player vtbl +0xa0 object (the vehicle when seated) has a networkable, posts
  (2, 0x3c) with that object's id (0x440cc9). The map's case 0x3c adds kind 1:
  duration +0xa2c = 12 s, +0xa24/+0xa28, flash 0 (0x782c8a). 0x77a9a0 colours
  it (1, 1, 0, 1) (0x97e170 → 0xa1cff8, 0x865a2f) while its flash runs or the
  blink +0xa18 is on, at the node alpha, icon type 0x19. The soldier draw also
  adds kind 1 (0x783b41, 12 s, flash 0) for a player with vtbl +0x278 true and
  +0x274 false; the Linux `Player` vtable has `setVoipSessionId` and
  `getIsMuted` near those slots: the VOIP speaker (below).
* **Kind 2, swept by the commander's satellite or UAV.**
  * Satellite (0x782580, from the soldier draw 0x783a48 and the vehicle draw
    0x7846e4, while +0xa7d is on). HUD event 41 (case 0x29) starts the scan:
    +0xa84 = `getSatelliteScanActiveTime` (vtbl +0x28c, 11 s), +0xa8c =
    `getSatelliteScanVisibleTime` (+0x29c, 7 s), +0xaa4 =
    `getSatelliteScanReloadTime` (+0x298, 16 s) (Linux GameLogic::GameLogic
    0x47782b..0x477843). Each frame +0xa88 += dt and +0xa98 = (150 (+0xaa0,
    0x78189a) + w) / 11 × +0xa88 (0x77272e), w = +0xb64, the map's drawn width
    (0x784c7d). The line is at x = (+0xa98 / w − 0.5) × `GLSWorldSizeX`; an
    object it has just passed (x − line < 0, |x − line| < this frame's step, 10
    on the first) gets kind 2: duration 2 s, flash +0xa8c = 7 s, texture
    +0xa94 = `mini_SatelliteObject.tga` (0x781785), and the sound
    `HUD_Satellite_target`.
  * UAV (0x782710, from 0x783a87 and 0x78471c). For each entry of +0xb3c (the
    UAV, centre +0xab0, radius +0xad0; or a radar object, its radius) an object
    within the radius whose map bearing atan2(Δu, Δv) (+2π below 0, 0x8ac48c)
    is within +0xac4 of spin − +0xac4 gets kind 2: duration +0xaec = 0.5 s
    (0x78195a), flash 0.3 s, and the sound. +0xac4 is this frame's spin step,
    dt × 4 (0x77b2a1). The texture is +0x988 = `mini_Soldier.tga` (0x781579)
    from the soldier draw and 0 (the object's own icon) from the vehicle draw.
  * 0x77a9a0 draws kind 2 in table 0xa1cc28 at the local team: (1, 0, 0) for
    both teams (0x97dfc0 and 0x97e150, copied at 0x8658a0), alpha = +0x14 /
    +0x1c × node alpha, so full during the flash and fading over the duration;
    icon type 0x18.
* **Kind 4, a specialist call.** HUD events 46 `CallForMedic`, 47
  `CallForAmmo`, 48 `CallForRepair`, 49 `CallForRevive`, 50
  `AutoCallForRevive` (the enum registration, 0x74e83c..0x74e8e4). The client
  sends a `SpottedEvent` with the number; the server posts it back as (2,
  +0x24) with the 24-byte body from +0x10 (Linux SpottedEvent::executeServer
  0x430321). The server sends 50 itself when a soldier goes down
  (Soldier::handleMessage 0x54d2e9 → Game::callForSpecialist 0x4145f0). Case
  0x2e..0x32 takes the caller's object from the first two bytes and its player;
  only for the local team:
  * Another player: when the local player's kit can answer (kit `Ability`
    bytes 0 healing for 46/49/50, 2 ammo for 47, 1 repair for 48; Linux
    AbilityGLCompTemplate +0x18/+0x19/+0x1a), or the node's +0x7f is set (who
    sets it is not established), the caller gets kind 4 for 12 s (+0xa2c),
    flash 0 (0x782def), texture vtbl +0x110 = 0x7730b0: +0x9b4 `mini_Medic`
    (46), +0x9a8 `mini_Ammo` (47), +0x9ac `mini_Repair` (48), +0x9b0
    `mini_Revive` (49, 50) (loader 0x7815f0..0x7816d1).
  * The local player: sets +0x7c (46/49/50), +0x7d (47) or +0x7e (48). The
    soldier draw then marks each drawn soldier whose kit has the matching
    ability (0x783929, 0x783994, 0x7839ff): duration 0, flash +0xa2c = 12 s,
    kind 4, `mini_Medic`, `mini_Repair` or `mini_Ammo`. The map draw clears
    the three flags after each frame (0x785200).
  * 0x77a9a0 leaves kind 4 its texture. The soldier draw gives an icon whose
    texture is `mini_Medic`, `mini_Repair` or `mini_Ammo` 16 × 16 and a second
    texture +0x9b8 `mini_MedicArrow` that takes the heading (0x783bc4).

**Kinds 1, 2 and 4: ported, frame by frame.** `RemoteWorld::stepMap` is one
map frame of the node, called from the frame loop (`app::stepMap`,
map_hit_sound.h) with the frame's dt:
* 0x77b280: +0xac4 = dt × 4, +0xabc += it, set to 0 past 2π. A UAV's start
  zeroes it (0x77a0d0); so does our reset, standing in for the constructor
  (0x7818e2). The UAV circle and the radar circles turn by it.
* 0x772680: while +0xa88 < +0xa84 the scan time runs and +0xa98 = (150 + w)
  / 11 × +0xa88; after, the reload +0xaa4 counts down and +0xa7d goes off.
  HUD event 41 (case 0x29) starts a scan only once +0xaa4 is 0: +0xa84 = 11,
  +0xaa4 = 16, +0xa88 = +0xa98 = +0xa9c = 0.
* The soldier draw (0x783440), for each soldier on foot from the ghost
  stream:
  * An enemy gets 0x782580 (when the scan is on and a map is drawn) and
    0x782710 (for each UAV and local radar object), with their own tests on
    its current position. 0x782580 includes the +0xa9c quirk: each test
    stores its line, so later objects in a frame see a step of 10 m.
  * A teammate gets the local player's own-call marks (+0x7c/+0x7d/+0x7e)
    and the VOIP mark: getIsTalking from `lastTalkTick` (stamped by
    `noteVoice` on each voice frame, the `setIsTalking(true)` of Linux
    0x494920) within 0.5 s, and not muted.
* The vehicle draw (0x783f90): the same two sweeps for an enemy-occupied
  vehicle, texture 0 from the UAV.
* 0x785200: the own-call flags clear.
* Every add asks for `HUD_Satellite_target` (`Sound/GameSounds.con`:
  `Common/Sound/Hud/Satellite_target.wav`, volume 0.42), kept or not.
  `stepMap` returns how many there were.

Kind 1 from radio (`receiveRadio`, HUD event 60) and kind 4 from a teammate's
call (`receiveSpecialistCall`) are added when their events come, as 0x782990
does. Test: `test_map_marks`.

The contacts only read the marks:
* `applyMapMark`: kind 1 → `radioBlink`, yellow while the blink is on;
  kind 4 → the mark's texture at 16 px, plus `mini_MedicArrow` (16 px,
  0x783bc4) turned by the soldier's body yaw for `mini_Medic`, `mini_Repair`
  and `mini_Ammo`.
* `applySweptMark`: an enemy is drawn only with a kind-2 mark, red at
  `mapMarkFade`, with the mark's texture (`mini_SatelliteObject.tga`,
  `mini_Soldier.tga`) or its own.

**The width +0xb64.** The map draw 0x784a80 copies its rectangle to +0xb5c,
scales +0xb64 by 0x773900 (pow(2.3, +0x698), 0x930150, times +0xb7c / +0x7b8),
and clamps it to the rectangle's width (0x784c70..0x784c7d). Ours
(`hud::Manager::satelliteDrawnWidth`): the whole world's drawn width, clamped
to the map rectangle, in HUD units. `app::stepMap` (map_hit_sound.h) runs
`RemoteWorld::stepMap` with it once a frame, after the live HUD, and plays
`HUD_Satellite_target` once in a frame that had any add, through a
`MenuMusic` stream at 0.42 × the effects volume.

Not measured:
* That the original's rectangle is in HUD units, the units of the 150 at
  +0xaa0.
* Which soldiers and vehicles the two draws pass to the sweeps beyond
  enemies with a live position (the icon-type test 0x17 at 0x7839xx is not
  named).
* The constructor's moment, which zeroes the spin, against ours.
* The bearing's axes (atan2(Δx, −Δz), from u along x and v against z).
* Nothing here is checked against the original on screen.
