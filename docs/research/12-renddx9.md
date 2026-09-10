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

## Every light colour exists twice: `X` and `X_1X`

The renderer keeps a table of effect-parameter names against offsets into
the light manager's block of values, built in `FUN_10039940` (0x10039940,
the registration runs from 0x1003a380 on). Colours appear in it in pairs:

| name | offset | pushed at |
|---|---|---|
| `StaticSkyColor` | 0x1f4 | 0x1003a46a |
| `StaticSkyColor_1X` | 0x204 | 0x1003a4a4 |
| `StaticSpecularColor` | 0x210 | 0x1003a4de |
| `StaticSpecularColor_1X` | 0x220 | 0x1003a518 |
| `SinglePointColor` | 0x22c | 0x1003a552 |
| `TerrainSunColor` | 0x118 | 0x1003a430 |

`SinglePointColor_1X` is in the same string block (0x101e1680). So a
shader picks not only *which* colour it wants but at which scale, by the
name it declares — which is how one engine feeds both the `ps_1_4` and the
`ps_2_0` builds of the same RaShader: `RaCommon.fx:63` doubles every
constant it reads (`CEXP`) below shader model 2.0, and says outright that
they are "`_d2` on CPU to fit [-1,+1] range".

Not established: which of the two fields holds the level's number whole.
Our static-mesh path draws with `CEXP` as the identity — the 2.0 case — so
it uses `Lightmanager.staticSkyColor` as the data gives it, and the
buildings sit in the same exposure as the ground. Measure: the writer of
0x1f4 and 0x204, or a frame dump of the constants the STM shader is given.

## Vegetation is drawn by three shaders, and the engine picks per material

Trees glow in our render because we light them the way we light a wall.
The engine does not: it has a separate family of shaders for vegetation and
chooses between them per material.

`FUN_101174d0` (0x101174d0) registers every variant against a 32-bit key:

| shader | key | what it is |
|---|---|---|
| `Leaf` | 0xf0000000 | `RaShaderLeaf.fx` |
| `LeafShadowed` | 0xf0000200 | + the shadow map |
| `Leafpointlight` | 0xf0000400 | the point-light pass |
| `LeafpointlightShadowed` | 0xf0000600 | both |
| `TrunkSTMBase` | 0xfa000000 | a trunk with no detail channel |
| `TrunkSTMDetail` | 0xf9000000 | a trunk with one |
| `TrunkSTMBaseShadowed` / `TrunkSTMDetailShadowed` | + 0x200 | the same, shadowed |
| `Road` / `RoadDetail` / `RoadDetailNoBlend` | 0xf1/0xf2/0xf4 000000 | the editor's roads |
| `Water` / `WaterBase` | 0xf8000000 | water |
| `Default` | 0x01000000 | everything else |

So 0x200 is "shadowed" and 0x400 "point light", and the high byte names
the family.

The choice is made while the vegetation is drawn, `FUN_100fcc70`
(0x100fcc70), on a flag at **`+0x1ec` of the material record** (the records
are an array of stride 0x204):

```c
if (*(char *)(material + 0x1ec) == '\0') {           // a trunk
    key  = shadowed ? 0x200 : 0;
    key |= (flags & 4) == 0 ? 0xfa000000 : 0xf9000000;
    ...
    colour = LightManager[+0x1e8]();                 // staticSkyColor
} else {                                             // a leaf
    colour = LightManager[+0x1c8]();                 // not the static pair
    key = shadowed ? 0xf0000200 : 0xf0000000;
    ...
    colour2 = LightManager[+0x40]();
    ... WindManager ...
}
```

Two things follow. A leaf is lit from **different colours entirely** — the
getters at `+0x1c8` and `+0x40`, not the `+0x1e8` the trunk path uses,
which is `staticSkyColor` (proved below). The level's Sky.con sets
`treeSunColor`, `treeSkyColor` and `treeAmbientColor` and we read them
without using them; this is where they go. And a leaf takes its sway from
the WindManager, which is `RaShaderLeaf.fx`'s `GlobalTime`/`WindSpeed`.

