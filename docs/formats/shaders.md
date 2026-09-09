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

Two things follow that our renderer does not do:

* the far term is **cubic**, not linear. Near the camera the fog is
  practically absent, and it thickens sharply towards the end;
* the near term is a separate straight line clamped from below by
  `FogColor.w`, so the alpha of the fog colour is a floor on visibility,
  not an opacity.

`FogRange` is **not measured**: the four numbers are packed by the engine
out of `Renderer.fogStartEndAndBase` (Dalian: `0.00/610.00/0.00/0.50`),
and how exactly is not established. The Linux server has
`dice::hfe::GameLogic::setFogStartEndAndBase(Vec4 const&)` — the name is
transplantable into the client, and that is where to look.

Our own shader (`src/gfx/src/mesh_renderer.cpp`) currently blends
linearly between `fogStart` and `fogEnd`. That number came from nowhere —
it is a rule 6 violation, and it is why the picture does not match either
way round.

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
`tile_ncbark01de.dds`, the detail. We draw slot 0 alone
(`mesh_renderer.cpp`, "For ordinary meshes slot 1 is the detail map,
which we do not use yet"), so the trunk comes out white while the needles,
whose technique is plain `Base`, come out right.

The same explains any other white surface on a `*Detail*` technique.

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

We add roads to the scene as ordinary opaque meshes
(`src/app/main.cpp:1778`), with the same depth state as everything else
and no lift. The road surface and the terrain then land on the same depth
and fight for it — that is the flicker.
