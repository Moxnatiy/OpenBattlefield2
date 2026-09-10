#include "obf2/gfx/mesh_renderer.h"

#include <cstring>

#include "obf2/gfx/terrain_light.h"
#include "obf2/mesh/material.h"

namespace obf2::gfx {
namespace {

// The original lifts a road one centimetre in world space before the projection
// (`Shaders_client.zip:RoadCompiled.fx:95`, `wPos.y += .01;`; the editor's
// shader agrees, Road.fx:59). Our roads are placed by a plain translation, so
// lifting the instance is the same thing.
constexpr float kRoadLift = 0.01f;

// The shaders are supplied as MSL text: SDL_GPU hands them to the Metal compiler
// at runtime, so for macOS a separate shader build step is not needed yet.
// When Windows comes, these same shaders will also have to exist as SPIR-V/DXIL.
constexpr const char* kShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float2 uv2      [[attribute(3)]];
    float2 uvLightmap [[attribute(4)]];
    float  alpha    [[attribute(5)]];
    float3 tangent  [[attribute(6)]];
    float2 uvDirt   [[attribute(7)]];
    float2 uvCrack  [[attribute(8)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
    // The detail map has a tiling UV set of its own
    // (`Shaders_client.zip:RaShaderSTM.fx:224`), not the base map's unwrap.
    float2 uv2;
    // The unwrap the baked light map is sampled with.
    float2 uvLightmap;
    // The dirt and the crack channels' own unwraps.
    float2 uvDirt;
    float2 uvCrack;
    // A road's edge fade, carried per vertex; 1 on everything else.
    float vertexAlpha;
    float viewDepth;
    // The frame's parameters travel to the fragment shader through varyings
    // rather than in a uniform buffer of their own: in the fragment stage they
    // never reach the shader (other data is read instead), while the vertex
    // stage works reliably. The values are constant, so interpolation is harmless.
    float4 fogColor;
    float4 fogParams;
    float4 sunColor;
    float4 skyColor;
    float4 material;
    float4 sunDirection;
    float4 pointColor;
    float4 lightmapOffset;
    float4 fogShape;
    float4 roadParams;
    float4 terrainTiling;
    float4 terrainDetail;
    float4 treeSunColor;
    float4 treeAmbientColor;
    float4 cameraPosition;
    float4 staticSpecular;
    float3 worldPosition;
    // The tangent frame, in world coordinates. The binormal is built here and
    // not in the fragment stage because it is the same for the whole triangle.
    float3 tangent;
    float3 binormal;
    // The vertex's own height. The terrain stands in world coordinates with no
    // transform, so this is the world Y the side planes are textured by.
    float localY;
};

struct Uniforms {
    float4x4 modelViewProjection;
    // The placement's own matrix. The lighting works in world coordinates and
    // the mesh's normals are in the object's, so a rotated building would
    // otherwise be lit as though it stood the way it was modelled — some walls
    // bright that should be shaded, and the object next to it the other way
    // round. Rigid placements only, so the upper 3x3 is enough for a normal.
    float4x4 model;
    float4 fogColor;   // rgb — the fog's colour
    float4 fogParams;  // x: start, y: end (0 = no fog), z: light map mode, w: detail tiling
    // The sun's and the sky's colours for whichever surface is being drawn:
    // TerrainSunColor/TerrainSkyColor for the terrain, `Lightmanager.staticSunColor`
    // and `staticSkyColor` for everything else. Which one is meant is decided by
    // the same fogParams.z that picks the branch below, and the CPU pushes the
    // right pair per range.
    float4 sunColor;
    float4 skyColor;
    // x: multiply the detail map in (a mesh material with a Detail channel);
    // y: take the alpha from the texture rather than 1 (the road pass);
    // z: the sky dome — unlit, and projected with a w of 10 (see below);
    // w: cut the surface out by the base map's alpha.
    float4 material;
    // `Lightmanager.sunDirection` — the way the light travels, so the vector
    // towards the sun is its negative. w unused.
    float4 sunDirection;
    // `Lightmanager.singlePointColor`, added whole. rgb.
    float4 pointColor;
    // The object's window into the level's light map atlas: xy scale, zw
    // offset, exactly as the game's `LightMapOffset`
    // (`Shaders_client.zip:RaShaderSTM.fx:216`). All zero means this object has
    // no baked light map — the vegetation and the thin props have none.
    float4 lightmapOffset;
    // x: the fog's `base`, the slope of its near ramp. Its floor rides in
    // `fogColor.w`, which is where the engine keeps it too.
    float4 fogShape;
    // x: a road's blend factor, how hard its markings sit over the tiling
    // surface under them (`RoadTemplate.SetBlendFactor`).
    // zw: the render target's size in pixels. The terrain's light buffer is
    // read by screen position, the way the game reads it — `tex2Dproj` of the
    // clip position (`Shaders_client.zip:RoadCompiled.fx:127`) — and a fragment
    // shader in Metal is handed that position in pixels, not normalised.
    float4 roadParams;
    // How many times the level's low-detail texture repeats over one patch:
    // xy on the two side planes, z on the top, w slides the sides up the
    // texture. The engine pushes exactly this vector
    // (`Shaders_client.zip:TerrainShader.fx:67`, FARTEXTILING; the values are
    // `terrain.farSideTiling`, `farTopTilingHi`, `farYOffset` from Terrain.con).
    float4 terrainTiling;
    // x, y: the half-texel correction the patch's own UV needs before it
    // addresses a per-patch map — the engine's DETAILTEX, `((n-1)/n, 1/(2n))`
    // for a map of n texels (`RendDX9.dll`, 0x100d9c30).
    // z: `vTexScale.y`, the scale the side planes take the world height by.
    // w: 1 when this patch has a low-detail component map and the level a
    // low-detail texture; 0 leaves the ground as the colour map alone.
    float4 terrainDetail;
    // The two colours a leaf is lit by, as the level's Sky.con writes them:
    // `Lightmanager.treeSunColor` and `treeAmbientColor`. Measured in a frame
    // dump of the original (docs/formats/shaders.md); w of the sun is 1 for a
    // leaf material and 0 for everything else.
    float4 treeSunColor;
    float4 treeAmbientColor;
    // Where the eye is, in world coordinates — the specular needs the vector to
    // it and nothing else in the frame does. w unused.
    float4 cameraPosition;
    // rgb: `Lightmanager.staticSpecularColor`, which reaches the shader whole
    // (measured: `psc c1` on Karkand is 0.65/0.60/0.52, its Sky.con value).
    // w: `StaticGloss`, the engine's own 0.2 (`psc c2` on 678 draws of the same
    // frame), which a material may override and we do not read that yet.
    float4 staticSpecular;
};

// The order of the fields above is the order of `VertexUniforms` below, and the
// two are one buffer: swap two of them and the shader reads a light direction
// where a material flag should be. There is no compiler to catch that — the
// symptom is a picture, and the last time it was a black sky over red walls.


vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(0)]]) {
    VertexOut out;
    // The sky dome is projected with a w of 10 rather than 1 — the original's
    // own trick (`Shaders_client.zip:SkyDome.fx:100`,
    // `vec4 posScaled = vec4(input.Pos.xyz, 10.0); //plo: fix for artifacts`).
    // Scaling the clip vector uniformly leaves the NDC untouched, so this draws
    // the dome as though it were a tenth of its size around the camera: an
    // 878-unit dome becomes 88, and it stops being cut by the far plane. Ours
    // is the fog's end — 135 on Strike at Karkand — so without this the horizon
    // ring falls outside it.
    const float w = uniforms.material.z > 0.5 ? 10.0 : 1.0;
    out.position = uniforms.modelViewProjection * float4(in.position, w);
    out.normal = (uniforms.model * float4(in.normal, 0.0)).xyz;
    out.uv = in.uv;
    out.uv2 = in.uv2;
    out.uvLightmap = in.uvLightmap;
    out.uvDirt = in.uvDirt;
    out.uvCrack = in.uvCrack;
    out.vertexAlpha = in.alpha;
    // For a perspective projection w in clip space equals the distance along the
    // view — exactly what the fog needs.
    out.viewDepth = out.position.w;
    out.fogColor = uniforms.fogColor;
    out.fogParams = uniforms.fogParams;
    out.sunColor = uniforms.sunColor;
    out.skyColor = uniforms.skyColor;
    out.material = uniforms.material;
    out.sunDirection = uniforms.sunDirection;
    out.pointColor = uniforms.pointColor;
    out.lightmapOffset = uniforms.lightmapOffset;
    out.fogShape = uniforms.fogShape;
    out.roadParams = uniforms.roadParams;
    out.terrainTiling = uniforms.terrainTiling;
    out.terrainDetail = uniforms.terrainDetail;
    out.treeSunColor = uniforms.treeSunColor;
    out.treeAmbientColor = uniforms.treeAmbientColor;
    out.cameraPosition = uniforms.cameraPosition;
    out.staticSpecular = uniforms.staticSpecular;
    out.worldPosition = (uniforms.model * float4(in.position, 1.0)).xyz;
    // The frame a normal map is read in. The game builds it the same way
    // (`Shaders_client.zip:RaShaderSTM.fx:130`, getTanBasisTranspose):
    //
    //   binormal = normalize(cross(Tan, Normal)) * flip
    //
    // where `flip` comes from the w of the engine's own compressed position and
    // the file we read carries no such field. So the sign is left at +1 and
    // written down as the one guess here.
    out.tangent = (uniforms.model * float4(in.tangent, 0.0)).xyz;
    out.binormal = cross(out.tangent, out.normal);
    out.localY = in.position.y;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              texture2d<float> baseColor [[texture(0)]],
                              texture2d<float> lightmap [[texture(1)]],
                              texture2d<float> detail [[texture(2)]],
                              texture2d<float> terrainLight [[texture(3)]],
                              texture2d<float> terrainDetailMap [[texture(4)]],
                              texture2d<float> normalMap [[texture(5)]],
                              texture2d<float> dirtMap [[texture(6)]],
                              texture2d<float> crackMap [[texture(7)]],
                              sampler baseSampler [[sampler(0)]],
                              sampler lightSampler [[sampler(1)]],
                              sampler detailSampler [[sampler(2)]],
                              sampler terrainLightSampler [[sampler(3)]],
                              sampler terrainDetailSampler [[sampler(4)]],
                              sampler normalSampler [[sampler(5)]],
                              sampler dirtSampler [[sampler(6)]],
                              sampler crackSampler [[sampler(7)]]) {
    float4 albedo = baseColor.sample(baseSampler, in.uv);

    // Leaves, fences, grates: the shape is cut out of the base map's alpha.
    // The technique switches it on per material (`AlphaTestEnable = <AlphaTest>`,
    // `Shaders_client.zip:RaShaderSTM.fx:583`) and the line under it sets the
    // reference: `AlphaRef = 127`, with the author's own note that it is there
    // because the setting it should come from does not work. 127 of 255.
    //
    // It is the **base** map's alpha, before the detail is multiplied in: with
    // `_ALPHATEST_` the shader keeps `totalDiffuse.a` and only scales it by
    // Transparency, while without it the alpha is thrown away and the detail's
    // becomes gloss instead (`RaShaderSTM.fx:282`).
    if (in.material.w > 0.5 && albedo.a < 127.0 / 255.0) discard_fragment();

    // A road is two textures, not one. The template names both and a factor to
    // mix them by, and the shader mixes them exactly that way
    // (`Shaders_client.zip:Road.fx:73`):
    //
    //     float4 tex0 = tex2D(sampler0, indata.Tex0);   // the markings
    //     float4 tex1 = tex2D(sampler1, indata.Tex1);   // the tiling surface
    //     outcolor.rgb = lerp(tex1.rgb, tex0.rgb, saturate(fBlendFactor));
    //     outcolor.a   = tex0.a;
    //     outcolor.a  *= indata.Alpha;
    //
    // Each has its own UV set — the markings on the mesh's first, the surface
    // on its second — and the per-vertex alpha is what fades a road's edge
    // into the terrain. We drew the markings alone on one set and let the edges
    // end square.
    //
    // The compiled road, which is what a level ships, scales that second set by
    // a tenth on the way in (`RoadCompiled.fx:126`,
    // `tex2D(sampler1, indata.Tex1*0.1)`), so the surface tiles ten times more
    // slowly than the numbers in the mesh say. Without it the asphalt is a
    // stripe pattern instead of a surface.
    if (in.material.y > 0.5) {
        float4 surface = detail.sample(detailSampler, in.uv2 * 0.1);
        albedo.rgb = mix(surface.rgb, albedo.rgb, saturate(in.roadParams.x));
    }

    // A material whose technique names a `Detail` channel is the base
    // **multiplied by** the detail, and the detail is sampled with the tiling
    // UV set (`Shaders_client.zip:RaShaderSTM.fx:262`, getCompositeDiffuse:
    // `totalDiffuse *= detail`). Without this only the base is drawn, and on
    // surfaces whose base map is a low-frequency tint — tree trunks, fences,
    // dumpsters — the base alone is very nearly white.
    //
    // The detail's alpha has a second job: where a material has a detail map
    // and is not alpha-tested, it **is** the gloss the specular is scaled by
    // (`RaShaderSTM.fx:283`, `gloss = detail.a`) — the same channel the alpha
    // test would otherwise have used.
    float gloss = in.staticSpecular.w;
    if (in.material.x > 0.5) {
        const float4 detailSample = detail.sample(detailSampler, in.uv2);
        albedo *= detailSample;
        if (in.material.w < 0.5) gloss = detailSample.a;
    }

    // The two channels after the detail, in the order `getCompositeDiffuse`
    // applies them (`Shaders_client.zip:RaShaderSTM.fx:293`):
    //
    //   #if _DIRT_   totalDiffuse.rgb *= tex2D(DirtMapSampler, …).rgb;
    //   #if _CRACK_  totalDiffuse.rgb = lerp(totalDiffuse.rgb, crack.rgb, crack.a);
    //
    // The dirt is a **multiply**, and dropping it is why our pylons and fuel
    // tanks came out pale against the buildings around them: a grimy surface
    // was being lit as though it were clean. 1368 of the game's materials name
    // a Dirt channel and 368 a Crack.
    //
    // Each reads its own unwrap — the set the technique's order gives it, the
    // same enumeration that decides the texture slots.
    if (in.fogShape.z > 0.5) {
        albedo.rgb *= dirtMap.sample(dirtSampler, in.uvDirt).rgb;
    }
    if (in.fogShape.w > 0.5) {
        const float4 crack = crackMap.sample(crackSampler, in.uvCrack);
        albedo.rgb = mix(albedo.rgb, crack.rgb, crack.a);
    }

    float3 light;
    // The normal the specular is taken against: the map's where there is one.
    float3 shadingNormal = normalize(in.normal);
    // fogShape.y says whether this material has a normal map and which unwrap
    // reads it: 0 none, 1 the base map's, 2 the tiling one.
    const float normalStrength = in.fogShape.y > 0.5 ? 1.0 : 0.0;
    // The object's baked light map, or white where it has none. Declared here
    // and not in the branch below: the specular is gated by its green channel
    // too, and that is computed after the branch chain.
    float3 lightmapValue = float3(1.0);
    if (in.fogParams.z > 1.5) {
        // The ground's light, read back out of the buffer it was drawn into —
        // for the terrain itself and for the roads lying on it. The buffer is
        // `obf2::gfx::TerrainLightBuffer`, which holds the game's own
        // ZFillLightmap pass; here is the other half, the two places that
        // consume it.
        //
        // The terrain (`Shaders_client.zip:TerrainShader_Hi.fx:81`):
        //
        //     vec4 accumlights = tex2Dproj(sampler1Clamp, indata.Tex1);
        //     vec3 light = 2*accumlights.w * vSunColor.rgb + accumlights.rgb;
        //     vec3 outColor = detailout * colormap * light;
        //     //tl: this 2* is the one missing from the light calculation above!
        //     vec3 fogOutColor = lerp(FogColor, outColor*2, indata.FogAndFade2.x);
        //
        // and the road (`Shaders_client.zip:RoadCompiled.fx:127`):
        //
        //     vec4 accumlights = tex2Dproj(sampler2, indata.PosTex);
        //     vec4 light = ((accumlights.w * vSunColor*2) + accumlights)*2;
        //     final.rgb *= light.xyz;
        //
        // Both come to the same expression — 4·a·SunColor + 2·rgb — so there is
        // one branch for the two. `sunColor` here is the terrain's pair
        // (`terrain.sunColor`), which the CPU pushes for both.
        float2 screenUv = in.position.xy / max(in.roadParams.zw, float2(1.0, 1.0));
        float4 accum = terrainLight.sample(terrainLightSampler, screenUv);
        light = 4.0 * accum.a * in.sunColor.rgb + 2.0 * accum.rgb;

        // And `detailout`: the ground's own structure, which the colour map
        // does not have — it holds about two texels to the metre. The game
        // tiles one texture per level over the terrain from three directions
        // and mixes them by the surface's normal
        // (`Shaders_client.zip:TerrainShader_Shared.fx:244` for the
        // coordinates, :177 for the mixing):
        //
        //   tex = (Pos0.y*vTexScale.z, wPos.y*vTexScale.y, Pos0.x*vTexScale.x)
        //   xPlaneTexCord = tex.xy;  yPlaneTexCord = tex.zx;  zPlaneTexCord = tex.zy
        //   Tex0b = yPlaneTexCord * vFarTexTiling.z
        //   Tex2a = xPlaneTexCord * vFarTexTiling.xy;  Tex2a.y += vFarTexTiling.w
        //   Tex2b = zPlaneTexCord * vFarTexTiling.xy;  Tex2b.y += vFarTexTiling.w
        //   BlendValue = saturate(abs(Normal) - vBlendMod), divided by its own sum
        //
        //   mounten  = xplane.y*Blend.x + yplane.x*Blend.y + zplane.y*Blend.z
        //   outColor = colormap * light * 2
        //            * lerp(0.5, yplane.z, lowComponent.x)
        //            * lerp(0.5, mounten,  lowComponent.z) ... * 4
        //
        // `yPlaneTexCord` is the patch's own UV in [0,1]: the line above it
        // builds the colour map's coordinates out of the same pair, and a
        // colour map covers exactly one patch. `vBlendMod` is not something the
        // engine sets — the shader declares it with its value
        // (`TerrainShader.fx:75`, `float3(0.2, 0.5, 0.2)`) and no `BLENDMOD`
        // string exists in `RendDX9.dll` to overwrite it.
        //
        // The three channels are not three textures: the game reads `.z` of the
        // top plane and `.y` of the sides out of one image, and `.x` of the top
        // for the mountain mix.
        if (in.terrainDetail.w > 0.5) {
            const float2 patchUv = in.uv;
            const float height = in.localY * in.terrainDetail.z;
            const float2 yUv = patchUv * in.terrainTiling.z;
            const float2 xUv = float2(patchUv.y, height) * in.terrainTiling.xy +
                               float2(0.0, in.terrainTiling.w);
            const float2 zUv = float2(patchUv.x, height) * in.terrainTiling.xy +
                               float2(0.0, in.terrainTiling.w);
            const float4 yPlane = terrainDetailMap.sample(terrainDetailSampler, yUv);
            const float4 xPlane = terrainDetailMap.sample(terrainDetailSampler, xUv);
            const float4 zPlane = terrainDetailMap.sample(terrainDetailSampler, zUv);

            float3 blend = saturate(abs(normalize(in.normal)) - float3(0.2, 0.5, 0.2));
            blend /= max(blend.x + blend.y + blend.z, 0.0001);
            const float mounten =
                xPlane.y * blend.x + yPlane.x * blend.y + zPlane.y * blend.z;

            // How much of it shows here: the patch's own `lowComponent` map,
            // sampled with the same UV and the engine's half-texel correction.
            const float4 lowComponent = detail.sample(
                detailSampler, patchUv * in.terrainDetail.x + in.terrainDetail.y);
            albedo.rgb *= 4.0 * mix(0.5, yPlane.z, lowComponent.x) *
                          mix(0.5, mounten, lowComponent.z);
        }
    } else if (in.fogParams.z > 0.5) {
        // Terrain. The game draws it in several passes: one fills an
        // accumulation buffer from the tile's baked light map, and the colour
        // pass reads that buffer back projectively
        // (`Shaders_client.zip:TerrainShader_Hi.fx:588` and
        // `TerrainShader_Shared.fx:170`):
        //
        //   accum.rgb = saturate(lightmap.z * vGIColor * 2) * 0.5;
        //   accum.w   = lightmap.y;                  // or the shadow map, when nearer
        //   light     = 2 * accum.w * vSunColor + accum.rgb;
        //   outColor  = colormap * light * 2;
        //
        // The same pass exists as hand-written ps_1_4 next to the technique
        // (TerrainShader_Hi.fx:630) and says it without the doublings, since
        // those constants are uploaded halved for 1.x:
        //
        //   texld r1, t0
        //   mul r0.xyz, r1.z, c0     // rgb = lightmap.b * vGIColor
        //   +mov_sat r0.w, r1.y      // a   = saturate(lightmap.g)
        //
        // So the sun rides the light map's **green** channel and the sky its
        // blue; the red one is not read by any terrain pass. We had the sun on
        // red, which is why the ground had shadows in the wrong places: on
        // Karkand's tiles red is nearly binary (endpoints cluster at 0 and 255)
        // while green carries the soft sun term.
        float3 baked = lightmap.sample(lightSampler, in.uv).rgb;
        light = 2.0 * baked.g * in.sunColor.rgb +
                0.5 * saturate(2.0 * baked.b * in.skyColor.rgb);
        // `colormap * light * 2` is the first half of Shared_PS_LowDetail. The
        // rest of that line — `* lerp(yplaneLowDetailmap.x, .z, blend)` and a
        // final `* 2` — is the low-detail texture, which needs the terrain
        // material system we do not have. The two are left out together: the
        // detail texture is centred on mid-grey, so dropping it and keeping its
        // doubling would brighten the whole ground by two.
        light *= 2.0;

        // The detail map is NOT multiplied in here. It turned out to be not a
        // colour but a weight map: the R/G/B channels give the shares of the
        // different terrain materials, and each of them has its own texture from
        // MaterialManager. Multiplying it in as a colour gives acid stains. The
        // texture is loaded and sits in slot 2 until there is a terrain material system.
        (void)detail;
        (void)detailSampler;
    } else if (in.treeSunColor.w > 0.5) {
        // A leaf. The game does not light it like a wall: it has a shader of
        // its own and colours of its own, and both were measured out of the
        // original (docs/formats/shaders.md, "Measured on Karkand").
        //
        // `RaShaderLeaf.fx:95` with the constants the engine uploads —
        // `Lights[0].color = treeSunColor/2`, `OverGrowthAmbient =
        // treeAmbientColor` — and the `ps_1_3` block at :207 whose `mul_x4`
        // multiplies the vertex colour by four:
        //
        //   LdotN     = saturate((dot(N, -Lights[0].dir) + 0.6) / 1.4)
        //   Color.rgb = Lights[0].color * LdotN + OverGrowthAmbient / CEXP(1)
        //   Color     = Color * 0.5
        //   out       = diffuseMap * CEXP(Color) * 2
        //
        // With `CEXP(x) = 2x` below shader model 2.0 the halvings cancel and
        // what is left is one line. The ramp never falls below 0.43, so a leaf
        // in shadow is dimmer and not black — and there is no sky term at all,
        // which is what keeps trees out of the glow the static formula gives
        // them.
        float3 normal = normalize(in.normal);
        float3 toSun = normalize(-in.sunDirection.xyz);
        float lDotN = saturate((dot(normal, toSun) + 0.6) / 1.4);
        light = in.treeSunColor.rgb * lDotN + in.treeAmbientColor.rgb;
    } else {
        // Everything that is not terrain, lit the way the game lights a static
        // mesh. We have no tangent frame and read no normal maps, which picks
        // the shader's own vertex path — `Shaders_client.zip:RaShaderSTM.fx:209`
        // for the terms and :524 for how they are put together, with
        // `getLightmap` returning (1,1,1) where there is no baked light map
        // (RaShaderSTM.fx:353):
        //
        //   invDot    = 1 - saturate(dot(N * 0.2, L))
        //   bumpedSky = skyNormal.z * StaticSkyColor * invDot
        //   sun       = saturate(dot(N, L)) * Lights[0].color
        //   diffuse   = sun * lightmap.g + bumpedSky
        //   FinalColor.rgb *= 2 * diffuse
        //
        // `skyNormal` is `vec3(0.78,0.52,0.65)` (RaShaderSTM.fx:34) and it is a
        // **tangent-space** constant, not a world direction: the pixel path
        // dots it against the normal map's sample, which with no map is
        // (0,0,1), leaving just its z. The vertex path takes that z directly.
        // Dotting it against a world normal instead — which is the mistake we
        // made first — turns every wall facing away from it black.
        //
        // The 0.2 makes `invDot` a shallow ramp from 1.0 in shadow to 0.8 in
        // full sun, so the shaded side keeps an ambient floor rather than
        // falling to nothing. The doubling at the end is the game's, and it is
        // most of why its meshes are as bright as they are.
        //
        // `Lights[0].color` is bound by the engine; we pair it with
        // `Lightmanager.staticSunColor`, which is the triple that goes with
        // `staticSkyColor` in the same block and is set per level beside it.
        //
        // Missing: the baked light map. Every level ships one per object
        // (`lightmaps/Objects/LightmapAtlas*.dds` and a `.tai` of the atlas
        // offsets), so until it is read nothing shadows anything.
        //
        // With a baked light map the shader takes the same terms and gates them
        // by its channels (`RaShaderSTM.fx:358`, the `_LIGHTMAP_` branch):
        //
        //   bumpedSky  = lightmap.b * InvDotAndLightAtt
        //   diffuse    = lerp(bumpedSky, sun + bumpedSky, lightmap.g)
        //              + lightmap.r * SinglePointColor
        //
        // and that lerp is just `bumpedSky + sun * lightmap.g`. So .g gates the
        // sun, .b the sky and .r the point colour. Without a map all three are
        // 1 and the two branches are the same expression, which is why there is
        // only one below.
        const float kSkyNormalZ = 0.65;
        if (in.lightmapOffset.x > 0.0) {
            lightmapValue = lightmap.sample(lightSampler,
                                            in.uvLightmap * in.lightmapOffset.xy +
                                                in.lightmapOffset.zw).rgb;
        }
        float3 normal = normalize(in.normal);
        float3 toSun = normalize(-in.sunDirection.xyz);

        // A material with a normal map is lit per pixel instead, which is a
        // different function in the game and not the same one with a better
        // normal (`RaShaderSTM.fx:382`, getDiffusePixelLighting):
        //
        //   diffuse   = saturate(dot(compNormals, normalizedLightVec)) * Lights[0].color
        //   bumpedSky = lightmap.b * dot(compNormals, skyNormal) * StaticSkyColor
        //   diffuse   = bumpedSky + diffuse * lightmap.g
        //   diffuse  += lightmap.r * SinglePointColor
        //
        // Two things change. The sun is taken against the map's normal, so a
        // wall gets its relief. And `skyNormal` — the constant (0.78,0.52,0.65)
        // — is dotted against that normal rather than contributing its z alone:
        // it is a **tangent-space** direction, so the dot is taken there, on
        // the sample as it comes out of the texture, before any frame is
        // applied. Where a surface has no normal map the sample is (0,0,1) and
        // the dot is 0.65 — which is exactly the vertex path below, and why the
        // two branches agree at the boundary.
        if (in.material.y < 0.5 && normalStrength > 0.5) {
            const float2 normalUv = in.fogShape.y > 1.5 ? in.uv2 : in.uv;
            const float3 sampled =
                normalize(normalMap.sample(normalSampler, normalUv).xyz * 2.0 - 1.0);
            const float3 t = normalize(in.tangent);
            const float3 b = normalize(in.binormal);
            const float3 n = normalize(in.normal);
            normal = normalize(t * sampled.x + b * sampled.y + n * sampled.z);

            const float3 skyNormal = float3(0.78, 0.52, 0.65);
            const float3 sunTerm = saturate(dot(normal, toSun)) * in.sunColor.rgb;
            const float3 skyTerm =
                lightmapValue.b * dot(sampled, skyNormal) * in.skyColor.rgb;
            light = 2.0 * (skyTerm + sunTerm * lightmapValue.g +
                           in.pointColor.rgb * lightmapValue.r);
        } else {
            float nDotL = dot(normal, toSun);
            float invDot = 1.0 - saturate(nDotL * 0.2);
            float3 sun = saturate(nDotL) * in.sunColor.rgb * lightmapValue.g;
            float3 sky = kSkyNormalZ * in.skyColor.rgb * invDot * lightmapValue.b;
            light = 2.0 * (sun + sky + in.pointColor.rgb * lightmapValue.r);
        }
        // The specular below takes the same normal, per pixel or per vertex.
        shadingNormal = normal;
    }

    // `SkyDome.fx` samples the sky texture and returns it — the dome carries
    // its own sky, painted, and nothing lights it.
    if (in.material.z > 0.5) light = float3(1.0);

    float3 color = albedo.rgb * light;

    // The specular highlight, on everything the static-mesh formula lights.
    // `RaShaderSTM.fx:394` with `compNormals = (0,0,1)` — which is the vertex
    // normal, since that constant is the tangent frame's own z and the frame is
    // orthonormal, so the dot product is the same computed in world
    // coordinates and no tangent frame is needed for this path:
    //
    //   halfVec  = normalize(normalizedLightVec + eyeVec)
    //   specular = pow(saturate(dot(compNormals, halfVec)), 32)
    //   specular *= lightmap.g * gloss
    //   FinalColor.rgb += specular * CEXP(StaticSpecularColor)
    //
    // The light map's green gates it the same way it gates the sun: a wall in
    // baked shadow has no highlight either.
    if (in.fogParams.z < 0.5 && in.treeSunColor.w < 0.5 && in.material.z < 0.5) {
        const float3 normal = shadingNormal;
        const float3 toSun = normalize(-in.sunDirection.xyz);
        const float3 toEye = normalize(in.cameraPosition.xyz - in.worldPosition);
        const float3 halfVec = normalize(toSun + toEye);
        float specular = pow(saturate(dot(normal, halfVec)), 32.0);
        specular *= lightmapValue.g * gloss;
        color += specular * in.staticSpecular.rgb;
    }

    // The game's own fog. The shape is in the shipped shader
    // (`Shaders_client.zip:RaCommon.fx:54`) and the four numbers it wants were
    // measured out of the engine's uploaded constants — a frame dump of the
    // original under mtld3d, which rule 6 takes as a source:
    //
    //   scalar calcFog(scalar w) {
    //       half2 fogVals = w*FogRange.xy + FogRange.zw;
    //       half close = max(fogVals.y, FogColor.w);
    //       half far = pow(fogVals.x, 3);
    //       return close-far;                        // visibility, not density
    //   }
    //
    // With `Renderer.fogStartEndAndBase = start/end/base/floor` and
    // r = end - start, the engine uploads
    //
    //   FogRange = ( 1/r, -base/r, -start/r, 1 + start/r )
    //   FogColor = ( r, g, b, floor )
    //
    // measured on two levels at once: Mashtuur City (30/200/1.40/0.40) gave
    // 0.005882 -0.008235 -0.176471 1.176471, which is 1/170, -1.4/170, -30/170
    // and 1+30/170 exactly; Strike at Karkand (0/135/2.30/0.40) gave
    // 0.007407 -0.017037 -0.000000 1.000000 on the same four.
    //
    // Substituting, the whole thing collapses to a shape with no packing left
    // in it. With t = (w - start) / (end - start):
    //
    //   fogVals.x  = t
    //   fogVals.y  = 1 - base * t
    //   visibility = max(1 - base * t, floor) - t^3
    //
    // So the near fall-off is a straight line of slope `base` clamped from
    // below by `floor`, and the cubic finishes it off at the end. On Karkand
    // that reaches the floor within 35 m — a dense, close fog, which is what
    // the map is. On Dalian base is 0, so the line stays at 1 and only the
    // cubic acts, and the map is clear until far away.
    //
    // fogParams.y == 0 means the level has no fog.
    if (in.fogParams.y > 0.0) {
        float t = saturate((in.viewDepth - in.fogParams.x) /
                           max(in.fogParams.y - in.fogParams.x, 0.001));
        float visibility =
            saturate(max(1.0 - in.fogShape.x * t, in.fogColor.w) - t * t * t);
        color = mix(in.fogColor.rgb, color, visibility);
    }
    // Roads blend into the terrain by the alpha of their own texture
    // (`Shaders_client.zip:Road.fx:78`, `outcolor.a = tex0.a`); everything else
    // is opaque.
    return float4(color, in.material.y > 0.5 ? albedo.a * in.vertexAlpha : 1.0);
}
)MSL";

