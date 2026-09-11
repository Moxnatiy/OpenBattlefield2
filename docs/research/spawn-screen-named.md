# The spawn screen of the original, named

Taken from a joined server on Strike at Karkand (`tools/bf2_run.sh`, the file
trigger, `tools/hud_atlas.py`). Not a list of rectangles this time: every
rectangle carries the name of the art it shows, looked up in
`Menu/Atlas/MemeAtlas.tai` by the texture coordinate the call itself uses.
See docs/research/03-frame-dump.md for how that works and why it is exact.

48 two-dimensional calls, 41 quads, 29 of them named. What is not named is
named honestly: text lives on a font page and not in the atlas, and where the
atlas entry's size does not match the size the call cut out, the tool refuses
rather than guesses.

## What the screen is made of

| where | size | the art |
|---|---|---|
| 0.0, 5.0 | 505x600 | `Ingame/GeneralIcons/full.dds` at **alpha 0** — a rectangle that draws nothing |
| 9.5, 25.5 | 246x50 | `Ingame/Respawn/team1_kit.tga` — the header of the kit list |
| 15.5, 30.5 | 18x12 | `Ingame/Flags/Icons/Hud/Score/US/scoreBoard_Flag.tga` |
| 103.5, 31.5 | 18x12 | `Ingame/Flags/Icons/Hud/Score/Mec/scoreBoard_Flag.tga` |
| 9.5, 26.5 … 117.5, 51.5 | six rectangles | `Ingame/GeneralIcons/empty.dds` — the tabs' click areas, invisible |
| 9.5, 72.5 | 246x69 | `Ingame/Respawn/kit_background.tga` |
| 14.5, 77.5 | 15x15 | `Ingame/Respawn/iconframe.tga` |
| 14.5, 77.5 | 16x16 | `Ingame/Kits/Icons/kit_Specops.tga` |
| 14.5, 94.5 | 150x44 | `Ingame/Weapons/Icons/Hud/USRIF_M4.tga` |
| 173.5, 76.5 | 58x17 | `Ingame/Weapons/Icons/Hud/usrif_fnscarl_mini.tga` |
| 14.5, 160.5 | 150x44 | `Ingame/Weapons/Icons/Hud/USRIF_M24.tga` |
| 173.5, 142.5 | 58x17 | `Ingame/Weapons/Icons/HUD/gbrif_l96a1_mini.tga` |
| 14.5, 226.5 | 150x44 | `Ingame/Weapons/Icons/Hud/USRGL_M203G.tga` |
| 173.5, 208.5 | 58x17 | `Ingame/Weapons/Icons/Hud/sasrif_fn2000_mini.tga` |
| 14.5, 292.5 | 150x44 | `Ingame/Weapons/Icons/Hud/USLMG_M249SAW.tga` |
| 173.5, 273.5 | 58x17 | `Ingame/Weapons/Icons/Hud/sasrif_mg36_mini.tga` |
| 14.5, 358.5 | 150x44 | `Ingame/Weapons/Icons/Hud/USRIF_Remington11-87.tga` |
| 173.5, 340.5 | 58x17 | `Ingame/Weapons/Icons/Hud/sasrif_mp7_mini.tga` |
| 14.5, 425.5 | 150x44 | `Ingame/Weapons/Icons/Hud/USRIF_M16a2.tga` |
| 173.5, 407.5 | 58x17 | `Ingame/Weapons/Icons/Hud/sasrif_g36e_mini.tga` |
| 14.5, 490.5 | 150x44 | `Ingame/Weapons/Icons/Hud/USRIF_MP5_A3.tga` |
| 173.5, 472.5 | 58x17 | `Ingame/Weapons/Icons/HUD/eurif_fnp90_mini.tga` |
| 589.0, 525.8 | 25x25 | `Ingame/GeneralIcons/pointerMinimap.tga` |

