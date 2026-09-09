#include "obf2/gfx/mesh_renderer.h"

#include <cstring>

namespace obf2::gfx {
namespace {

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
};

struct VertexOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
    float viewDepth;
    // The frame's parameters travel to the fragment shader through varyings
    // rather than in a uniform buffer of their own: in the fragment stage they
    // never reach the shader (other data is read instead), while the vertex
    // stage works reliably. The values are constant, so interpolation is harmless.
    float4 fogColor;
    float4 fogParams;
    float4 sunColor;
    float4 skyColor;
};

struct Uniforms {
    float4x4 modelViewProjection;
    float4 fogColor;   // rgb — the fog's colour
    float4 fogParams;  // x: start, y: end (0 = no fog), z: light map mode, w: detail tiling
    float4 sunColor;   // TerrainSunColor
    float4 skyColor;   // TerrainSkyColor
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(0)]]) {
    VertexOut out;
    out.position = uniforms.modelViewProjection * float4(in.position, 1.0);
    out.normal = in.normal;
    out.uv = in.uv;
    // For a perspective projection w in clip space equals the distance along the
    // view — exactly what the fog needs.
    out.viewDepth = out.position.w;
    out.fogColor = uniforms.fogColor;
    out.fogParams = uniforms.fogParams;
    out.sunColor = uniforms.sunColor;
    out.skyColor = uniforms.skyColor;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              texture2d<float> baseColor [[texture(0)]],
                              texture2d<float> lightmap [[texture(1)]],
                              texture2d<float> detail [[texture(2)]],
                              sampler baseSampler [[sampler(0)]],
                              sampler lightSampler [[sampler(1)]],
                              sampler detailSampler [[sampler(2)]]) {
    float4 albedo = baseColor.sample(baseSampler, in.uv);

    float3 light;
    if (in.fogParams.z > 0.5) {
        // Terrain: in BF2's light map the red channel is exposure to the sun and
        // the blue one to the sky. Each is multiplied by its own colour from
        // Sky.con, which is why TerrainSunColor can exceed one: it brightens.
        float3 baked = lightmap.sample(lightSampler, in.uv).rgb;
        light = in.sunColor.rgb * baked.r + in.skyColor.rgb * baked.b;

        // The detail map is NOT multiplied in here. It turned out to be not a
        // colour but a weight map: the R/G/B channels give the shares of the
        // different terrain materials, and each of them has its own texture from
        // MaterialManager. Multiplying it in as a colour gives acid stains. The
        // texture is loaded and sits in slot 2 until there is a terrain material system.
        (void)detail;
        (void)detailSampler;
    } else {
        float3 normal = normalize(in.normal);
        float3 lightDirection = normalize(float3(0.4, 0.9, 0.35));
        // Half-Lambert lighting: the shadow side does not go black, and the
        // geometry's silhouette stays fully visible.
        float lambert = dot(normal, lightDirection) * 0.5 + 0.5;
        light = float3(0.35 + 0.65 * lambert);
    }

    float3 color = albedo.rgb * light;

    // fogParams.y == 0 means the level has no fog.
    if (in.fogParams.y > 0.0) {
        float t = saturate((in.viewDepth - in.fogParams.x) /
                           max(in.fogParams.y - in.fogParams.x, 0.001));
        color = mix(color, in.fogColor.rgb, t);
    }
    return float4(color, 1.0);
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
  info.num_samplers = isVertex ? 0 : 3;  // colour, light map, detail
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
struct VertexUniforms {
  float modelViewProjection[16]{};
  float fogColor[4]{};
  float fogParams[4]{};  // start, end, lightmapMode, 0
  float sunColor[4]{};
  float skyColor[4]{};
};

}  // namespace

static_assert(sizeof(mesh::Vertex) == 32, "the vertex layout must match the shader");