### The engine decides vegetation by the path

`StaticMeshTemplate::load` (`FUN_1011acd0`, 0x1011acd0 — the assert beside
it names `Code\BF2\Geom\StaticMeshTemplate.cpp`) searches the name of the
file it is about to load:

```c
this->isVegetation = name.find("vegitation") != npos;      // +0x29e
this->isWater      = name.find("objects/water/") != npos;
this->isRoad       = name.find("objects/roads/") != npos;
if (this->isRoad) this->noBlend = name.find("noBlend") != npos;
```

So the directory the artists put a mesh in **is** the engine's rule, not a
convention on top of it — the misspelling included. That settles the outer
flag: a mesh under `objects/vegitation/` is drawn by the tree shaders, and
one anywhere else is not. `obf2::mesh::isVegetationPath` is that line.

**Not established: where the `+0x1ec` flag comes from.** The mesh's own
material does not carry it, and that is measured rather than assumed:
`mesh_info --leafflag` walks every material in the game, splits them by
whether the artists named the base texture `leaf*`, and prints what the
parsed fields look like on each side. Over 198 leaf materials and 6100
others, nothing separates them —

```
base texture named leaf*: 198 materials
    alphaMode  0:1 2:197
    u5         0:44 3:2 5:1 16:8 512:1 1304:1 ...
    techniques :22 Base:176
every other material: 6100 materials
    alphaMode  0:5827 1:1 2:272
    u5         0:715 1:4 2:9 3:6 4:4 5:1 ...
    techniques :9 Base:218 BaseDetail:117 ...
```

— `alphaMode 2` is nearly universal among leaves but 272 other materials
carry it too (fences, grates), and the two unnamed shorts after the node
index hold values like 12767488, which is a pointer left in the file. The
object's `.tweak` says nothing about leaves either.

The outer flag `+0x29e` is settled (the path test above); this inner one is
not. What we go by instead is the state the leaf shader itself sets — alpha
test at `AlphaRef = 127` with no culling (`RaShaderLeaf.fx:233`), which for
us is `alphaMode == 2`. Inside the game's vegetation meshes that agrees with
the artists' texture names wherever they can be checked: 249 of 251
leaf-named materials are alpha-tested (`mesh_info --leafflag`). The 29
alpha-tested materials named otherwise are leaves too, by their geometry.
It is an inference, and `obf2::mesh::markVegetationLeaves` says so. What the data does carry is
`ObjectTemplate.mapMaterial 0 leafCol 1007` beside
`mapMaterial 1 wood_col 93`, and textures named `leaf_*.dds`; over the 123
vegetation tweaks the names divide cleanly (91 leaf, 74 wood). Both are the
artists' conventions, not something the engine is shown to read, so
matching on them would be a workaround and not a port (rule 12). Measure:
the writer of `+0x1ec`, or a frame dump of the original that shows which
shader a leaf draw is given.

## The static colours are uploaded whole

`FUN_1002c2e0` (0x1002c2e0), the renderer's per-frame gather, writes the
light manager's colours into the block the effect parameters bind to:

```c
p = LightManager[+0x1e8]();                    // staticSkyColor
*(base + 0x1f8) = p[0];  *(base + 0x1fc) = p[1];  *(base + 0x200) = p[2];
p = LightManager[+0x1e0]();                    // StaticSpecularColor -> base + 0x214
p = LightManager[+0x1f0]();                    // SinglePointColor    -> base + 0x230
```

`base` is `DAT_1023ddbc`, and the offsets are the registry's plus four —
`StaticSkyColor` is registered at 0x1f4, `StaticSpecularColor` at 0x210,
`SinglePointColor` at 0x22c. No scaling on the way in: these three reach
the shader as the level's data writes them. So the `_1X` pairing is not a
halving of these fields, and our static-mesh path is right to use
`Lightmanager.staticSkyColor` as it stands — which the frame dump already
said about the sun (docs/formats/shaders.md).

One field nearby *is* halved — `base + 0x154`, from the getter at `+0x1d0`
— so the two conventions live side by side and each field has to be
checked rather than assumed.
