# The game's own shaders — 101 `.fx` files, source and all

`mods/bf2/Shaders_client.zip` holds **the effect source the original
renders with**: 101 files, HLSL/D3DX9 text, not compiled blobs. Nothing
here needed reverse engineering — it is the game's data, like `.con`.

That makes it a first-class source under rule 6: a constant taken from
here is cited as `Shaders_client.zip:RoadCompiled.fx:95`, exactly like an
address in the binary.

What matters to us:

| file | what it settles |
|---|---|
| `RaCommon.fx` | the fog curve, shared by every shader below |
| `RaShaderSTM.fx`, `RaShaderSTMCommon.fx` | static meshes: how base, detail, dirt and crack combine |
| `RaShaderSM.fx`, `RaShaderBM.fx` | skinned and bundled meshes, the same scheme |
| `RoadCompiled.fx`, `Road.fx` | roads: the lift above the terrain and the depth state |
| `TerrainShader.fx` | the terrain, its light map and the detail weights |
| `RaShaderLeaf.fx`, `RaShaderTrunkSTMDetail.fx` | vegetation: leaves and trunks are separate shaders |
| `SkyDome.fx` | the sky, and how the horizon meets the fog |

What is **not** here: `FogRange`, `TexUnpack`, `LightMapOffset`,
`TexBaseInd`/`TexDetailInd` and the rest of the effect parameters are set
by the engine. Their values still come from `BF2.exe`.

## Fog

`RaCommon.fx:51`:

```hlsl
vec4 FogRange : fogRange;
vec4 FogColor : fogColor;

scalar calcFog(scalar w)
{
    half2 fogVals = w*FogRange.xy + FogRange.zw;
    half close = max(fogVals.y, FogColor.w);
    half far = pow(fogVals.x, 3);
    return close-far;
}
```

The argument is `w` of the clip-space position — the distance along the
view. The result is a **visibility** factor: the shaders write it into the
`FOG` register (`Road.fx:66`, `outdata.Fog = saturate(calcFog(outdata.Pos.w))`)
and the fixed-function stage blends `lerp(FogColor, color, fog)`.

Two things follow: the far term is **cubic**, not linear, and the near
term is a separate straight line clamped from below by `FogColor.w`, so
the alpha of the fog colour is a floor on visibility rather than an
opacity.

`FogRange` is **not established**: the engine packs those four numbers
itself, and the name is nowhere in `BF2.exe` — neither as a string nor as
a semantic, so the binding lives in the compiled effect and the code
addresses parameters by handle. The Linux server only stores the setting
(`dice::hfe::GameLogic::setFogStartEndAndBase(Vec4 const&)`); the packing
is client-side. The cheap way to settle it is a measurement rather than a
hunt: a frame dump of the original under `mtld3d` carries the uploaded
constants, and rule 6 takes a frame-dump measurement as a source.

The package also ships a **second, simpler fog**, and this one we can
feed. `Common.dfx:4`:

```hlsl
vec4 fogDistances : fogDistances : register(vs_1_1, c93);

float calcFog(float w)
{
    return ((fogDistances.y - w) / (fogDistances.y - fogDistances.x));
}
```

Visibility, linear between start and end, and the caller blends
`lerp(FogColor, color, fog)`. Written the other way round that is
`mix(color, fogColor, (w - start) / (end - start))` — algebraically the
same thing our renderer already did. So the fog we draw is a shipped BF2
formula after all; what it lacked was the citation, and that is now next
to it in `src/gfx/src/mesh_renderer.cpp`.

## What "there is no fog" actually was: there was no sky

The fog works. Measured on Strike at Karkand, whose
`Renderer.fogStartEndAndBase` is `0.00/135.00/2.30/0.40` and whose
`Renderer.fogColor` is `163/135/86`: a hillside past the fog's end comes
out `150/124/77` on screen — 92 % of the way to the fog colour, the rest
being the terrain's own tint under the light map.

What is missing is the other half of the horizon. The level's `Sky.con`
(Strike at Karkand, `server.zip`) ends like this:

```
run /Common/Sky/SkyDome/skydome.con
Skydome.skyTemplate skydome
Skydome.skyTexture common\textures\sky\karkand_cloudy
Skydome.domeRotation 60
Skydome.hasCloudLayer 0
Skydome.fadeCloudsDistances 900/500
Renderer.fogColor 163.00/135.00/86.00
Renderer.fogStartEndAndBase 0.00/135.00/2.30/0.40
```