MeshRenderer::~MeshRenderer() {
  if (device_ == nullptr) return;
  SDL_GPUDevice* gpu = device_->gpu();
  if (placeholder_ != nullptr) SDL_ReleaseGPUTexture(gpu, placeholder_);
  if (overlayPipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, overlayPipeline_);
  if (sampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, sampler_);
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
  const SDL_GPUVertexAttribute attributes[3] = {
      {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, position)},
      {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(mesh::Vertex, normal)},
      {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(mesh::Vertex, uv)},
  };

  SDL_GPUColorTargetDescription colorTarget{};
  colorTarget.format = device.colorFormat();

  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vertexShader;
  info.fragment_shader = fragmentShader;
  info.vertex_input_state.vertex_buffer_descriptions = &bufferDescription;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attributes;
  info.vertex_input_state.num_vertex_attributes = 3;
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
  SDL_ReleaseGPUShader(gpu, vertexShader);
  SDL_ReleaseGPUShader(gpu, fragmentShader);
  if (pipeline == nullptr) return fail("graphics pipeline");

  auto renderer = std::unique_ptr<MeshRenderer>(new MeshRenderer());
  renderer->device_ = &device;
  renderer->pipeline_ = pipeline;

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

  SDL_GPUSamplerCreateInfo samplerInfo{};
  samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
  samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  // BF2's textures are made to repeat: detail and road surfaces tile dozens of
  // times over one object.
  samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  samplerInfo.max_lod = 1000.0f;
  renderer->sampler_ = SDL_CreateGPUSampler(gpu, &samplerInfo);
  if (renderer->sampler_ == nullptr) return fail("sampler");

  // The interface is another matter: every node is its own picture stretched
  // exactly over its rectangle, and repeating is not needed there at all.
  // With it, bilinear sampling right at a quad's edge also takes a pixel from
  // the opposite side of the texture, and a one-pixel dark line appears along
  // the plates' outline — especially when the window is not exactly 800x600 and
  // the edge does not land on a whole pixel. Mips are unnecessary too: the
  // interface is never minified.
  samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  samplerInfo.max_lod = 0.0f;
  renderer->overlaySampler_ = SDL_CreateGPUSampler(gpu, &samplerInfo);
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

  // A sphere around the mesh's bounds: the centre in the middle, the radius to a corner.
  const Vec3f minimum{source.bounds.min.x, source.bounds.min.y, source.bounds.min.z};
  const Vec3f maximum{source.bounds.max.x, source.bounds.max.y, source.bounds.max.z};
  gpuMesh.boundsCenter = (minimum + maximum) * 0.5f;
  gpuMesh.boundsRadius = length(maximum - minimum) * 0.5f;

  // A material's slot 0 is the base colour (`_c`): that is visible both from the
  // technique names ("BaseDetailNDetail") and from the suffix distribution over the game's 4524 materials.
  for (const mesh::DrawRange& source_range : source.ranges) {
    GpuMesh::Range range;
    range.indexStart = source_range.indexStart;
    range.indexCount = source_range.indexCount;

    if (!source_range.maps.empty() && resolve) {
      if (const auto decoded = resolve(source_range.maps.front())) {
        range.texture = uploadTexture(*decoded);
        if (range.texture != nullptr) gpuMesh.ownedTextures.push_back(range.texture);
      }
    }
    // The terrain's third slot is the detail map.
    if (source_range.maps.size() > 2 && resolve && source_range.lightmapInSecondSlot) {
      if (const auto decoded = resolve(source_range.maps[2])) {
        range.detail = uploadTexture(*decoded);
        if (range.detail != nullptr) gpuMesh.ownedTextures.push_back(range.detail);
      }
    }
    // The terrain's second slot is the baked lighting. For ordinary meshes slot 1
    // is the detail map, which we do not use yet, so the light map is taken only
    // where it was explicitly put (see level::buildTerrainPatches).
    if (source_range.maps.size() > 1 && resolve && source_range.lightmapInSecondSlot) {
      if (const auto decoded = resolve(source_range.maps[1])) {
        range.lightmap = uploadTexture(*decoded);
        if (range.lightmap != nullptr) gpuMesh.ownedTextures.push_back(range.lightmap);
      }
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

void MeshRenderer::renderScene(const Frame& frame, const std::vector<DrawItem>& items,
                               const Mat4& viewProjection, Color clearColor) {
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

  SDL_BindGPUGraphicsPipeline(pass, pipeline_);

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
  uniforms.fogColor[3] = 1.0f;
  uniforms.fogParams[0] = fog_.start;
  uniforms.fogParams[1] = fog_.end;
  uniforms.sunColor[0] = terrainSun_.r;
  uniforms.sunColor[1] = terrainSun_.g;
  uniforms.sunColor[2] = terrainSun_.b;
  uniforms.skyColor[0] = terrainSky_.r;
  uniforms.skyColor[1] = terrainSky_.g;
  uniforms.skyColor[2] = terrainSky_.b;

  for (const DrawItem& item : items) {
    if (item.mesh == nullptr || item.mesh->vertices == nullptr) continue;

    if (item.mesh->boundsRadius > 0.0f) {
      const Vec3f center = transformPoint(item.transform, item.mesh->boundsCenter);
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

    const Mat4 modelViewProjection = viewProjection * item.transform;
    std::memcpy(uniforms.modelViewProjection, modelViewProjection.m, sizeof(Mat4));

    for (const GpuMesh::Range& range : item.mesh->ranges) {
      if (range.indexCount == 0) continue;

      const SDL_GPUTextureSamplerBinding bindings[3] = {
          {range.texture != nullptr ? range.texture : placeholder_, sampler_},
          {range.lightmap != nullptr ? range.lightmap : placeholder_, sampler_},
          {range.detail != nullptr ? range.detail : placeholder_, sampler_},
      };
      SDL_BindGPUFragmentSamplers(pass, 0, bindings, 3);

      // The lighting mode changes from range to range, so the uniform is pushed
      // before every draw call.
      uniforms.fogParams[2] = range.lightmap != nullptr ? 1.0f : 0.0f;
      // How many times the detail repeats over a patch. With no texture the
      // tiling is zero and the sampling lands in the white placeholder.
      uniforms.fogParams[3] = range.detail != nullptr ? detailTiling_ : 0.0f;
      SDL_PushGPUVertexUniformData(frame.commands, 0, &uniforms, sizeof(uniforms));

      SDL_DrawGPUIndexedPrimitives(pass, range.indexCount, 1, range.indexStart, 0, 0);
    }
  }

  SDL_EndGPURenderPass(pass);
}

}  // namespace obf2::gfx