// The interface shader: the texture as it is, with alpha, without lighting.
constexpr const char* kOverlayShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

struct OverlayUniforms {
    // A transform straight in NDC: xy is the offset, zw the scale. Needed so
    // that one ready-made quad can be placed in different spots without
    // rebuilding the geometry every frame.
    float4 offsetScale;
};

vertex VertexOut overlay_vertex(VertexIn in [[stage_in]],
                                constant OverlayUniforms& uniforms [[buffer(0)]]) {
    VertexOut out;
    float2 placed = in.position.xy * uniforms.offsetScale.zw + uniforms.offsetScale.xy;
    out.position = float4(placed, in.position.z, 1.0);
    out.uv = in.uv;
    return out;
}

struct OverlayTint {
    float4 tint;
};

fragment float4 overlay_fragment(VertexOut in [[stage_in]],
                                 texture2d<float> image [[texture(0)]],
                                 sampler imageSampler [[sampler(0)]],
                                 constant OverlayTint& shade [[buffer(0)]]) {
    return image.sample(imageSampler, in.uv) * shade.tint;
}
)MSL";

SDL_GPUShader* createOverlayShader(SDL_GPUDevice* gpu, SDL_GPUShaderStage stage,
                                   const char* entrypoint) {
  SDL_GPUShaderCreateInfo info{};
  info.code = reinterpret_cast<const Uint8*>(kOverlayShaderSource);
  info.code_size = std::strlen(kOverlayShaderSource);
  info.entrypoint = entrypoint;
  info.format = SDL_GPU_SHADERFORMAT_MSL;
  info.stage = stage;
  info.num_samplers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? 1 : 0;
  // Both stages have one constant buffer each: the vertex one takes the offset
  // and the scale, the fragment one the node's tint.
  info.num_uniform_buffers = 1;
  return SDL_CreateGPUShader(gpu, &info);
}

