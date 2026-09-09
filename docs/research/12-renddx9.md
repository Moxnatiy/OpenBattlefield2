# The renderer is `RendDX9.dll`, not `BF2.exe`

A whole binary the project had never opened. `Game Files/RendDX9.dll` is
2.5 MB and holds the Direct3D 9 renderer: the shader parameters the
`.fx` files declare, the light manager, and the console commands that feed
both. `RendDX9x2.dll` (2.1 MB) is the same renderer built for the
higher shader model — the game picks one at start-up.

How it was found: `Lightmanager.staticSunColor` is set by every level's
Sky.con and its value demonstrably reaches the shader — measured, it is
the triple two rows above the fog in a frame dump. Yet the string
`staticSunColor` is **not in `BF2.exe`**. Neither is `treeSunColor`, nor
`FogRange`, nor `LightMapOffset`. They are all in `RendDX9.dll`:

```
                        RendDX9.dll   RendDX9x2.dll
FogRange                     1              1
FogColor                     2              2
StaticSkyColor               1              1
LightMapOffset               1              1
treeSunColor                 1              1
Lightmanager                 1              3
```

This corrects a wrong conclusion. When `FogRange` could not be found in
`BF2.exe`, the note in [../formats/shaders.md](../formats/shaders.md) said
the binding must therefore live in the compiled effect with the code
addressing parameters by handle. It does not: the search was simply in the
wrong module. Rule 12 says to open the binary rather than reason around
it — the failure here was not reasoning around it but opening the wrong
one, and then explaining the absence instead of doubting the search.

## What this unlocks

Everything the renderer decides is now reachable by name:

| what | the string to start from | why it is wanted |
|---|---|---|
| the fog's packing | `FogRange`, `FogColor` | confirm the four numbers measured from the dump |
| vegetation lighting | `treeSunColor`, `treeSkyColor`, `treeAmbientColor` | trees are lit by the static-mesh formula here and glow; the game has its own |
| the object light map | `LightMapOffset` | confirm the atlas window, and which TEXCOORD set feeds it |
| material channels | `StaticSkyColor`, the `RaShader*` selection | which shader the engine picks for a given `.fx` + technique |

The order of work does not change: the game's data first, then the
binary. What changes is which binary.

## Sources of truth, corrected

`docs/` and CLAUDE.md name `BF2.exe` as the source of behaviour. That is
right for game logic and wrong for anything drawn. For the renderer the
order is:

1. the game's own shaders, `mods/bf2/Shaders_client.zip` — 101 `.fx`
   files of source ([../formats/shaders.md](../formats/shaders.md));
2. `RendDX9.dll` — what the engine puts into them;
3. a frame dump under `mtld3d` — what the numbers actually are on a given
   level, when a value is easier measured than traced.

## Two terrains: the game's and the editor's

The level's `Terrain.con` has two branches, and the argument decides which
one the engine builds:

```
if v_arg1 == BF2Editor
  terrain.create TerrainEditable
  terrain.patchSize 128
  ... the tile texture base names, the tiling, terrain.init
else
  terrain.create Terrain
  terrain.load Levels/Strike_at_Karkand/terraindata.raw
endIf
```

They are two different classes in `RendDX9.dll`, and each caches its own
effect handles at start-up:

| class | where | its effect | SUNCOLOR | GICOLOR |
|---|---|---|---|---|
| `Terrain` — the game's | `FUN_100d7f80`, 0x100d7f80 | `Shaders/TerrainShader` | `+0x20c` | `+0x210` |
| `TerrainEditable` — the editor's | `FUN_1010b380`, 0x1010b380 | `Shaders/TerrainEditorShader` | `+0x83` | `+0x84` (dwords) |

So the editor's terrain really is a different renderer, not the same one
with a different loader — the editor also loads its own
`BF2Editor/Content/Terrain/Grid` overlays there.

**We run the editor's branch**, because the game's takes a single compiled
blob, `terraindata.raw`, which we have not reversed; the editor's names
the loose files we can already read (`HeightmapPrimary.raw`, `Colormaps/`,
`Lightmaps/`, `Detailmaps/`). That is a deliberate deviation, and it costs
us nothing in **data** — the loose files are the same baked tiles the game
ships — but it does mean the `.con` we execute is not the one the game
executes. Where the two branches set the same thing under different
names, the game's spelling is what we follow: the terrain's two colours
are `terrain.sunColor` / `terrain.GIColor`, and the values are identical
to the editor's `LightSettings.*` on every one of the game's twelve levels
that set them (measured over `levels/*/server.zip:Sky.con`).

Debt: `terraindata.raw`. Measure: our terrain is built from it rather than
from the heightmap plus the tile lists, and the patch and chart layout
comes out the same.

The game terrain's own handle list also names `SINGLEPOINTCOLOR_1X` next
to `SUNCOLOR`, `GICOLOR` and `AMBIENTCOLOR`, and the `_1X` was the thread
worth pulling: these colours are **not** uploaded whole.

| what | where | what it stores |
|---|---|---|
| `Terrain::setSunColor` | 0x100db420 | `saturate(colour * 0.25)` at `+0x2e4` |
| `Terrain::setGIColor` | 0x100db520 | `saturate(colour * 0.5)` at `+0x2f0` |
| their getters | 0x100db620, 0x100e2fa0 | multiply back by 4 and by 2 |
| the per-frame push | 0x100d9c30 | those fields into SUNCOLOR / GICOLOR, and `singlePointColor * 0.25` into SINGLEPOINTCOLOR_1X |

The shader undoes both scales, so the ground is lit by one times the
level's own numbers; the quarter is what keeps a sun like Highway Tampa's
2.34 inside a constant register. Worked through in
[../formats/shaders.md](../formats/shaders.md).