Seven kit rows, each a 150x44 picture of the kit's main weapon on the left and
a 58x17 one of its secondary on the right, over a 246x69 background — and the
row under the cursor gets `kit_selected.tga` over it. That is the whole left
column, and it is exactly what our `SpawnMenu` tree has to produce.

## What is drawn and is not in the atlas

* **the map**, 446.9x512 at 342.6,26.5 — its texture coordinate covers a whole
  page, so it is not a picture from the atlas but a target the game rendered
  the level's map into. Beside it a 511.5x511.5 call from `TextureId(73)`,
  another target of its own;
* **every caption** — the font pages (`TextureId(72)`, 128x64, A4R4G4B4) and
  the two 2048x2048 pages the game keeps for the localised faces;
* the crosshair-sized 100x100 call from `TextureId(186)`, tinted blue at half
  alpha, and the 77x324 orange one from `TextureId(75)` — neither is atlas art.

## Why this matters more than the rectangles did

Our own `--hud-rects` prints, for every node it draws, the node's name **and
its texture**. The original's side now prints the same thing. So the interface
is compared by pairing two lists of file names, and what is missing or extra
comes out as a list of names rather than as an argument about a few pixels.

## Paired with our own

`tools/hud_atlas.py <dump> --ours ours.txt`, where `ours.txt` is
`openbf2 --hosted --level strike_at_karkand --hud-rects`. The two sides are
paired by the art each draws, not by where it lands, so what comes out is a
list of names.

**The half pixel everything seemed to be off by was the tool's own.** The dump
prints the vertices the game handed to Direct3D 9, and D3D9 puts a pixel's
centre at an integer, so a rectangle covering pixels 10..255 is written
9.5..255.5. Metal puts the centre at integer + 0.5, so the same rectangle is
written 10.0..256.0 — and mtld3d's own vertex shader adds that half pixel back
for pre-transformed geometry (`dxso/ff.rs`, the `XYZRHW` branch: `+ 1.0 / vp.x`,
`- 1.0 / vp.y`). The original's 9.5 and our 10.0 are the **same pixels**.

An earlier reading of this comparison called it a difference and proposed one
offset "to move the whole interface onto the original's grid". That offset would
have moved our interface half a pixel off it. `tools/hud_atlas.py` now adds the
half pixel itself, and the two sides come out at `+0.0, +0.0`.

**The original draws these and we draw them nowhere.** With the per-quad limit
raised the same screen comes out as 156 quads, 110 of them named, and the list
is no longer about a few pixels:

| where | size | the art | in the left panel |
|---|---|---|---|
| 0.0, 5.0 | 505x600 | `GeneralIcons/full.dds` | **alpha 0** — it draws nothing |
| 15.5, 30.5 | 18x12 | `Score/US/scoreBoard_Flag.tga` | the header's flags |
| 103.5, 31.5 | 18x12 | `Score/Mec/scoreBoard_Flag.tga` | |
| 173.5, 76.5 + 66n | 58x17 | seven `*_mini.tga` | each kit's **secondary** weapon |
| 233.5, 77.5 + 66n | 15x15 | `Respawn/lockicon.tga` and `iconframe.tga` | the padlock on a locked kit |
| 233.5 / 215.5, 113.5 + 66n | 15x15 | `handgrenade`, `c4`, `claymore`, `kevlarvest`, `smokegrenade` | what the kit carries |
| 9.5, 26.5 | 170x25 | `GeneralIcons/empty.dds` | the tab strip's click area; ours is 85 wide |
| 589.0, 525.8 | 25x25 | `GeneralIcons/pointerMinimap.tga` | |

And on the map, which we draw bare: `minimap_cpbase`, `mini_jeep`, `mini_tank`,
`mini_apc`, `mini_smgsmall`, `mini_armourdefsmall`, `radar`, `bridge`,
`airdef`, `uavtrailer` — the assets and vehicles of the level, each 16x16 or
19x19 over the map.

**A kit row is more than we build**, and the level's own data says exactly
what is in it — `Menu/HUD/HudSetup/SpawnInterface/HudElementsSpawn.con`, the
`Kit0Info` group:

