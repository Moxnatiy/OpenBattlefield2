#pragma once
#include <memory>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/gfx/device.h"
#include "obf2/gfx/mesh_renderer.h"

namespace obf2::gfx {

// The light the ground gives back — an off-screen buffer, not a texture lookup.
//
// BF2 does not light its terrain in the pass that draws it. It first fills a
// screen-sized buffer from the tile's baked light map and then reads that buffer
// back projectively in every pass that needs the ground's light: the terrain
// itself, and the roads lying on it.
//
// The fill is one pass, and it is written twice in the shipped shader. As HLSL
// (`Shaders_client.zip:TerrainShader_Hi.fx:588`, Hi_PS_DirectionalLightShadows):
//
//     vec4 lightmap = tex2D(sampler0Clamp, indata.Tex0);
//     vec4 light = saturate(lightmap.z * vGIColor * 2) * 0.5;
//     light.w = lightmap.y;               // or the shadow map, where it is nearer
//
// and as the hand-written ps_1_4 of the same pass (TerrainShader_Hi.fx:630),
// whose constants are uploaded halved for the 1.x range:
//
//     texld r1, t0
//     mul r0.xyz, r1.z, c0     // rgb = lightmap.b * vGIColor
//     +mov_sat r0.w, r1.y      // a   = saturate(lightmap.g)
//
// So the sun rides the light map's **green** channel and the sky — the shader
// calls it GI — its blue. The red one no terrain pass reads.
//
// Who consumes it, and how, is in `MeshRenderer`'s fragment shader: both the
// terrain (`TerrainShader_Hi.fx:83`) and the road (`RoadCompiled.fx:128`) reduce
// to the same expression, `4 * accum.a * SunColor + 2 * accum.rgb`.
//
// The dynamic shadow map is not drawn into it: we have none. Where the game
// would take `min(shadow, lightmap.y)` we take the baked value alone.
class TerrainLightBuffer {
 public:
  ~TerrainLightBuffer();
  TerrainLightBuffer(const TerrainLightBuffer&) = delete;
  TerrainLightBuffer& operator=(const TerrainLightBuffer&) = delete;

  static std::unique_ptr<TerrainLightBuffer> create(Device& device, std::string* error = nullptr);

  // Draws every terrain range of `items` into the buffer and returns it, or
  // nullptr when the buffer could not be made this frame. The pass is its own —
  // it has to end before the main one may sample what it wrote.
  SDL_GPUTexture* render(const Frame& frame, const std::vector<MeshRenderer::DrawItem>& items,
                         const Mat4& viewProjection, Color giColor);

  // The sampler the game reads the buffer back with: point filtering, clamped,
  // no mips — the road's own declaration of it spells the whole state out
  // (`Shaders_client.zip:RoadCompiled.fx:44`, sampler2 over `lighting`).
  SDL_GPUSampler* readSampler() const { return readSampler_; }

 private:
  TerrainLightBuffer() = default;
  bool resize(Uint32 width, Uint32 height);

  Device* device_ = nullptr;
  SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
  SDL_GPUSampler* sampler_ = nullptr;      // reading the light map inside the pass
  SDL_GPUSampler* readSampler_ = nullptr;  // reading the buffer itself, point/clamp
  SDL_GPUTexture* color_ = nullptr;
  SDL_GPUTexture* depth_ = nullptr;
  Uint32 width_ = 0, height_ = 0;
};

}  // namespace obf2::gfx
