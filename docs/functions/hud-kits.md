# The spawn screen's kit rows: who fills them

`BF2.exe`, **0x468510** — a virtual of `HudInformationLayer`
(`code\bf2\game\HudInformationLayer.cpp`, named through the checked build's
`Debug` calls), reached from the vtable at 0x89e9f0. It runs every frame and
writes the seven rows of the spawn screen's kit column into the layer's own
fields, which the `.con` then reads by name (docs/functions/hud-variables.md).

Everything below was taken from that function; what the function does with a
value was checked against a frame dump of the running game on Strike at Karkand
(docs/research/spawn-screen-named.md) — the pictures the original really put on
screen, named through `Menu/Atlas/MemeAtlas.tai`.

## The loop

```
local_1c  = 0                     the row
local_34  = 0xc5                  an index into the layer as an array of ints
local_28  = param_1 + 0x247       Kit<N>AbilityIcon<M>Show
local_24  = param_1 + 0xb7        the row's string fields, as ints
do {
  ...
  local_24  += 1                  one int  = one row
  local_28  += 5                  five ability flags = one row
  local_34  += 5                  five ability paths = one row
  local_1c  += 1
} while (local_34 <= 0xe7);
```

`0xc5, 0xca … 0xe3` is seven passes — **seven rows, and the count is the loop's,
not the data's**. The kit of a row comes from the kit template manager:

```
local_20 = (**(DAT_0099ef80 + 0x144))(param_2, local_1c)   // (team, row)
```

`param_2` is the side. In the game's data that call is answered by the level:

```
gameLogic.setKit 2 0 "US_Specops" "us_light_soldier"
```

— team, row, the kit's `ObjectTemplate`, the soldier's. So the order of the
column belongs to the level and not to the engine. Across the game's levels it is
always Specops, Sniper, Assault, Support, Engineer, Medic, AT, which is also the
order `Kits/ai/Objects.ai` creates its seven `kitTemplate`s in.

## The fields of a row

The byte offsets are into `HudInformationLayer`; `N` is the row.

| field | variable | what the engine puts there |
|---|---|---|
| +0x202 + N | `Kit%iShow` | the layer's own `KitsShow` (+0x201), copied at the end of each pass |
| +0x240 + N | `PlayerKitIcon%iSelectShow` | the chosen row — set elsewhere |
| +0x247 + 5N + M | `Kit%iAbilityIcon%iShow` | whether the row has an M-th ability icon |
| +0x26a + N | `KitUnlockArrow%iShow` | `KitsShow` and `KitUnlock%iShow` and the manager's `+0x250(row, team, 1)` |
| +0x271 + N | `KitUnlock%iShow` | `KitsShow` and the kit has an unlock |
| +0x278 + 4N | `Kit%iSprintAbility` | `1.0 - <a float of the kit at its +0x280>` |
| +0x2dc + 4N | a string | written from the unlock loops |
| +0x2f8 + 4N | `KitAltWeaponIcon%iPath` | `Ingame/GeneralIcons/empty.tga`, replaced by the unlock's picture |
| +0x314 + 20N + 4M | `Kit%iAbilityIcon%iPathString` | the M-th ability icon |
| +0x3a0 + 4N | `KitName%iString` | the kit's localised name (a wide string) |
| +0x3bc + 4N | `KitWeaponIcon%iPath` | the kit's primary weapon picture |

The three flag families sit end to end and that is what pins them down:
`PlayerKitIcon%iSelectShow` is seven bytes from 0x240, the thirty-five ability
flags run 0x247..0x269, then seven `KitUnlockArrow` and seven `KitUnlock`. The
registration function (0x468dd0) gives the same three starts independently.

## Where each picture comes from in the data

The engine reads these through a component of the kit; the mapping below is the
one the frame dump confirms, picture by picture, for all seven kits of the US
side on Strike at Karkand.

