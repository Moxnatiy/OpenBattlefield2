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
| -0.5, 4.5 | 505x600 | `Ingame/GeneralIcons/full.dds` — the panel behind everything |
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