SDL_GPUShader* createShader(SDL_GPUDevice* gpu, SDL_GPUShaderStage stage, const char* entrypoint) {
  const bool isVertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
  SDL_GPUShaderCreateInfo info{};
  info.code = reinterpret_cast<const Uint8*>(kShaderSource);
  info.code_size = std::strlen(kShaderSource);
  info.entrypoint = entrypoint;
  info.format = SDL_GPU_SHADERFORMAT_MSL;
  info.stage = stage;
  info.num_uniform_buffers = isVertex ? 1 : 0;
  info.num_samplers = isVertex ? 0 : 8;  // colour, light map, detail, the ground's light and its texture, normal, dirt, crack
  return SDL_CreateGPUShader(gpu, &info);
}

SDL_GPUTextureFormat toGpuFormat(texture::Format format) {
  switch (format) {
    case texture::Format::Bc1: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case texture::Format::Bc2: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case texture::Format::Bc3: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case texture::Format::Bgra8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case texture::Format::Bgra4: return SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM;
    case texture::Format::Bgr565: return SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
    case texture::Format::R8: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
  }
  return SDL_GPU_TEXTUREFORMAT_INVALID;
}

// A mirror of Uniforms from the vertex shader: the matrix plus the frame's constants.
// A mirror of `Uniforms` in the shader above, field for field and in order.
struct VertexUniforms {
  float modelViewProjection[16]{};
  float model[16]{};
  float fogColor[4]{};
  float fogParams[4]{};  // start, end, lightmapMode, 0
  float sunColor[4]{};
  float skyColor[4]{};
  float material[4]{};  // x: multiply the detail map in, y: alpha from the texture
  float sunDirection[4]{};
  float pointColor[4]{};
  float lightmapOffset[4]{};
  float fogShape[4]{};   // x: the fog's base
  float roadParams[4]{}; // x: a road's blend factor
  float terrainTiling[4]{};  // xy: the side planes, z: the top, w: the y offset
  float terrainDetail[4]{};  // xy: the half-texel fix, z: the height scale, w: on/off
  float treeSunColor[4]{};      // w: 1 on a leaf material
  float treeAmbientColor[4]{};
  float cameraPosition[4]{};
  float staticSpecular[4]{};    // rgb: the colour, w: the gloss
};

}  // namespace

