# Reference: the original's battle HUD, 800x600

Captured from a live game: `tools/bf2_app.sh`, the player already in a
battle, the dump taken with `touch /tmp/mtld3d_dump`. Coordinates
converted to screen space (`400 + x`, `300 + y`).

**Important, about batches.** BF2 merges HUD quads that share an atlas
into a single call, and the dump gives the bounds of the **batch**, not of
an individual node. So several rows below are dozens of nodes together and
cannot be checked one by one. The ones that come as their own call are
checked exactly.

| call | x | y | width | height | what it is |
|---|---|---|---|---|---|
| 228 | 350.0 | 250.0 | 100.0 | 100.0 | a separate 128x128 texture |
| 229 | -0.5 | 4.5 | 505.0 | 600.0 | **batch**: the left half of the screen together with the bottom-left region |
| 230 | 385.5 | 286.5 | 27.0 | 27.0 | **the crosshair** — we do not have it |
| 231 | 597.0 | 0.0 | 197.0 | 197.0 | minimap, batch |
| 232 | 597.0 | 0.0 | 197.0 | 197.0 | minimap, batch |
| 233 | 626.0 | 22.0 | 105.0 | 175.0 | map contents: control point icons |
| 234 | 597.0 | 0.0 | 196.5 | 196.5 | minimap, batch |
| 235 | 679.5 | 92.5 | 73.0 | 7.0 | control point caption, size 6 |
| 236 | 595.5 | -0.5 | 200.0 | 212.0 | `MapFrame` |
| 237 | 612.5 | 2.5 | 169.0 | 10.0 | the ticket numbers, both together |
| 238 | 637.5 | 560.5 | 400.0 | 39.0 | `BottomRightBar` |
| 239 | 767.5 | 567.5 | 16.0 | 10.0 | rounds in the magazine |
| 240 | 785.5 | 562.5 | 5.0 | 7.0 | magazine count |

## What the comparison gave

**An exact match** — and for the first time checked against a live game
rather than against the data:

| what | original | ours |
|---|---|---|
| minimap | 597.0, 0.0, 197x197 | `setMiniPos 197/-300` -> 597, 0; `setMiniSize 197/197` |
| `MapFrame` | 595.5, -0.5, 200x212 | 596, 0, 200x212 |
| ticket numbers | 612.5..781.5 | 612..783 (two nodes of 27) |
| **`BottomRightBar`** | **637.5, 560.5, 400x39** | **637.5, 561, 400x39** |

The last row matters on its own: 336.5 as the right-hand region's shown
position was **measured** from the spawn screen dump, and is now confirmed
in battle as well. The file has 201 in that place, and it is wrong.

## The scoreboard

Captured in a separate dump while `Tab` was held (`docs/research` does not
keep the log itself — see the screenshot below). The most notable rows:

| call | x | y | width | height | what it is |
|---|---|---|---|---|---|
| 156 | -0.5 | -0.5 | 800.0 | 600.0 | input overlay (`DummyButton`) |
| 166 | 9.5 | 25.5 | 780.0 | 513.0 | **batch**: both tables together with the tabs |
| 163 | 9.5 | 516.5 | 389.0 | 30.0 | own team's summary |
| 170 | 400.5 | 516.5 | 389.0 | 30.0 | the other team's |
| 173 | 9.5 | 535.5 | 780.0 | 22.0 | the bottom bar |
| 174 | 329.5 | 543.5 | 141.1 | 13.0 | "Right click to activate mouse" |
| 175 | 9.5 | -0.5 | 310.0 | 18.0 | the "SERVER INFO" bar |
| 178 | 331.5 | -0.5 | 188.0 | 397.0 | not yet identified |

The summary rows match ours to within half a pixel (ours are 10/401, 517,
389x30). The rest is a batch and is not compared item by item.

**And the main thing: the crosshair (call 148, 27x27) is drawn even while
the scoreboard is up.** So it does not belong to the battle screen — it is
always there.

## What we do not have

* **the crosshair** — 27x27 in the centre (call 230);
* **the ammo numbers** — 16x10 and 5x7 in the bottom right (239, 240);
* the control point icons on the minimap fit into 105x175 in the original
  (call 233), while ours are noticeably bigger — the icon size has not
  been measured yet.

## What the comparison found in excess — and what was done about it

We were drawing the **large** map's bar in battle:

```
MapFrameOpenPic      596.0   0.0  200.0 x 18.0
FriendlyCPsMapOpen   658.0   6.0   19.5 x  6.0
EnemyCPsMapOpen      716.5   6.0   19.5 x  6.0
```

The original does not have them in battle. The `MapFrameOpen` branch hangs
off `setNodeLogicShowVariable NOT MapMinSize 1`, and we took the
condition's value only from the dictionary of **numbers** — boolean show
variables live in another one. Because of that `NOT MapMinSize` was
**always** true. Fixed: `obf2::hud::showValue` looks in both dictionaries.
