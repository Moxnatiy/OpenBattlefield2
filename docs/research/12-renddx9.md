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