static_assert(sizeof(mesh::Vertex) == 80, "the vertex layout must match the shader");

SDL_GPUTexture* MeshRenderer::uploadSharedTexture(const texture::Texture& source) {
  SDL_GPUTexture* uploaded = uploadTexture(source);
  if (uploaded != nullptr) sharedTextures_.push_back(uploaded);
  return uploaded;
}

MeshRenderer::~MeshRenderer() {
  if (device_ == nullptr) return;
  SDL_GPUDevice* gpu = device_->gpu();
  for (SDL_GPUTexture* texture : sharedTextures_) SDL_ReleaseGPUTexture(gpu, texture);
  if (placeholder_ != nullptr) SDL_ReleaseGPUTexture(gpu, placeholder_);
  if (overlayPipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, overlayPipeline_);
  if (roadPipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, roadPipeline_);
  if (skyPipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, skyPipeline_);
  if (sampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, sampler_);
  if (normalSampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, normalSampler_);
  if (overlaySampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, overlaySampler_);
  if (pipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline_);
}

std::unique_ptr<MeshRenderer> MeshRenderer::create(Device& device, std::string* error) {
  auto fail = [error](const char* what) -> std::unique_ptr<MeshRenderer> {
    if (error) *error = std::string(what) + ": " + SDL_GetError();
    return nullptr;
  };

  SDL_GPUDevice* gpu = device.gpu();
  SDL_GPUShader* vertexShader = createShader(gpu, SDL_GPU_SHADERSTAGE_VERTEX, "vertex_main");
  if (vertexShader == nullptr) return fail("vertex shader");
  SDL_GPUShader* fragmentShader = createShader(gpu, SDL_GPU_SHADERSTAGE_FRAGMENT, "fragment_main");
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(gpu, vertexShader);
    return fail("fragment shader");
  }

  const SDL_GPUVertexBufferDescription bufferDescription{
      0, static_cast<Uint32>(sizeof(mesh::Vertex)), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
  const SDL_GPUVertexAttribute attributes[9] = {
      {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, position)},
      {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, normal)},
      {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uv)},
      {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uv2)},
      {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uvLightmap)},
      {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(mesh::Vertex, alpha)},
      {6, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, tangent)},
      {7, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uvDirt)},
      {8, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uvCrack)},
  };

  SDL_GPUColorTargetDescription colorTarget{};
  colorTarget.format = device.colorFormat();

  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vertexShader;
  info.fragment_shader = fragmentShader;
  info.vertex_input_state.vertex_buffer_descriptions = &bufferDescription;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attributes;
  info.vertex_input_state.num_vertex_attributes = 9;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  // Vertex winding in BF2 is counter-clockwise: across all 1635 meshes in the
  // game (2.2 M triangles) the geometric normal agrees with the vertex normals
  // in 99.66% of cases. So back-face culling is on.
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
  // In a left-handed system the winding on screen is the opposite of a
  // right-handed one, so clockwise triangles become front-facing. The mesh data
  // has not changed — what changed is the side we look at it from.
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  info.depth_stencil_state.enable_depth_test = true;
  info.depth_stencil_state.enable_depth_write = true;
  info.target_info.color_target_descriptions = &colorTarget;
  info.target_info.num_color_targets = 1;
  info.target_info.depth_stencil_format = device.depthFormat();
  info.target_info.has_depth_stencil_target = true;

  // The depth format only becomes known once the first depth texture is created.
  if (info.target_info.depth_stencil_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(device.window(), &width, &height);
    device.acquireDepthTarget(static_cast<Uint32>(width), static_cast<Uint32>(height));
    info.target_info.depth_stencil_format = device.depthFormat();
  }

  SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);

  // Roads are the same geometry drawn a second way. The original tests depth
  // but does **not** write it, and blends by the texture's alpha
  // (`Shaders_client.zip:RoadCompiled.fx:194`, technique roadcompiledFull, pass
  // NV3x: `ZEnable = TRUE, ZWriteEnable = FALSE, SrcBlend = SRCALPHA,
  // DestBlend = INVSRCALPHA`). Drawn as ordinary opaque geometry they land on
  // the terrain's own depth and fight it for every pixel.
  SDL_GPUColorTargetDescription roadTarget = colorTarget;
  roadTarget.blend_state.enable_blend = true;
  roadTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  roadTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  roadTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  roadTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  roadTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  roadTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  SDL_GPUGraphicsPipelineCreateInfo roadInfo = info;
  roadInfo.target_info.color_target_descriptions = &roadTarget;
  roadInfo.depth_stencil_state.enable_depth_write = false;
  SDL_GPUGraphicsPipeline* roadPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &roadInfo);

  // The sky dome is the background: drawn first, with no depth at all, so
  // everything after it paints over.
  //
  // This is **not** the original's state. `SkyDome.fx:226`, technique
  // SkyDomeNV3xNoClouds, keeps `ZWriteEnable = TRUE, ZFunc = LESSEQUAL` and its
  // vertex shader projects the dome with a w of 10 rather than 1
  // (`SkyDome.fx:100`, "fix for artifacts on BFO"), which pulls an 878-unit
  // dome in to an effective 88. That ordering depends on the engine's view
  // distance, which we do not have — our far plane is the fog's end. Drawn
  // first with no depth the picture is the same while nothing may stand behind
  // the sky, and the deviation is here in writing rather than in a number.
  //
  // The dome is not culled either: we look at it from inside, and 1120
  // triangles are not worth establishing which way the winding turns under our
  // left-handed conversion.
  SDL_GPUGraphicsPipelineCreateInfo skyInfo = info;
  skyInfo.depth_stencil_state.enable_depth_test = false;
  skyInfo.depth_stencil_state.enable_depth_write = false;
  skyInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  SDL_GPUGraphicsPipeline* skyPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &skyInfo);

  SDL_ReleaseGPUShader(gpu, vertexShader);
  SDL_ReleaseGPUShader(gpu, fragmentShader);
  if (pipeline == nullptr) return fail("graphics pipeline");
  if (roadPipeline == nullptr) return fail("road pipeline");
  if (skyPipeline == nullptr) return fail("sky pipeline");

  auto renderer = std::unique_ptr<MeshRenderer>(new MeshRenderer());
  renderer->device_ = &device;
  renderer->pipeline_ = pipeline;
  renderer->roadPipeline_ = roadPipeline;
  renderer->skyPipeline_ = skyPipeline;

  // The ground's light. Its own pass, its own file: what it is and what the
  // game does with it is in `obf2/gfx/terrain_light.h`. A failure here is not
  // fatal — the terrain then reads its light map directly and the roads take
  // the static-mesh formula, which is what they had before.
  renderer->terrainLight_ = TerrainLightBuffer::create(device);

  // The second pipeline is for the interface: no depth (the call order decides
  // that), but with alpha blending, without which the font's glyphs would be
  // opaque rectangles.
  SDL_GPUShader* overlayVertex = createOverlayShader(gpu, SDL_GPU_SHADERSTAGE_VERTEX, "overlay_vertex");
  SDL_GPUShader* overlayFragment =
      createOverlayShader(gpu, SDL_GPU_SHADERSTAGE_FRAGMENT, "overlay_fragment");
  if (overlayVertex == nullptr || overlayFragment == nullptr) return fail("interface shader");

  SDL_GPUColorTargetDescription overlayTarget{};
  overlayTarget.format = device.colorFormat();
  overlayTarget.blend_state.enable_blend = true;
  overlayTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  overlayTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  overlayTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  overlayTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  overlayTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  overlayTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  SDL_GPUGraphicsPipelineCreateInfo overlayInfo{};
  overlayInfo.vertex_shader = overlayVertex;
  overlayInfo.fragment_shader = overlayFragment;
  overlayInfo.vertex_input_state = info.vertex_input_state;
  overlayInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  overlayInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  overlayInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  overlayInfo.target_info.color_target_descriptions = &overlayTarget;
  overlayInfo.target_info.num_color_targets = 1;

  renderer->overlayPipeline_ = SDL_CreateGPUGraphicsPipeline(gpu, &overlayInfo);
  SDL_ReleaseGPUShader(gpu, overlayVertex);
  SDL_ReleaseGPUShader(gpu, overlayFragment);
  if (renderer->overlayPipeline_ == nullptr) return fail("interface pipeline");

  // The world's samplers come from the texture-filtering setting; the level the
  // profile carries by default is medium.
  renderer->setTextureFiltering(2);
  if (renderer->sampler_ == nullptr) return fail("sampler");

  // The interface is another matter: every node is its own picture stretched
  // exactly over its rectangle, and repeating is not needed there at all.
  // With it, bilinear sampling right at a quad's edge also takes a pixel from
  // the opposite side of the texture, and a one-pixel dark line appears along
  // the plates' outline — especially when the window is not exactly 800x600 and
  // the edge does not land on a whole pixel. Mips are unnecessary too: the
  // interface is never minified.
  SDL_GPUSamplerCreateInfo overlaySamplerInfo{};
  overlaySamplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
  overlaySamplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  overlaySamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  overlaySamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  overlaySamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  overlaySamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  overlaySamplerInfo.max_lod = 0.0f;
  renderer->overlaySampler_ = SDL_CreateGPUSampler(gpu, &overlaySamplerInfo);
  if (renderer->overlaySampler_ == nullptr) return fail("interface sampler");

  // A placeholder for materials whose texture is missing or unreadable: better a
  // white surface than a black hole or a crash.
  texture::Texture white;
  white.format = texture::Format::Bgra8;
  white.width = 1;
  white.height = 1;
  white.data.assign(4, std::byte{0xFF});
  white.mips.push_back(texture::MipLevel{1, 1, 0, 4});
  renderer->placeholder_ = renderer->uploadTexture(white);
  if (renderer->placeholder_ == nullptr) return fail("placeholder texture");

  return renderer;
}

