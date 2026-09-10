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

`FogRange` is **measured**: the engine uploads four floats and a frame dump
of the original under `mtld3d` catches them on the way (rule 6 takes a
frame-dump measurement as a source; `tools/mtld3d_frame_dump_constants.patch`
adds the constants to the dump).

It was measured rather than reversed because I could not find the name in
`BF2.exe` and concluded the binding must live in the compiled effect. That
conclusion was wrong, and wrong in the way rule 12 warns about: **the
renderer is not in `BF2.exe` at all**. It is in `RendDX9.dll`, which holds
`FogRange`, `FogColor`, `StaticSkyColor`, `LightMapOffset` and
`treeSkyColor` as plain strings. The measurement stands; the reason given
for stopping at it did not. See
[../research/12-renddx9.md](../research/12-renddx9.md).

Two levels settle it, because one cannot. Mashtuur City
(`fogStartEndAndBase 30/200/1.40/0.40`) uploads

```
[dump] draw 40 vsc c7: 0.005882 -0.008235 -0.176471 1.176471
[dump] draw 40 vsc c8: 0.827451 0.749020 0.639216 0.400000
```

and with `r = end - start = 170` those are `1/170`, `-1.40/170`,
`-30/170` and `1 + 30/170` exactly. Strike at Karkand (`0/135/2.30/0.40`)
gives `0.007407 -0.017037 -0.000000 1.000000` — the same four with
`start = 0`, down to the negative zero in the third. So:

```
r        = end - start
FogRange = ( 1/r, -base/r, -start/r, 1 + start/r )
FogColor = ( r/255, g/255, b/255, floor )
```

where `floor` is the **fourth** number of `fogStartEndAndBase`, which the
name does not account for at all.

Substituted back into `calcFog`, the packing cancels out and what is left
has no engine in it. With `t = (w - start) / (end - start)`:

```
fogVals.x  = t
fogVals.y  = 1 - base * t
visibility = max(1 - base * t, floor) - t³
```

A straight line of slope `base` down to a floor, and a cubic that finishes
it off at the end. On Karkand that reaches the floor within 35 m — a
dense, close dust, which is what the map is; our old linear ramp gave 0.85
visibility at 20 m where the game gives 0.66. On Dalian `base` is 0, so
the line stays at 1 and only the cubic acts, and the air is clear until
several hundred metres.

The same dump settled something else for free. Two rows above the fog sat
`0.800000 0.740000 0.580000` on Karkand — `Lightmanager.staticSunColor`
exactly. Pairing `Lights[0].color` with that triple had been a reading
from the names; it is now a measurement.

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

With a baked light map the shader keeps the same terms and gates them by
its channels (`RaShaderSTM.fx:358`, the `_LIGHTMAP_` branch):

```hlsl
vec3 bumpedSky = lightmap.b * indata.InvDotAndLightAtt.rgb;
vec3 bumpedDiff = diffuse + bumpedSky;
diffuse = lerp(bumpedSky, bumpedDiff, lightmap.g);
diffuse += lightmap.r * SinglePointColor;
```

That lerp is just `bumpedSky + sun * lightmap.g`, so `.g` gates the sun,
`.b` the sky and `.r` the point colour. Without a map all three are 1 and
the two branches are the same expression — which is why the renderer has
only one.

**Done**, and the format needed no reverse engineering at all: see below.

## The object light map atlas is a self-describing text file

`Levels/<name>/lightmaps/Objects/LightmapAtlas.tai` documents its own
format in its header:

```
# <filename>		<atlas filename>, <atlas idx>, <woffset>, <hoffset>, <width>, <height>
```

and an entry looks like this:

```
levels/strike_at_karkand/lightmaps/objects/house_high_06=00=-226=166=59.dds
        levels/strike_at_karkand/lightmaps/objects/LightmapAtlas0.dds, 0, 0, 0, 0.5, 0.5
```

The left-hand name is what ties a baked map to a **placement**: the
template's name, two digits, and the object's world position. Everything
about it was measured against Strike at Karkand's own data rather than
assumed:

* the position has its fraction **cut off, not rounded**. Of the 1336
  placed objects, truncation matches every one of the 823 that has an
  entry; rounding disagrees on 1115 positions and matches none of them;
* the two digits are geometry and lod. The file holds `00`, `01`, `02`,
  `03` for the lods of geometry 0 (823, 521, 406 and 65 entries) and
  `10`..`12` for a second geometry. We draw lod 0, so we ask for `00`;
* the 513 objects with no entry have none at any lod: they are the
  vegetation and the thin props, which the game lights with its own tree
  shaders instead.

