#include "obf2/gfx/terrain_light.h"

#include <cstring>

namespace obf2::gfx {
namespace {

// The game's ZFillLightmap pass, in MSL. The geometry is the terrain's own, so
// the vertex layout is the one every other pass uses; only `uv` is read.
constexpr const char* kShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float2 uv2      [[attribute(3)]];
    float2 uv3      [[attribute(4)]];
    float  alpha    [[attribute(5)]];
    float3 tangent  [[attribute(6)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 giColor;
};

struct Uniforms {
    float4x4 modelViewProjection;
    // `terrain.GIColor` from the level's Sky.con — the sky's share of the
    // ground's light. Travels as a varying for the same reason as in the main
    // shader: the fragment stage does not see the constant buffer here.
    float4 giColor;
};

vertex VertexOut terrain_light_vertex(VertexIn in [[stage_in]],
                                      constant Uniforms& uniforms [[buffer(0)]]) {
    VertexOut out;
    out.position = uniforms.modelViewProjection * float4(in.position, 1.0);
    out.uv = in.uv;
    out.giColor = uniforms.giColor;
    return out;
}

fragment float4 terrain_light_fragment(VertexOut in [[stage_in]],
                                       texture2d<float> lightmap [[texture(0)]],
                                       sampler lightSampler [[sampler(0)]]) {
    // `Shaders_client.zip:TerrainShader_Hi.fx:588`:
    //     light     = saturate(lightmap.z * vGIColor * 2) * 0.5;
    //     light.w   = lightmap.y;
    float3 baked = lightmap.sample(lightSampler, in.uv).rgb;
    float3 gi = saturate(baked.b * in.giColor.rgb * 2.0) * 0.5;
    return float4(gi, saturate(baked.g));
}
)MSL";

SDL_GPUShader* createShader(SDL_GPUDevice* gpu, SDL_GPUShaderStage stage, const char* entrypoint) {
  const bool isVertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
  SDL_GPUShaderCreateInfo info{};
  info.code = reinterpret_cast<const Uint8*>(kShaderSource);
  info.code_size = std::strlen(kShaderSource);
  info.entrypoint = entrypoint;
  info.format = SDL_GPU_SHADERFORMAT_MSL;
  info.stage = stage;
  info.num_uniform_buffers = isVertex ? 1 : 0;
  info.num_samplers = isVertex ? 0 : 1;
  return SDL_CreateGPUShader(gpu, &info);
}

// A mirror of `Uniforms` above, field for field.
struct LightUniforms {
  float modelViewProjection[16]{};
  float giColor[4]{};
};

}  // namespace

TerrainLightBuffer::~TerrainLightBuffer() {
  if (device_ == nullptr) return;
  SDL_GPUDevice* gpu = device_->gpu();
  if (color_ != nullptr) SDL_ReleaseGPUTexture(gpu, color_);
  if (depth_ != nullptr) SDL_ReleaseGPUTexture(gpu, depth_);
  if (sampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, sampler_);
  if (readSampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, readSampler_);
  if (pipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline_);
}

std::unique_ptr<TerrainLightBuffer> TerrainLightBuffer::create(Device& device,
                                                               std::string* error) {
  auto fail = [error](const char* what) -> std::unique_ptr<TerrainLightBuffer> {
    if (error) *error = std::string(what) + ": " + SDL_GetError();
    return nullptr;
  };

  SDL_GPUDevice* gpu = device.gpu();
  SDL_GPUShader* vertexShader =
      createShader(gpu, SDL_GPU_SHADERSTAGE_VERTEX, "terrain_light_vertex");
  if (vertexShader == nullptr) return fail("terrain light vertex shader");
  SDL_GPUShader* fragmentShader =
      createShader(gpu, SDL_GPU_SHADERSTAGE_FRAGMENT, "terrain_light_fragment");
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(gpu, vertexShader);
    return fail("terrain light fragment shader");
  }