SDL_GPUTexture* MeshRenderer::uploadTexture(const texture::Texture& source) {
  SDL_GPUDevice* gpu = device_->gpu();
  const SDL_GPUTextureFormat format = toGpuFormat(source.format);
  if (format == SDL_GPU_TEXTUREFORMAT_INVALID || source.mips.empty()) return nullptr;
  if (!SDL_GPUTextureSupportsFormat(gpu, format, SDL_GPU_TEXTURETYPE_2D,
                                    SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
    return nullptr;  // the backend does not support this format — decided above
  }

  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = format;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = source.width;
  info.height = source.height;
  info.layer_count_or_depth = 1;
  info.num_levels = static_cast<Uint32>(source.mips.size());
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;

  SDL_GPUTexture* gpuTexture = SDL_CreateGPUTexture(gpu, &info);
  if (gpuTexture == nullptr) return nullptr;

  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferInfo.size = static_cast<Uint32>(source.data.size());
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu, &transferInfo);
  if (transfer == nullptr) {
    SDL_ReleaseGPUTexture(gpu, gpuTexture);
    return nullptr;
  }

  void* mapped = SDL_MapGPUTransferBuffer(gpu, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(gpu, transfer);
    SDL_ReleaseGPUTexture(gpu, gpuTexture);
    return nullptr;
  }
  std::memcpy(mapped, source.data.data(), source.data.size());
  SDL_UnmapGPUTransferBuffer(gpu, transfer);

  SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(gpu);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
  for (Uint32 level = 0; level < static_cast<Uint32>(source.mips.size()); ++level) {
    const texture::MipLevel& mip = source.mips[level];

    SDL_GPUTextureTransferInfo location{};
    location.transfer_buffer = transfer;
    location.offset = static_cast<Uint32>(mip.offset);
    location.pixels_per_row = mip.width;
    location.rows_per_layer = mip.height;

    SDL_GPUTextureRegion region{};
    region.texture = gpuTexture;
    region.mip_level = level;
    region.w = mip.width;
    region.h = mip.height;
    region.d = 1;

    SDL_UploadToGPUTexture(copy, &location, &region, false);
  }
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(commands);
  SDL_ReleaseGPUTransferBuffer(gpu, transfer);

  return gpuTexture;
}