Not one `Skydome.*` command had a handler in our engine, so where the
original draws a textured dome we left the clear colour. The terrain faded
correctly into a sandy fog and then met a hard edge of flat blue — which
reads exactly like "the fog is missing".

**Done.** `obf2::level` reads all fourteen `Skydome.*` commands (every one
of the game's 13 levels sets all of them) and `level::buildSkyDome` loads
the mesh the level's own Sky.con names — `run /Common/Sky/SkyDome/skydome.con`,
geometry beside it in `Meshes/`, 667 vertices — putting the level's
`skyTexture` in place of the template's default `skyclear01.dds`. The
renderer draws it in a pass of its own, first, unlit and unfogged: the
dome carries a painted horizon, and `SkyDome.fx` computes no fog either.

Two things about that pass are worth naming. The dome **is** projected the
original's way, with a w of 10 rather than 1 (`SkyDome.fx:100`,
`vec4 posScaled = vec4(input.Pos.xyz, 10.0); //plo: fix for artifacts on
BFO`): scaling a clip vector uniformly leaves the NDC alone, so an
878-unit dome draws as an 88-unit one and stops being cut by the far
plane — ours is the fog's end, 135 on Karkand, and without the trick the
horizon ring falls outside it.

The depth state, on the other hand, is **not** the original's.
`SkyDome.fx:226` keeps `ZWriteEnable = TRUE, ZFunc = LESSEQUAL`, which
works there because the engine's far plane is its view distance rather
than the fog's end. We draw the dome first with no depth at all: the same
picture, as long as nothing is ever meant to stand behind the sky.

Not drawn: the cloud layers (they need the per-frame scroll offsets and a
second dome) and the sun flare (its own additive sprite). Both are read
into `Level::sky` and sit there.

Also not established: the third and fourth components of
`fogStartEndAndBase`. The name accounts for three (`2.30` would be the
base), and Dalian's `0.00/610.00/0.00/0.50` differs in both, so neither is
a constant. They are unused by the linear form.

## Static meshes: `Base` is a tint, `Detail` is the surface

`RaShaderSTM.fx:262`, `getCompositeDiffuse`:

```hlsl
#if _BASE_
    totalDiffuse = tex2D(DiffuseMapSampler, indata.Interpolated[__TEXBASE_INTER].xy);
#endif
#elif _DETAIL_
    vec4 detail = tex2D(DetailMapSampler, indata.Interpolated[__TEXDETAIL_INTER].xy);
#endif
#if (_DETAIL_|| _PARALLAXDETAIL_)
    totalDiffuse *= detail;
#endif
```

So a material named `BaseDetailNDetail` means base **multiplied by**
detail, and the detail has a **UV set of its own**
(`RaShaderSTM.fx:224`, `indata.TexSets[TexDetailInd].xy * TexUnpack`) —
a tiling one, while the base's is unique per surface.

This is what makes tree trunks white. The pine
`objects/vegitation/asia/pine/meshes/nc_pinebig01.staticmesh` has two
ranges:

| range | technique | maps |
|---|---|---|
| 0 (the trunk) | `BaseDetailNDetail` | `nccolor.dds`, `tile_ncbark01de.dds`, `tile_ncbark01deb.dds`, `SpecularLUT_pow36.dds` |
| 1 (the needles) | `Base` | `ncpineleaf01de.dds`, `SpecularLUT_pow36.dds` |