| node | where | what shows it |
|---|---|---|
| `Kit0BackgroundIcon` | 10 73 246 69 | always |
| `Kit0KitSquare` / `Kit0MiniIcon` | 15 78 15x15 | `NOT PlayerKitIcon0SelectShow` |
| `Kit0KitSquareSelected` / `Kit0MiniIconSelected` / `Kit0SelectIcon` | the same place | `PlayerKitIcon0SelectShow` |
| `Kit0WeaponIcon` | 15 95 150 44 | texture from `KitWeaponIcon0Path` |
| `Kit0AltWeaponIcon` | 174 77 58 17 | `KitUnlock0Show` **and** `KitUnlockArrow0Show`, texture `KitAltWeaponIcon0Path` |
| `Kit0LockAltWeaponIcon` | 174 77 58 17 | `KitUnlock0Show` and **NOT** `KitUnlockArrow0Show` |
| `Kit0LockSquare` + `Kit0Lock` | 234 78 15x15 | the same pair — `iconframe.tga` and `lockicon.tga` |
| `Kit0SprintAbilityIcon` | 177 101 13 5 | always |
| `Kit0SprintAbilityFaded` + the bar `Kit0SprintAbility` | 190 101 59 5 | the bar's value is `Kit0SprintAbility`, snap 20 |
| `Kit0AbilityIcon0..4` | 234, 216, 198, 180, 162 at 114, 15x15 | `Kit0AbilityIconNShow`, texture `Kit0AbilityIconNPathString` |

So the secondary weapon, the padlock and the five equipment icons are all
**there in the data** and we did not draw them because their variables were not
set: `KitUnlock0Show`, `KitUnlockArrow0Show`, `Kit0AbilityIconNShow` and the
paths beside them. That is the answer to who governs an element — a variable
per element, written by the game every frame.

**They are set now.** Who writes them, and out of what, is reversed in
docs/functions/hud-kits.md (`HudInformationLayer`, 0x468510) and built in
`src/hud/src/kit_list.cpp`. Laid beside this dump, every rectangle of the left
column now pairs at `+0.0, +0.0` — the seven backgrounds, the kit icons, the
seven weapon pictures, the seven unlock pictures, the padlocks, the sprint icons
and bars, and every equipment icon. Nothing visible is left missing on that side
of the screen.

The one entry still in the missing list there, `GeneralIcons/full.dds` 505x600,
is drawn with the tint `1.00/0.00/0.00/0.00` — **alpha zero**. `full.dds` is the
game's solid fill, and the interface uses it wherever it wants a plain rectangle;
this one is invisible. An earlier reading of this table called it "the panel
behind everything" because the dump named the picture and the size and the table
did not carry the tint. It does now.

An earlier reading of this comparison called the sprint icon and its bar
surplus on our side. That was wrong, and the fault was the dump's: its per-quad
list stopped at sixteen rectangles, and a kit row is eighteen — so the whole
row came through as one bounding box and its small pieces were never listed.
The limit is now 128 rectangles.

## What is still missing, measured

Everything below is from the same dump and is still absent on our side. The left
column is done; the rest of the screen is the map and what stands on it.

| the art | where | what it is |
|---|---|---|
| `spawn_unselected` | 521.5, 356.5 82x4 and 521.5, 360.5 16x16 | a spawn point with a plate under it |
| `pointerMinimap.tga` | 25x25 wherever the mouse is | the cursor, which we do not draw at all |
| `GeneralIcons/empty.dds` | 9.5, 26.5 170x25 | the tab strip's click area; ours is two of 85 |

Everything struck from this list is now drawn and pairs with the original: the
map's crop and its narrowed rectangle, the frame, the red hatch outside the
combat area, the base icon, the vehicles, the emplacements and the strategic
objects. What they cost is written up in docs/functions/hud-map.md, along with
the one difference that stands — the original draws one icon per bridge and we
draw one per destroyable segment.