std::optional<GpuMesh> MeshRenderer::upload(const mesh::RenderMesh& source,
                                            const TextureResolver& resolve, std::string* error) {
  auto fail = [error](const char* what) -> std::optional<GpuMesh> {
    if (error) *error = std::string(what) + ": " + SDL_GetError();
    return std::nullopt;
  };
  if (source.vertices.empty() || source.indices.empty()) {
    if (error) *error = "empty geometry";
    return std::nullopt;
  }

  SDL_GPUDevice* gpu = device_->gpu();
  const Uint32 vertexBytes = static_cast<Uint32>(source.vertices.size() * sizeof(mesh::Vertex));
  const Uint32 indexBytes = static_cast<Uint32>(source.indices.size() * sizeof(std::uint32_t));

  SDL_GPUBufferCreateInfo vertexInfo{};
  vertexInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  vertexInfo.size = vertexBytes;
  SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(gpu, &vertexInfo);
  if (vertexBuffer == nullptr) return fail("vertex buffer");

  SDL_GPUBufferCreateInfo indexInfo{};
  indexInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
  indexInfo.size = indexBytes;
  SDL_GPUBuffer* indexBuffer = SDL_CreateGPUBuffer(gpu, &indexInfo);
  if (indexBuffer == nullptr) {
    SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
    return fail("index buffer");
  }

  // One transfer buffer for both arrays: the vertices, then the indices.
  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferInfo.size = vertexBytes + indexBytes;
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu, &transferInfo);
  if (transfer == nullptr) {
    SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
    SDL_ReleaseGPUBuffer(gpu, indexBuffer);
    return fail("transfer buffer");
  }

  auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(gpu, transfer, false));
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(gpu, transfer);
    SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
    SDL_ReleaseGPUBuffer(gpu, indexBuffer);
    return fail("mapping the transfer buffer");
  }
  std::memcpy(mapped, source.vertices.data(), vertexBytes);
  std::memcpy(mapped + vertexBytes, source.indices.data(), indexBytes);
  SDL_UnmapGPUTransferBuffer(gpu, transfer);

  SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(gpu);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);

  SDL_GPUTransferBufferLocation source0{transfer, 0};
  SDL_GPUBufferRegion vertexRegion{vertexBuffer, 0, vertexBytes};
  SDL_UploadToGPUBuffer(copy, &source0, &vertexRegion, false);

  SDL_GPUTransferBufferLocation source1{transfer, vertexBytes};
  SDL_GPUBufferRegion indexRegion{indexBuffer, 0, indexBytes};
  SDL_UploadToGPUBuffer(copy, &source1, &indexRegion, false);

  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(commands);
  SDL_ReleaseGPUTransferBuffer(gpu, transfer);

  GpuMesh gpuMesh;
  gpuMesh.vertices = vertexBuffer;
  gpuMesh.indices = indexBuffer;
  gpuMesh.hasLightmapUv = source.hasLightmapUv;

  // A sphere around the mesh's bounds: the centre in the middle, the radius to a corner.
  const Vec3f minimum{source.bounds.min.x, source.bounds.min.y, source.bounds.min.z};
  const Vec3f maximum{source.bounds.max.x, source.bounds.max.y, source.bounds.max.z};
  gpuMesh.boundsCenter = (minimum + maximum) * 0.5f;
  gpuMesh.boundsRadius = length(maximum - minimum) * 0.5f;

  // Which slot holds what is named by the material's own technique — the
  // texture list follows its tokens in order (`obf2::mesh::materialLayout`).
  // The terrain is the exception: it is ours, not the game's, and
  // `level::buildTerrainPatches` puts the baked light map in slot 1 and the
  // detail weights in slot 2 itself.
  for (const mesh::DrawRange& source_range : source.ranges) {
    GpuMesh::Range range;
    range.indexStart = source_range.indexStart;
    range.indexCount = source_range.indexCount;

    auto load = [&](int slot) -> SDL_GPUTexture* {
      if (slot < 0 || !resolve) return nullptr;
      if (static_cast<std::size_t>(slot) >= source_range.maps.size()) return nullptr;
      const auto decoded = resolve(source_range.maps[static_cast<std::size_t>(slot)]);
      if (!decoded) return nullptr;
      SDL_GPUTexture* uploaded = uploadTexture(*decoded);
      if (uploaded != nullptr) gpuMesh.ownedTextures.push_back(uploaded);
      return uploaded;
    };

    if (source_range.lightmapInSecondSlot) {
      range.texture = load(0);
      range.lightmap = load(1);
      range.detail = load(2);
    } else {
      // A road's material has no technique — the two maps are the template's
      // primary and secondary, in that order.
      if (source_range.technique.empty() && source_range.maps.size() == 2) {
        range.texture = load(0);
        range.detail = load(1);
        gpuMesh.ranges.push_back(range);
        continue;
      }
      const mesh::MaterialLayout layout = mesh::materialLayout(source_range.technique);
      // With no technique at all (31 materials in the game) slot 0 is still the
      // base colour: every one of them carries a single `_c` map.
      range.texture = load(layout.base >= 0 ? layout.base : 0);
      range.detail = load(layout.detail);
      range.detailMultiply = range.detail != nullptr;
      // The normal map, and which UV set it is read with: the shader samples
      // `NBase` with the base map's unwrap and `NDetail` with the tiling one
      // (`Shaders_client.zip:RaShaderSTM.fx:316`, getCompositeNormals). A
      // material names at most one of the two — `BaseDetailNDetail` is 4191 of
      // the game's materials and `NBase` appears in none of the top ten.
      range.dirt = load(layout.dirt);
      range.crack = load(layout.crack);
      if (layout.normalDetail >= 0) {
        range.normalMap = load(layout.normalDetail);
        range.normalOnDetailUv = true;
      } else if (layout.normalBase >= 0) {
        range.normalMap = load(layout.normalBase);
        range.normalOnDetailUv = false;
      }
      // 0 and 2 are the only values the game's static meshes carry, and 2 is
      // the alpha-tested one — the pine's needles have it while its trunk does
      // not (`mesh_info … nc_pinebig01.staticmesh`).
      range.alphaTest = source_range.alphaMode == 2;
      range.leaf = source_range.leaf;
    }
    gpuMesh.ranges.push_back(range);
  }

  return gpuMesh;
}