  const SDL_GPUVertexBufferDescription bufferDescription{
      0, static_cast<Uint32>(sizeof(mesh::Vertex)), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
  const SDL_GPUVertexAttribute attributes[7] = {
      {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, position)},
      {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, normal)},
      {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uv)},
      {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uv2)},
      {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uv3)},
      {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(mesh::Vertex, alpha)},
      {6, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, tangent)},
  };

  SDL_GPUColorTargetDescription colorTarget{};
  colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vertexShader;
  info.fragment_shader = fragmentShader;
  info.vertex_input_state.vertex_buffer_descriptions = &bufferDescription;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attributes;
  info.vertex_input_state.num_vertex_attributes = 7;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  // The pass the game calls ZFill: it is the one that lays the terrain's depth
  // down (`TerrainShader_Hi.fx:607`, ZWriteEnable = TRUE, ZFunc = LESS). Ours
  // writes into a depth buffer of its own, since the main pass clears its own.
  info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  info.depth_stencil_state.enable_depth_test = true;
  info.depth_stencil_state.enable_depth_write = true;
  info.target_info.color_target_descriptions = &colorTarget;
  info.target_info.num_color_targets = 1;
  info.target_info.depth_stencil_format = device.depthFormat();
  info.target_info.has_depth_stencil_target = true;

  if (info.target_info.depth_stencil_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(device.window(), &width, &height);
    device.acquireDepthTarget(static_cast<Uint32>(width), static_cast<Uint32>(height));
    info.target_info.depth_stencil_format = device.depthFormat();
  }

  SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
  SDL_ReleaseGPUShader(gpu, vertexShader);
  SDL_ReleaseGPUShader(gpu, fragmentShader);
  if (pipeline == nullptr) return fail("terrain light pipeline");

  SDL_GPUSamplerCreateInfo linear{};
  linear.min_filter = SDL_GPU_FILTER_LINEAR;
  linear.mag_filter = SDL_GPU_FILTER_LINEAR;
  linear.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  linear.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  linear.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  linear.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

  SDL_GPUSamplerCreateInfo point{};
  point.min_filter = SDL_GPU_FILTER_NEAREST;
  point.mag_filter = SDL_GPU_FILTER_NEAREST;
  point.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  point.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  point.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  point.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

  auto buffer = std::unique_ptr<TerrainLightBuffer>(new TerrainLightBuffer());
  buffer->device_ = &device;
  buffer->pipeline_ = pipeline;
  buffer->sampler_ = SDL_CreateGPUSampler(gpu, &linear);
  buffer->readSampler_ = SDL_CreateGPUSampler(gpu, &point);
  if (buffer->sampler_ == nullptr || buffer->readSampler_ == nullptr) return fail("sampler");
  return buffer;
}

bool TerrainLightBuffer::resize(Uint32 width, Uint32 height) {
  if (color_ != nullptr && width == width_ && height == height_) return true;

  SDL_GPUDevice* gpu = device_->gpu();
  if (color_ != nullptr) SDL_ReleaseGPUTexture(gpu, color_);
  if (depth_ != nullptr) SDL_ReleaseGPUTexture(gpu, depth_);
  color_ = nullptr;
  depth_ = nullptr;

  SDL_GPUTextureCreateInfo colorInfo{};
  colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
  colorInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  colorInfo.width = width;
  colorInfo.height = height;
  colorInfo.layer_count_or_depth = 1;
  colorInfo.num_levels = 1;
  color_ = SDL_CreateGPUTexture(gpu, &colorInfo);

  SDL_GPUTextureCreateInfo depthInfo{};
  depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
  depthInfo.format = device_->depthFormat();
  depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  depthInfo.width = width;
  depthInfo.height = height;
  depthInfo.layer_count_or_depth = 1;
  depthInfo.num_levels = 1;
  depth_ = SDL_CreateGPUTexture(gpu, &depthInfo);

  if (color_ == nullptr || depth_ == nullptr) return false;
  width_ = width;
  height_ = height;
  return true;
}

SDL_GPUTexture* TerrainLightBuffer::render(const Frame& frame,
                                           const std::vector<MeshRenderer::DrawItem>& items,
                                           const Mat4& viewProjection, Color giColor) {
  if (device_ == nullptr || pipeline_ == nullptr) return nullptr;
  if (!resize(frame.width, frame.height)) return nullptr;

  SDL_GPUColorTargetInfo colorTarget{};
  colorTarget.texture = color_;
  // Nothing under the sky: where no terrain is drawn the buffer stays at zero,
  // and a road over nothing gets no light — which is what the game does too,
  // since its buffer is cleared the same way.
  colorTarget.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f};
  colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
  colorTarget.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPUDepthStencilTargetInfo depthTarget{};
  depthTarget.texture = depth_;
  depthTarget.clear_depth = 1.0f;
  depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
  depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
  depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

  SDL_GPURenderPass* pass =
      SDL_BeginGPURenderPass(frame.commands, &colorTarget, 1, &depthTarget);
  SDL_BindGPUGraphicsPipeline(pass, pipeline_);

  LightUniforms uniforms{};
  uniforms.giColor[0] = giColor.r;
  uniforms.giColor[1] = giColor.g;
  uniforms.giColor[2] = giColor.b;

  for (const MeshRenderer::DrawItem& item : items) {
    if (item.mesh == nullptr || item.mesh->vertices == nullptr) continue;
    if (item.sky || item.road) continue;

    const Mat4 modelViewProjection = viewProjection * item.transform;
    std::memcpy(uniforms.modelViewProjection, modelViewProjection.m, sizeof(Mat4));

    bool bound = false;
    for (const GpuMesh::Range& range : item.mesh->ranges) {
      // Only the terrain: a range whose own second slot is a light map. That is
      // what a terrain patch is here and what nothing else is.
      if (range.indexCount == 0 || range.lightmap == nullptr) continue;
      if (!bound) {
        const SDL_GPUBufferBinding vertexBinding{item.mesh->vertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
        const SDL_GPUBufferBinding indexBinding{item.mesh->indices, 0};
        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(frame.commands, 0, &uniforms, sizeof(uniforms));
        bound = true;
      }
      const SDL_GPUTextureSamplerBinding binding{range.lightmap, sampler_};
      SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
      SDL_DrawGPUIndexedPrimitives(pass, range.indexCount, 1, range.indexStart, 0, 0);
    }
  }

  SDL_EndGPURenderPass(pass);
  return color_;
}

}  // namespace obf2::gfx