`nccolor.dds` is a 512×512 low-frequency tint shared by the whole of
`vegitation/asia` — on the trunk it is nearly white. All the bark is in
`tile_ncbark01de.dds`, the detail. We drew slot 0 alone
(`mesh_renderer.cpp`, "For ordinary meshes slot 1 is the detail map,
which we do not use yet"), so the trunk came out white while the needles,
whose technique is plain `Base`, came out right.

The same explained every other white surface on a `*Detail*` technique —
the fences and the dumpsters on Strike at Karkand among them.

**Done.** `obf2::mesh::materialLayout` reads the technique into slot
numbers, the vertex carries the tiling UV set as well as the base one, and
the fragment shader multiplies the detail in. The slot ordering is not a
guess: `tools/mesh_info --materials` over the whole corpus shows the crack
map in slot 2 for `BaseDetailCrackNDetailNCrack` (125 materials) and in
slot 3 for `BaseDetailDirtCrackNDetailNCrack` (243) — it moves by exactly
one when `Dirt` appears before it in the name. `tests/test_material.cpp`
holds those counts.

Still not drawn: the dirt and crack channels, and the normal maps, which
need a tangent frame we do not build.

## How a static mesh is lit

`RaShaderSTM.fx` has two lighting paths. With no tangent frame and no
normal maps we are on the vertex one (:209 for the terms, :524 for the
assembly), and `getLightmap` returns `(1,1,1)` where there is no baked
light map (:353):

```hlsl
scalar invDot = 1-saturate(dot(unpackedNormal*0.2, -Lights[0].dir));
Out.InvDotAndLightAtt.rgb   = skyNormal.z * CEXP(StaticSkyColor) * invDot;
Out.ColorOrPointLightFog.rgb = saturate(dot(unpackedNormal, -Lights[0].dir))
                             * CEXP(Lights[0].color);
...
FinalColor.rgb *= 2 * diffuse;
```

Three things are worth pinning down.

`skyNormal` is `vec3(0.78,0.52,0.65)` (:34) and it is a **tangent-space**
constant, not a world direction. The pixel path dots it against the
normal map's sample — `(0,0,1)` when there is no map — which leaves its z
alone, and the vertex path uses that z straight. Dot it against a world
normal and every wall facing away from it goes black. We made exactly that
mistake first.

The `0.2` turns `invDot` into a shallow ramp: 1.0 in shadow, 0.8 in full
sun. That is the ambient floor, and it is why the game's shaded sides are
not black.

The **doubling** at the end is most of why the original's meshes are as
bright as they are.

The colours are the level's, from the `Lightmanager.*` block of its
Sky.con — `staticSunColor`, `staticSkyColor`, `sunDirection`,
`singlePointColor`. All twenty-one commands of that block are read into
`obf2::level::Lighting`; the tree, effect and hemisphere-map values sit
there unused.

Not done: the baked light map. Every level ships one per object,
`lightmaps/Objects/LightmapAtlas*.dds` with a `.tai` of the atlas offsets.
Its three channels are what the formula above multiplies by — `.g` gates
the sun, `.b` the sky, `.r` the point colour — so until it is read nothing
shadows anything.

## Alpha test: leaves, fences, grates

The technique's pass ends with two lines
(`Shaders_client.zip:RaShaderSTM.fx:583`):

```
AlphaTestEnable = < AlphaTest >;
AlphaRef = 127; // temporary hack by johan because "m_shaderSettings.m_alphaTestRef = 127" somehow doesn't work
```

So the reference is **127 of 255**, and whether the test runs at all is a
per-material bool. What sets it is the material's `alphaMode`, which is
straight in the mesh file. Over the game's static meshes it takes exactly
two values — `tools/mesh_info --materials` now prints the distribution:

```
0  BaseDetailNDetail       4008        2  Base                252
0  BaseDetailDirtNDetail   1125        2  BaseDetailNDetail   183
```

2 is the cut-out one. The pine `nc_pinebig01` shows it in miniature: its
trunk range is `alphaMode 0` and its needles `alphaMode 2`.

The test is on the **base** map's alpha, before the detail is multiplied
in. With `_ALPHATEST_` the shader keeps `totalDiffuse.a` and only scales
it by Transparency; without it the alpha is discarded and the detail's
becomes gloss instead (`RaShaderSTM.fx:282`).

## Roads are lifted and do not write depth

`RoadCompiled.fx:95`, in the vertex shader:

```hlsl
wPos.y += .01;
```

One centimetre, in world space, before the projection. And the pass
(`RoadCompiled.fx:194`):

```
AlphaBlendEnable = TRUE;
SrcBlend  = SRCALPHA;
DestBlend = INVSRCALPHA;
ZEnable      = TRUE;
ZWriteEnable = FALSE;
```

The DirectX9 pass of the same technique takes the other route —
`ZEnable = FALSE` with `DepthBias = -0.0001f` and
`SlopeScaleDepthBias = -0.00001f` (`RoadCompiled.fx:215`). The editor's
shader agrees: `Road.fx:59` also does `Pos.y += 0.01`, and its pass p0 is
`ZEnable = TRUE, ZWriteEnable = FALSE`.

We added roads to the scene as ordinary opaque meshes, with the same depth
state as everything else and no lift. The road surface and the terrain
then landed on the same depth and fought for it — that was the flicker.

**Done.** `obf2::gfx::MeshRenderer` now draws roads in a second pass of
its own: the instance is lifted by `kRoadLift`, the pipeline keeps the
depth test and drops the depth write, and the colour is blended by the
texture's alpha. A placement says only `DrawItem::road`; what that means
is the renderer's business.