`woffset`/`hoffset` are the offset and `width`/`height` the scale, which
is the reverse of the order the shader wants them in — `LightMapOffset` is
xy scale, zw offset (`RaShaderSTM.fx:216`).

Two consequences for the renderer. The light map has a UV set of its own,
a third one the vertex now carries. And it belongs to the *placement*, not
the geometry: the same building stands on a level thirty times and each
copy has its own window into the atlas, so the mesh is shared and the
light map is not.

**Which UV set is the light map's** was assumed at first and the assumption
was wrong. A comment in our own parser said TEXCOORD2, and taking it turned
whole buildings solid black — a mesh has between one and five sets, the
engine picks per material through `TexLightMapInd`, and that index is not
in the data we can read.

What is in the data is the shape of the sets, and `mesh_info --lightmapuv`
measures it. A light map's unwrap is unique and lies inside [0,1]; a detail
set tiles and runs past ±15. On `house_high_06`:

```
TEXCOORD0 (usage 0x5):   u -3.379..4.993       v -2.751..1.306
TEXCOORD1 (usage 0x105): u -15.806..65535.000  v -3.608..1.557
TEXCOORD2 (usage 0x205): u -15.806..15.991     v -4.012..1.962
TEXCOORD3 (usage 0x305): u -15.806..15.991     v -4.601..1.854
TEXCOORD4 (usage 0x405): u 0.000..0.996        v 0.000..0.953
```

It is the **last** set, not the second. Over the corpus: of the 1253 static
meshes with more than one set, 1021 have their last inside the unit square.
The other 232 are not unwraps, so the loader checks every vertex and gives
those meshes no light map at all rather than putting the whole object on
one texel — which is what produced the black buildings.

`obf2::level::ObjectLightmaps` reads the file and
`tests/test_lightmap_atlas.cpp` holds four of its entries verbatim.

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

## A road is two textures and an edge fade

The depth state was only half of it. `Road.fx:73`:

```hlsl
float4 tex0 = tex2D(sampler0, indata.Tex0);
float4 tex1 = tex2D(sampler1, indata.Tex1);
outcolor.rgb = lerp(tex1.rgb, tex0.rgb, saturate(fBlendFactor));
outcolor.a   = tex0.a;
outcolor.a  *= indata.Alpha;
```

Two textures, each on its own UV set, mixed by a factor — and a per-vertex
alpha on top. All three come straight out of the game's data, from the
road template in `objects/roads/splines/*.con`:

```
RoadTemplate.SetBlendFactor 0.85
RoadTemplate.SetIsPrimaryTexture 1
RoadTemplateTexture.SetTextureFile "objects\roads\textures\road_desert_2lane_512_2"
RoadTemplate.SetIsPrimaryTexture 0
RoadTemplateTexture.SetTextureFile "objects\roads\textures\tarmac_layer1_lowtiling"
```

The primary carries the markings, the secondary is the tiling surface
under them. `SetIsPrimaryTexture` applies to the `SetTextureFile` after
it, so the order in the file is primary then secondary.

We drew the primary alone, on one UV set, and read past the rest of the
vertex — the road mesh's 32 bytes are position, `u/v`, `u1/v1` and the
alpha, and we were taking only the first pair. So roads had no surface
under their markings and their edges ended square instead of fading into
the terrain. Both are read now.

Which of the two goes to `sampler0` is the one inference left here: the
data writes primary first and `Road.fx` samples `detail0: TEXLAYER0` with
`CLAMP` across and `WRAP` along, which is what a marking strip wants and
not what a tiling surface wants. `RendDX9.dll` can settle it —
`BlendFactor` is a string in it.

## The ground's light is a buffer, not a texture lookup

The terrain is not lit in the pass that draws it. BF2 fills a screen-sized
buffer from the tile's baked light map first, and every pass that needs
the ground's light reads that buffer back projectively — the terrain
itself and the roads lying on it.

The fill is `TerrainShader_Hi.fx:588`, and the same pass is written a
second time as hand-made `ps_1_4` beside the technique
(`TerrainShader_Hi.fx:630`), which is the version the game actually
compiles for this technique:

```hlsl
vec4 lightmap = tex2D(sampler0Clamp, indata.Tex0);
vec4 light = saturate(lightmap.z * vGIColor * 2) * 0.5;
light.w = lightmap.y;            // or the shadow map, where it is nearer
```

```
ps_1_4
texld r1, t0
mul r0.xyz, r1.z, c0     // rgb = lightmap.b * vGIColor
+mov_sat r0.w, r1.y      // a   = saturate(lightmap.g)
```