| what the row shows | where it lives |
|---|---|
| the caption | the kit's `vehicleHud.hudName`, through the localiser |
| the little icon at the left, 15x15 | the kit's `vehicleHud.vehicleIcon` |
| the big weapon picture, 150x44 | the `weaponHud.weaponIcon` of the item whose `ObjectTemplate.itemIndex` is **3** |
| the small unlock picture, 58x17 | the `weaponHud.altWeaponIcon` of the weapon in the kit's `ItemContainer` of the highest `unlockLevel` |
| the ability icons, 15x15 | the kit's own `vehicleHud.abilityIcon` first, then each item's `weaponHud.specialAbilityIcon`, in `addTemplate` order, at most five |
| the sprint bar | `1 - ObjectTemplate.sprintStaminaDissipationFactor` |

**Item index 3 is the primary weapon.** The assault kit settles it: it carries
both `USRIF_M203` (index 3) and `USRGL_M203` (index 4), whose pictures differ,
and the row shows the rifle's. The other indices in the same kits are 1 for the
knife, 2 for the pistol, 4 for a grenade or a launcher and 5 upwards for the
kit's tools.

**The unlock shown is the level 2 one** — SCAR-L, L96A1, FN2000, MG36, MP7, G36E,
P90, one per kit, all seven matching the dump. Whether the rule is "the highest
level" or "the unlock the player has chosen" (`spawnManager.selectNextUnlock`)
the dump cannot say: every kit in the game has exactly the levels 1 and 2, and
the profile the dump was taken with had neither — the row draws its padlock.

**The sprint bar's field is not proved.** The binary reads a float at the kit's
+0x280 and the value on screen is one minus it; the only per-kit float in the
data that fits is `sprintStaminaDissipationFactor`, which is 0.2 on the light
kits and 0.6 on Assault, Support and AT. The dump agrees — the 0.2 kits draw a
full bar and the 0.6 kits two thirds of one — but the offset itself has not been
tied to the property in the binary.

## The lock and the arrow

`KitUnlock%iShow` turns the whole unlock group on; `KitUnlockArrow%iShow` chooses
between its two halves (`HudElementsSpawn.con`):

* **arrow on** — `Kit%iAltWeaponIcon` in a bright tint, plus the swap button
  `SelectUnlock%iRight` and the blinking `Unlock%iBlink`;
* **arrow off** — `Kit%iLockAltWeaponIcon` in a dull one (0.678/0.663/0.525),
  plus `Kit%iLockSquare` and `Kit%iLock` — the frame and the padlock.

The picture's path is the same variable either way, so the unlock is named on a
locked row too. That is what the original draws for a profile with no unlocks,
and it is what we draw.

## The bar and its steps

`createBarNode Kit0Info Kit0SprintAbility 3 190 101 59 5` with
`setBarNodeSnap 20` and `setBarNodeSnapDir 1`.

**`setBarNodeSnap` is the width of one step in HUD units**, not a flag. A bar 59
wide with a step of 20 has three steps, and the fill rounds up. The dump shows
exactly that: a 0.8 row fills 58.5 px of the bar and a 0.4 row fills 39.0 — two
thirds. Snapping to twentieths is ruled out by the same numbers: 39/58.5 is not a
multiple of 1/20.

The number after the node's name is **not** the direction of growth. The kit bar
is a `3` and it grows from the left, in the picture as well as on screen — the
fill takes the leftmost 39 of the art's 59 columns. What the number is has not
been established; the map's `EnemyCPs` is also a `3` and pairs with
`flags_Captured_Right.tga` and a mirrored `setBarNodeBorder`, but there is no
dump of the battle HUD to measure that against.

A bar's quad also sits half a pixel inside its own rectangle: the backing picture
comes out at 190.0,101.0 59x5 and the bar over it at 190.5,101.5 58.5x4.5. Every
other node carries Direct3D 9's half-pixel adjustment in its vertices and a bar's
does not.

## In our code

`src/hud/src/kit_list.cpp` builds a row out of the registry;
`src/level/src/level.cpp` reads `gameLogic.setKit`; `src/app/main.cpp` pours the
row into the variables in `applySpawnState`, so the column follows the side.
`tools/kit_info <modDir> <level> [team]` prints the seven rows, and
`tools/hud_atlas.py <dump> --ours <our --hud-rects>` lays them beside the
original's.
