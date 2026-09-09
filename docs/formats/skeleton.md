# `.ske` — skeleton

Verified against the game's data: the size computed from this layout
matches the file byte for byte
(`soldiers/Common/Animations/3p_setup.ske` — 80 bones, 3399 bytes;
`Common/Cloth_Line/cloth_line_setup.ske` — 3 bones, 132 bytes).

```
u32  version (2 in every file in the game)
u32  bone count
for each bone:
    u16   name length **including the terminating zero**
    char  name[length]
    i16   parent index (-1 at the root)
    f32   rotation x, y, z, w  (quaternion)
    f32   translation x, y, z
```

A bone is `2 + name_length + 2 + 28` bytes.

## What the numbers mean

Rotation and translation are **local, relative to the parent**, not an
inverse bind matrix. It shows in the soldier skeleton's own values: knee
to shin is exactly 0.075, shin to foot 0.385 — those are bone lengths,
not world coordinates.

The hierarchy is flat and already ordered: a parent always comes before
its child, so world matrices are computed in a single forward pass.

## Skeletons in the game

Nine files in total. The main ones:

| File | Bones | What for |
|---|---|---|
| `soldiers/Common/Animations/3p_setup.ske` | 80 | soldier, third person |
| `soldiers/Common/Animations/1p_setup.ske` | ~60 | first-person arms and weapon |
| `Vehicles/Air/parachute/parachute_setup.ske` | — | parachute |
| `Common/Flags/flag_setup.ske` | — | flag on a pole |

To look at one:

```bash
ske_info "Game Files/mods/bf2" objects/soldiers/Common/Animations/3p_setup.ske
```

## Next

Animations live separately — 3470 `.baf` files. They have not been taken
apart yet.