So **green is the sun and blue is the sky**; the red channel no terrain
pass reads. We had the sun on red, and on Karkand's tiles red is nearly
binary — 65% of texels at 0 and 30% at 240 — so the ground had hard
shadows in the wrong places while the soft term in green went unused.
The measured channel means of `lightmaps/tx02x02.dds`, decoded texel by
texel: R 80, G 128 (30% at 0, 61% at 192), B 178.

The two consumers reduce to one expression. The terrain
(`TerrainShader_Hi.fx:81`, and the hand-written `ps_1_4` of the same pass
at :755, `mad r5.xyz, r0_x2.w, c2, r0` then `lrp r0.xyz, v0.x, r0_x2, c0`):

```hlsl
vec4 accumlights = tex2Dproj(sampler1Clamp, indata.Tex1);
vec3 light = 2*accumlights.w * vSunColor.rgb + accumlights.rgb;
vec3 outColor = detailout * colormap * light;
//tl: this 2* is the one missing from the light calculation above!
vec3 fogOutColor = lerp(FogColor, outColor*2, indata.FogAndFade2.x);
```

and the road (`RoadCompiled.fx:127`):

```hlsl
vec4 accumlights = tex2Dproj(sampler2, indata.PosTex);
vec4 light = ((accumlights.w * vSunColor*2) + accumlights)*2;
final.rgb *= light.xyz;
```

Both are `4·a·SunColor + 2·rgb`. `detailout` is the terrain material
system's tiling detail, whose neutral value is one — the game builds it
that way (`lerp(0.5, …) * 4 * lerp(0.5, …)`) — so a renderer without that
system leaves it out and changes nothing.

**Done.** `obf2::gfx::TerrainLightBuffer` is that pass, in its own file;
`MeshRenderer` runs it before the scene and reads it back by screen
position, which is what `tex2Dproj` of a clip position amounts to.

## The terrain's colours are uploaded quartered and halved

Taken whole, the expression above overshoots: on Strike at Karkand a
sunlit texel would be `0.48 · (4·0.75·0.75 + 1) = 1.6`, which clips, and
on Highway Tampa, whose sun is `2.34/1.72/0.56`, worse. Something had to
be scaling them, and the binary says what.

`Terrain::setSunColor` (`RendDX9.dll`, 0x100db420) does not store the
colour it is given:

```
sun[i] = saturate(colour[i] * 0.25)
```

and `Terrain::setGIColor` (0x100db520) the same with `0.5`. The matching
getters multiply back — by 4 (0x100db620) and by 2 (0x100e2fa0) — so this
is storage, not a tint. What is stored is what is uploaded: the terrain's
per-frame constant push (0x100d9c30) reads those very fields into the
SUNCOLOR and GICOLOR handles:

```c
(**(code **)(**(int **)(this + 0x20c) + 0x18))(this + 0x2e4, 0);  // SUNCOLOR <- sun/4
(**(code **)(**(int **)(this + 0x210) + 0x18))(this + 0x2f0, 0);  // GICOLOR  <- gi/2
```

The shader's own factors undo exactly those, so the ground ends up lit by
**one** times the level's numbers:

```
light = 4·lightmap.g·saturate(sun/4) + saturate(2·lightmap.b·saturate(gi/2))
      = lightmap.g·min(sun, 4)       + saturate(lightmap.b·min(gi, 2))
```

The quarter is what keeps a sun of 2.34 inside a constant register at all,
and it caps the sun at 4 and the sky at 2 — which no level in the game
reaches.

The same function scales one more constant at the call site, and names it
in the shader: `SINGLEPOINTCOLOR_1X` is `Lightmanager.singlePointColor`
times 0.25.

We store the scaled pair, since that is what the shader reads
(`MeshRenderer::setTerrainLighting`).

## Which of the two terrain colours the game reads

Sky.con sets the terrain's pair twice, under a condition:

```
if v_arg1 == BF2Editor
  LightSettings.TerrainSunColor 0.75/0.71/0.57
  LightSettings.TerrainSkyColor 0.73/0.64/0.33
else
  terrain.sunColor 0.75/0.71/0.57
  terrain.GIColor  0.73/0.64/0.33
endIf
```

`v_arg1` is the editor's own argument, so **the game runs the `else`** and
the `terrain.*` pair is the one that matters; the `LightSettings.*` names
are the editor's spelling of the same two numbers. We read only the
editor's branch, so every level was lit by our defaults instead of by its
own data. Both spellings are read now.

The names are properties of the `terrain` object in `RendDX9.dll` —
`GIColor` is registered there as a `Vec3` (`FUN_100b6b43`, 0x100b6b43),
alongside `SunColorLow`/`SunColorHigh` and `GIColorLow`/`GIColorHigh`,
which no level in the game sets.