void MeshRenderer::release(GpuMesh& gpuMesh) {
  SDL_GPUDevice* gpu = device_->gpu();
  for (SDL_GPUTexture* texture : gpuMesh.ownedTextures) SDL_ReleaseGPUTexture(gpu, texture);
  if (gpuMesh.vertices != nullptr) SDL_ReleaseGPUBuffer(gpu, gpuMesh.vertices);
  if (gpuMesh.indices != nullptr) SDL_ReleaseGPUBuffer(gpu, gpuMesh.indices);
  gpuMesh = GpuMesh{};
}

void MeshRenderer::renderOverlay(const Frame& frame, const std::vector<DrawItem>& items,
                                 Color clearColor, bool clear) {
  SDL_GPUColorTargetInfo colorTarget{};
  colorTarget.texture = frame.swapchain;
  colorTarget.clear_color = SDL_FColor{clearColor.r, clearColor.g, clearColor.b, clearColor.a};
  colorTarget.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  colorTarget.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(frame.commands, &colorTarget, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(pass, overlayPipeline_);

  // The draw order is the layering order: the background first, then the text.
  for (const DrawItem& item : items) {
    if (item.mesh == nullptr || item.mesh->vertices == nullptr) continue;

    // The offset and the scale come from the matrix: the translation in xy, the scale on the diagonal.
    const float offsetScale[4] = {item.transform.m[12], item.transform.m[13],
                                  item.transform.m[0], item.transform.m[5]};
    SDL_PushGPUVertexUniformData(frame.commands, 0, offsetScale, sizeof(offsetScale));
    SDL_PushGPUFragmentUniformData(frame.commands, 0, item.tint, sizeof(item.tint));

    const SDL_GPUBufferBinding vertexBinding{item.mesh->vertices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    const SDL_GPUBufferBinding indexBinding{item.mesh->indices, 0};
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    for (const GpuMesh::Range& range : item.mesh->ranges) {
      if (range.indexCount == 0) continue;
      // In the interface the placeholder will not do: a white texture over the
      // whole screen would simply hide the frame. No picture — nothing is drawn.
      if (range.texture == nullptr) continue;
      SDL_GPUTextureSamplerBinding binding{range.texture, overlaySampler_};
      SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
      SDL_DrawGPUIndexedPrimitives(pass, range.indexCount, 1, range.indexStart, 0, 0);
    }
  }

  SDL_EndGPURenderPass(pass);
}

void MeshRenderer::render(const Frame& frame, const GpuMesh& gpuMesh,
                          const Mat4& modelViewProjection, Color clearColor) {
  const DrawItem item{&gpuMesh, Mat4::identity()};
  renderScene(frame, {item}, modelViewProjection, clearColor);
}

void MeshRenderer::setTextureFiltering(int quality) {
  if (device_ == nullptr) return;
  SDL_GPUDevice* gpu = device_->gpu();
  if (sampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, sampler_);
  if (normalSampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, normalSampler_);

  // What this setting does is write a preamble for the shader compiler, and
  // `EffectManager` writes it in full (`RendDX9.dll`, `FUN_10034010`,
  // 0x10034010; the assert beside it names
  // `Code\\BF2\\RendDX9\\Ra\\EffectManager.cpp`):
  //
  //   1  FILTER_STM_NORM_MIP POINT,  FILTER_BM_NORM_MIP POINT,
  //      every diffuse filter LINEAR
  //   2  FILTER_STM_NORM_MIP LINEAR, FILTER_BM_NORM_MIP POINT,
  //      every diffuse filter LINEAR
  //   3  both normal mips LINEAR, and the diffuse filters ANISOTROPIC where the
  //      device can (min and mag are asked separately), with
  //
  //          maxAnisotropy = min(device, 2)   on a static mesh
  //          maxAnisotropy = min(device, 4)   on a bundled one
  //
  //   anything else is the engine's own "Undefined texture filter quality."
  //
  // So the highest setting is anisotropic **2** on the world's meshes, not the
  // 16 a modern driver would offer.
  const bool anisotropic = quality >= 3;

  SDL_GPUSamplerCreateInfo info{};
  info.min_filter = SDL_GPU_FILTER_LINEAR;
  info.mag_filter = SDL_GPU_FILTER_LINEAR;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  // BF2's textures are made to repeat: detail and road surfaces tile dozens of
  // times over one object.
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  info.max_lod = 1000.0f;
  info.enable_anisotropy = anisotropic;
  info.max_anisotropy = 2.0f;
  sampler_ = SDL_CreateGPUSampler(gpu, &info);

  // The normal map's own mip filter is the one thing the lower two levels
  // differ in: point at 1, linear at 2 and 3.
  SDL_GPUSamplerCreateInfo normal = info;
  normal.enable_anisotropy = false;
  normal.mipmap_mode =
      quality <= 1 ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  normalSampler_ = SDL_CreateGPUSampler(gpu, &normal);
}

void MeshRenderer::setTerrainDetail(SDL_GPUTexture* lowDetail, const float sideTiling[2],
                                    float topTiling, float yOffset, int componentSize) {
  terrainDetail_ = lowDetail;
  terrainTiling_[0] = sideTiling[0];
  terrainTiling_[1] = sideTiling[1];
  terrainTiling_[2] = topTiling;
  terrainTiling_[3] = yOffset;
  // The half-texel correction the engine gives a per-patch map, from the same
  // push that sets DETAILTEX (`RendDX9.dll`, 0x100d9c30): a UV in [0,1] lands
  // on texel centres as `uv * (n-1)/n + 1/(2n)`.
  const float n = componentSize > 0 ? static_cast<float>(componentSize) : 1.0f;
  terrainDetailUv_[0] = (n - 1.0f) / n;
  terrainDetailUv_[1] = 1.0f / (2.0f * n);
}

void MeshRenderer::renderScene(const Frame& frame, const std::vector<DrawItem>& items,
                               const Mat4& viewProjection, Color clearColor) {
  // First the ground's light, into a buffer of its own — the game's own order:
  // its terrain's ZFillLightmap pass runs before anything that reads the light
  // (`Shaders_client.zip:TerrainShader_Hi.fx:607`, pass p0). Everything lit by
  // the ground samples the result by screen position afterwards.
  SDL_GPUTexture* groundLight =
      terrainLight_ != nullptr ? terrainLight_->render(frame, items, viewProjection, terrainSky_)
                               : nullptr;

  SDL_GPUTexture* depth = device_->acquireDepthTarget(frame.width, frame.height);

  SDL_GPUColorTargetInfo colorTarget{};
  colorTarget.texture = frame.swapchain;
  colorTarget.clear_color = SDL_FColor{clearColor.r, clearColor.g, clearColor.b, clearColor.a};
  colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
  colorTarget.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPUDepthStencilTargetInfo depthTarget{};
  depthTarget.texture = depth;
  depthTarget.clear_depth = 1.0f;
  depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
  depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
  depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(frame.commands, &colorTarget, 1,
                                                   depth != nullptr ? &depthTarget : nullptr);

  // The view frustum is taken from the same matrix we draw with, so the culling
  // is guaranteed to agree with what the camera sees.
  const Frustum frustum = extractFrustum(viewProjection);
  drawn_ = 0;
  culled_ = 0;

  // The fog and the lighting colours are the same for the frame; the light map
  // mode is not, so it has to be pushed before every range.
  VertexUniforms uniforms{};
  uniforms.fogColor[0] = fog_.color.r;
  uniforms.fogColor[1] = fog_.color.g;
  uniforms.fogColor[2] = fog_.color.b;
  uniforms.fogColor[3] = fog_.floorVisibility;
  uniforms.fogParams[0] = fog_.start;
  uniforms.fogParams[1] = fog_.end;
  uniforms.fogShape[0] = fog_.base;
  uniforms.sunDirection[0] = sunDirection_.x;
  uniforms.sunDirection[1] = sunDirection_.y;
  uniforms.sunDirection[2] = sunDirection_.z;
  uniforms.pointColor[0] = pointColor_.r;
  uniforms.pointColor[1] = pointColor_.g;
  uniforms.pointColor[2] = pointColor_.b;
  // The size of what we are drawing into: the ground's light is read back by
  // screen position, and Metal hands the fragment shader that position in pixels.
  uniforms.roadParams[2] = static_cast<float>(frame.width);
  uniforms.roadParams[3] = static_cast<float>(frame.height);
  std::memcpy(uniforms.terrainTiling, terrainTiling_, sizeof(uniforms.terrainTiling));
  uniforms.treeSunColor[0] = treeSun_.r;
  uniforms.treeSunColor[1] = treeSun_.g;
  uniforms.treeSunColor[2] = treeSun_.b;
  uniforms.cameraPosition[0] = cameraPosition_.x;
  uniforms.cameraPosition[1] = cameraPosition_.y;
  uniforms.cameraPosition[2] = cameraPosition_.z;
  uniforms.staticSpecular[0] = staticSpecular_.r;
  uniforms.staticSpecular[1] = staticSpecular_.g;
  uniforms.staticSpecular[2] = staticSpecular_.b;
  uniforms.staticSpecular[3] = staticGloss_;
  uniforms.treeAmbientColor[0] = treeAmbient_.r;
  uniforms.treeAmbientColor[1] = treeAmbient_.g;
  uniforms.treeAmbientColor[2] = treeAmbient_.b;
  uniforms.terrainDetail[0] = terrainDetailUv_[0];
  uniforms.terrainDetail[1] = terrainDetailUv_[1];
  // `vTexScale.y`: the scale the side planes take the world height by. A
  // constant in the engine, not a level's number — `RendDX9.dll`, 0x100d9c30,
  // pushes TEXSCALE as (255/n, -0.00615148, 255/n, 0).
  uniforms.terrainDetail[2] = -0.00615148f;

  // Three passes over the same list, each through its own pipeline: the sky is
  // the background, then everything solid, then the roads as a skin on the
  // terrain.
  enum class Layer { Sky, Solid, Road };
  auto layerOf = [](const DrawItem& item) {
    if (item.sky) return Layer::Sky;
    return item.road ? Layer::Road : Layer::Solid;
  };
  auto drawLayer = [&](Layer wanted) {
    for (const DrawItem& item : items) {
      if (layerOf(item) != wanted) continue;
      if (item.mesh == nullptr || item.mesh->vertices == nullptr) continue;

      Mat4 transform = item.transform;
      if (item.road) transform.m[13] += kRoadLift;

      // The sky is always around the camera, so culling it can only ever get
      // it wrong.
      if (!item.sky && item.mesh->boundsRadius > 0.0f) {
        const Vec3f center = transformPoint(transform, item.mesh->boundsCenter);
        if (!frustum.intersectsSphere(center, item.mesh->boundsRadius)) {
          ++culled_;
          continue;
        }
      }
      ++drawn_;

      const SDL_GPUBufferBinding vertexBinding{item.mesh->vertices, 0};
      SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
      const SDL_GPUBufferBinding indexBinding{item.mesh->indices, 0};
      SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

      const Mat4 modelViewProjection = viewProjection * transform;
      std::memcpy(uniforms.modelViewProjection, modelViewProjection.m, sizeof(Mat4));
      std::memcpy(uniforms.model, transform.m, sizeof(Mat4));
      uniforms.material[1] = item.road ? 1.0f : 0.0f;
      uniforms.roadParams[0] = item.roadBlendFactor;
      const bool bakedItem = item.lightmap != nullptr && item.mesh->hasLightmapUv;
      if (bakedItem) {
        std::memcpy(uniforms.lightmapOffset, item.lightmapOffset,
                    sizeof(uniforms.lightmapOffset));
      } else {
        std::memset(uniforms.lightmapOffset, 0, sizeof(uniforms.lightmapOffset));
      }
      uniforms.material[2] = item.sky ? 1.0f : 0.0f;
      // No fog on the sky: the dome's texture already holds the horizon the fog
      // fades into. `SkyDome.fx` computes none either.
      uniforms.fogParams[1] = item.sky ? 0.0f : fog_.end;

      for (const GpuMesh::Range& range : item.mesh->ranges) {
        if (range.indexCount == 0) continue;

        // Slot 1 is the light map. For the terrain it belongs to the range —
        // one patch, one map. For a placed object it belongs to the **item**:
        // the same building stands on a level thirty times and each copy has
        // its own window into the level's atlas, so the geometry is shared and
        // the light map is not.
        // Only when the geometry has the light map's UV set. A mesh without
        // TEXCOORD2 has `uv3` all zeroes, so it would sample a single texel at
        // the corner of its window — and where that texel is black, the whole
        // object goes black. The atlas may well hold an entry for the placement
        // anyway.
        const bool baked = item.lightmap != nullptr && item.mesh->hasLightmapUv;
        SDL_GPUTexture* lightmapTexture = baked ? item.lightmap : range.lightmap;
        const SDL_GPUTextureSamplerBinding bindings[8] = {
            {range.texture != nullptr ? range.texture : placeholder_, sampler_},
            {lightmapTexture != nullptr ? lightmapTexture : placeholder_, sampler_},
            {range.detail != nullptr ? range.detail : placeholder_, sampler_},
            {groundLight != nullptr ? groundLight : placeholder_,
             groundLight != nullptr ? terrainLight_->readSampler() : sampler_},
            {terrainDetail_ != nullptr ? terrainDetail_ : placeholder_, sampler_},
            {range.normalMap != nullptr ? range.normalMap : placeholder_,
             normalSampler_ != nullptr ? normalSampler_ : sampler_},
            {range.dirt != nullptr ? range.dirt : placeholder_, sampler_},
            {range.crack != nullptr ? range.crack : placeholder_, sampler_},
        };
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 8);

        // The lighting mode changes from range to range, so the uniform is pushed
        // before every draw call.
        // Which branch of the shader draws this range — and with it, which pair
      // of light colours it means by sunColor/skyColor.
      // 0 — a static mesh, lit by the sun and the sky directly; 1 — terrain,
      // reading its own light map (only when there is no buffer to read
      // instead); 2 — lit by the ground's light buffer, which is both the
      // terrain and the roads lying on it.
      const bool terrain = range.lightmap != nullptr;
      const bool fromGround = groundLight != nullptr && (terrain || item.road);
      uniforms.fogParams[2] = fromGround ? 2.0f : (terrain ? 1.0f : 0.0f);
      const Color& sun = (terrain || fromGround) ? terrainSun_ : staticSun_;
      const Color& sky = (terrain || fromGround) ? terrainSky_ : staticSky_;
      uniforms.sunColor[0] = sun.r;
      uniforms.sunColor[1] = sun.g;
      uniforms.sunColor[2] = sun.b;
      uniforms.skyColor[0] = sky.r;
      uniforms.skyColor[1] = sky.g;
      uniforms.skyColor[2] = sky.b;
        // How many times the detail repeats over a patch. With no texture the
        // tiling is zero and the sampling lands in the white placeholder.
        uniforms.fogParams[3] = range.detail != nullptr ? detailTiling_ : 0.0f;
        // The ground's structure needs both halves: the level's texture and
        // this patch's map of where it shows.
        uniforms.terrainDetail[3] =
            (terrain && terrainDetail_ != nullptr && range.detail != nullptr) ? 1.0f : 0.0f;
        uniforms.material[0] = range.detailMultiply ? 1.0f : 0.0f;
        uniforms.material[3] = range.alphaTest ? 1.0f : 0.0f;
        uniforms.treeSunColor[3] = range.leaf ? 1.0f : 0.0f;
        uniforms.fogShape[1] =
            range.normalMap == nullptr ? 0.0f : (range.normalOnDetailUv ? 2.0f : 1.0f);
        uniforms.fogShape[2] = range.dirt != nullptr ? 1.0f : 0.0f;
        uniforms.fogShape[3] = range.crack != nullptr ? 1.0f : 0.0f;
        SDL_PushGPUVertexUniformData(frame.commands, 0, &uniforms, sizeof(uniforms));

        SDL_DrawGPUIndexedPrimitives(pass, range.indexCount, 1, range.indexStart, 0, 0);
      }
    }
  };

  SDL_BindGPUGraphicsPipeline(pass, skyPipeline_);
  drawLayer(Layer::Sky);
  SDL_BindGPUGraphicsPipeline(pass, pipeline_);
  drawLayer(Layer::Solid);
  SDL_BindGPUGraphicsPipeline(pass, roadPipeline_);
  drawLayer(Layer::Road);

  SDL_EndGPURenderPass(pass);
}

}  // namespace obf2::gfx