## The compiled road tiles its surface ten times more slowly

`RoadCompiled.fx:126` scales the second UV set on the way into the
sampler:

```hlsl
vec4 t1 = tex2D(sampler1, indata.Tex1*0.1);
```

The editor's `Road.fx` does not. A level ships compiled roads
(`Roads/*_compiled.mesh`), so the tenth applies: without it the asphalt
under the markings is a stripe pattern instead of a surface.

## The ground's structure: one texture, tiled from three directions

A colour map holds about two texels to the metre, so on its own the ground
is a blur from standing height. The game fixes that with one texture per
level — `Levels/<name>/lowdetailtexture.dds` — tiled over the terrain from
three directions and mixed by the surface's normal.

The coordinates come out of the vertex shader
(`Shaders_client.zip:TerrainShader_Shared.fx:244`, and the same lines in
`TerrainShader_Hi.fx:154`):

```hlsl
vec3 tex = vec3(indata.Pos0.y * vTexScale.z, wPos.y * vTexScale.y, indata.Pos0.x * vTexScale.x);
vec2 xPlaneTexCord = tex.xy;
vec2 yPlaneTexCord = tex.zx;
vec2 zPlaneTexCord = tex.zy;

outdata.Tex0b = yPlaneTexCord * vFarTexTiling.z;
outdata.Tex2a = xPlaneTexCord.xy * vFarTexTiling.xy;   outdata.Tex2a.y += vFarTexTiling.w;
outdata.Tex2b = zPlaneTexCord.xy * vFarTexTiling.xy;   outdata.Tex2b.y += vFarTexTiling.w;
outdata.BlendValueAndWater.xyz = saturate(abs(indata.Normal) - vBlendMod);
```

and the mixing out of the pixel shader (`TerrainShader_Shared.fx:177`):

```hlsl
scalar mounten = (xplaneLowDetailmap.y * BlendValue.x) +
                 (yplaneLowDetailmap.x * BlendValue.y) +
                 (zplaneLowDetailmap.y * BlendValue.z);
vec4 outColor = colormap * light * 2 * lerp(0.5, yplaneLowDetailmap.z, lowComponent.x)
                                     * lerp(0.5, mounten, lowComponent.z);
return lerp(outColor*4, terrainWaterColor, BlendValueAndWater.w);
```

Three channels of **one** image: `.z` of the top plane, `.y` of the sides,
`.x` of the top for the mountain mix. `lowComponent` is the patch's own
`LowDetailmaps/txCCxRR.dds` and says how much of it shows — red for the
top, blue for the mountain sides. With both at zero the whole thing is
`4 · 0.5 · 0.5 = 1` and the ground is the colour map alone.

`yPlaneTexCord` is the patch's own UV in [0,1]: the line above it builds
the colour map's coordinates out of the same pair, and a colour map covers
exactly one patch.

Where the four constants come from:

| constant | value | source |
|---|---|---|
| `vFarTexTiling.xy` | `terrain.farSideTiling` (5/5 on Karkand) | Terrain.con |
| `vFarTexTiling.z` | `terrain.farTopTilingHi` (24) or `farTopTilingLow` (4) | Terrain.con; the video setting picks, and `RendDX9.dll` 0x100d9c30 reads the field at `+0x330` or `+0x32c` by a flag at `+0x35e` |
| `vFarTexTiling.w` | `terrain.farYOffset` (0) | Terrain.con |
| `vTexScale.y` | **-0.00615148** | a constant in the engine, `RendDX9.dll` 0x100d9c30: TEXSCALE is pushed as `(255/n, -0.00615148, 255/n, 0)` |
| `vBlendMod` | `float3(0.2, 0.5, 0.2)` | the shader's own default, `TerrainShader.fx:75` — no `BLENDMOD` string exists in `RendDX9.dll`, so nothing overwrites it |
| `vDetailTex` | `((n-1)/n, 1/(2n))` | `RendDX9.dll` 0x100d9c30 — the half-texel correction for a per-patch map of `n` texels (`terrain.lowDetailmapSize`, 512) |

**Done.** `MeshRenderer::setTerrainDetail` takes the level's texture and its
tilings; the patch's `lowComponent` rides in the range's third slot, where
the chart map used to sit unused.

**Not done.** The *near* detail — `dsampler3Wrap` in `Hi_PS_FullDetail` — is
a texture per terrain material, and the materials live in `terraindata.raw`
(docs/research/12-renddx9.md). That is the last piece of the ground.
