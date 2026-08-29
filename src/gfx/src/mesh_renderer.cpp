#include "obf2/gfx/mesh_renderer.h"

#include <cstring>

namespace obf2::gfx {
namespace {

// Шейдери подаються як текст MSL: SDL_GPU віддає їх компілятору Metal у
// рантаймі, тому для macOS окремий крок збірки шейдерів поки не потрібен.
// Коли дійде до Windows, ці ж шейдери доведеться мати ще й у SPIR-V/DXIL.
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
    // Параметри кадру їдуть у фрагментний шейдер через varyings, а не
    // власним uniform-буфером: у фрагментного вони до шейдера не доходять
    // (читаються чужі дані), а вершинний працює надійно. Значення сталі,
    // тому інтерполяція їм не шкодить.
    float4 fogColor;
    float4 fogParams;
    float4 sunColor;
    float4 skyColor;
};

struct Uniforms {
    float4x4 modelViewProjection;
    float4 fogColor;   // rgb — колір туману
    float4 fogParams;  // x: початок, y: кінець (0 = туману немає), z: режим лайтмапи, w: тайлінг детейлу
    float4 sunColor;   // TerrainSunColor
    float4 skyColor;   // TerrainSkyColor
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(0)]]) {
    VertexOut out;
    out.position = uniforms.modelViewProjection * float4(in.position, 1.0);
    out.normal = in.normal;
    out.uv = in.uv;
    // Для перспективної проєкції w у кліп-просторі дорівнює відстані
    // вздовж погляду — саме те, що потрібно туману.
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
        // Терен: у лайтмапі BF2 червоний канал — це доступ до сонця, синій —
        // до неба. Кожен множиться на свій колір зі Sky.con, і саме тому
        // TerrainSunColor буває більшим за одиницю: він підсвічує.
        float3 baked = lightmap.sample(lightSampler, in.uv).rgb;
        light = in.sunColor.rgb * baked.r + in.skyColor.rgb * baked.b;

        // Детейл-мапа сюди НЕ домножується. З'ясувалося, що це не колір, а
        // карта ваг: канали R/G/B задають частки різних матеріалів терену,
        // і кожен із них має власну текстуру з MaterialManager. Домноження
        // її як кольору дає кислотні плями. Текстура вантажиться й лежить
        // у слоті 2, доки не буде матеріальної системи терену.
        (void)detail;
        (void)detailSampler;
    } else {
        float3 normal = normalize(in.normal);
        float3 lightDirection = normalize(float3(0.4, 0.9, 0.35));
        // Півламбертове освітлення: тіньовий бік не стає чорним, і силует
        // геометрії видно повністю.
        float lambert = dot(normal, lightDirection) * 0.5 + 0.5;
        light = float3(0.35 + 0.65 * lambert);
    }

    float3 color = albedo.rgb * light;

    // fogParams.y == 0 означає, що туману на рівні немає.
    if (in.fogParams.y > 0.0) {
        float t = saturate((in.viewDepth - in.fogParams.x) /
                           max(in.fogParams.y - in.fogParams.x, 0.001));
        color = mix(color, in.fogColor.rgb, t);
    }
    return float4(color, 1.0);
}
)MSL";

// Шейдер інтерфейсу: текстура як є, з альфою, без освітлення.
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
    // Перетворення прямо в NDC: xy — зсув, zw — масштаб. Потрібне, щоб
    // один готовий прямокутник можна було ставити в різні місця, не
    // перезбираючи геометрію щокадру.
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
  // Обидва щаблі мають по одному буферу сталих: вершинний бере зсув і
  // масштаб, фрагментний — відтінок вузла.
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
  info.num_samplers = isVertex ? 0 : 3;  // колір, лайтмапа, детейл
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

// Дзеркало Uniforms із вершинного шейдера: матриця плюс сталі кадру.
struct VertexUniforms {
  float modelViewProjection[16]{};
  float fogColor[4]{};
  float fogParams[4]{};  // start, end, lightmapMode, 0
  float sunColor[4]{};
  float skyColor[4]{};
};

}  // namespace

static_assert(sizeof(mesh::Vertex) == 32, "розкладка вершини має збігатися з шейдером");

MeshRenderer::~MeshRenderer() {
  if (device_ == nullptr) return;
  SDL_GPUDevice* gpu = device_->gpu();
  if (placeholder_ != nullptr) SDL_ReleaseGPUTexture(gpu, placeholder_);
  if (overlayPipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, overlayPipeline_);
  if (sampler_ != nullptr) SDL_ReleaseGPUSampler(gpu, sampler_);
  if (pipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline_);
}

std::unique_ptr<MeshRenderer> MeshRenderer::create(Device& device, std::string* error) {
  auto fail = [error](const char* what) -> std::unique_ptr<MeshRenderer> {
    if (error) *error = std::string(what) + ": " + SDL_GetError();
    return nullptr;
  };

  SDL_GPUDevice* gpu = device.gpu();
  SDL_GPUShader* vertexShader = createShader(gpu, SDL_GPU_SHADERSTAGE_VERTEX, "vertex_main");
  if (vertexShader == nullptr) return fail("вершинний шейдер");
  SDL_GPUShader* fragmentShader = createShader(gpu, SDL_GPU_SHADERSTAGE_FRAGMENT, "fragment_main");
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(gpu, vertexShader);
    return fail("фрагментний шейдер");
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
  // Обхід вершин у BF2 — проти годинникової стрілки: на всіх 1635 мешах гри
  // (2.2 млн трикутників) геометрична нормаль збігається з нормалями вершин
  // у 99.66% випадків. Тому відсікання задніх граней увімкнене.
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
  // У лівій системі напрям обходу на екрані протилежний до правої, тож
  // лицьовими стають трикутники за годинниковою стрілкою. Дані мешів при
  // цьому не змінилися — змінився бік, з якого ми на них дивимось.
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  info.depth_stencil_state.enable_depth_test = true;
  info.depth_stencil_state.enable_depth_write = true;
  info.target_info.color_target_descriptions = &colorTarget;
  info.target_info.num_color_targets = 1;
  info.target_info.depth_stencil_format = device.depthFormat();
  info.target_info.has_depth_stencil_target = true;

  // Формат глибини стає відомим лише після створення першої текстури глибини.
  if (info.target_info.depth_stencil_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(device.window(), &width, &height);
    device.acquireDepthTarget(static_cast<Uint32>(width), static_cast<Uint32>(height));
    info.target_info.depth_stencil_format = device.depthFormat();
  }

  SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
  SDL_ReleaseGPUShader(gpu, vertexShader);
  SDL_ReleaseGPUShader(gpu, fragmentShader);
  if (pipeline == nullptr) return fail("графічний пайплайн");

  auto renderer = std::unique_ptr<MeshRenderer>(new MeshRenderer());
  renderer->device_ = &device;
  renderer->pipeline_ = pipeline;

  // Другий пайплайн — для інтерфейсу: глибини немає (порядок задає сам
  // виклик), зате є альфа-змішування, без якого гліфи шрифту були б
  // непрозорими прямокутниками.
  SDL_GPUShader* overlayVertex = createOverlayShader(gpu, SDL_GPU_SHADERSTAGE_VERTEX, "overlay_vertex");
  SDL_GPUShader* overlayFragment =
      createOverlayShader(gpu, SDL_GPU_SHADERSTAGE_FRAGMENT, "overlay_fragment");
  if (overlayVertex == nullptr || overlayFragment == nullptr) return fail("шейдер інтерфейсу");

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
  if (renderer->overlayPipeline_ == nullptr) return fail("пайплайн інтерфейсу");

  SDL_GPUSamplerCreateInfo samplerInfo{};
  samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
  samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  // Текстури BF2 розраховані на повторення: детейл і дорожні покриття
  // тайляться десятки разів на одному об'єкті.
  samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  samplerInfo.max_lod = 1000.0f;
  renderer->sampler_ = SDL_CreateGPUSampler(gpu, &samplerInfo);
  if (renderer->sampler_ == nullptr) return fail("семплер");

  // Заглушка для матеріалів, у яких текстури немає або вона не читається:
  // краще біла поверхня, ніж чорна діра чи падіння.
  texture::Texture white;
  white.format = texture::Format::Bgra8;
  white.width = 1;
  white.height = 1;
  white.data.assign(4, std::byte{0xFF});
  white.mips.push_back(texture::MipLevel{1, 1, 0, 4});
  renderer->placeholder_ = renderer->uploadTexture(white);
  if (renderer->placeholder_ == nullptr) return fail("текстура-заглушка");

  return renderer;
}

SDL_GPUTexture* MeshRenderer::uploadTexture(const texture::Texture& source) {
  SDL_GPUDevice* gpu = device_->gpu();
  const SDL_GPUTextureFormat format = toGpuFormat(source.format);
  if (format == SDL_GPU_TEXTUREFORMAT_INVALID || source.mips.empty()) return nullptr;
  if (!SDL_GPUTextureSupportsFormat(gpu, format, SDL_GPU_TEXTURETYPE_2D,
                                    SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
    return nullptr;  // формат не тягне цей бекенд — вирішується вище
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
    if (error) *error = "порожня геометрія";
    return std::nullopt;
  }

  SDL_GPUDevice* gpu = device_->gpu();
  const Uint32 vertexBytes = static_cast<Uint32>(source.vertices.size() * sizeof(mesh::Vertex));
  const Uint32 indexBytes = static_cast<Uint32>(source.indices.size() * sizeof(std::uint32_t));

  SDL_GPUBufferCreateInfo vertexInfo{};
  vertexInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  vertexInfo.size = vertexBytes;
  SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(gpu, &vertexInfo);
  if (vertexBuffer == nullptr) return fail("вершинний буфер");

  SDL_GPUBufferCreateInfo indexInfo{};
  indexInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
  indexInfo.size = indexBytes;
  SDL_GPUBuffer* indexBuffer = SDL_CreateGPUBuffer(gpu, &indexInfo);
  if (indexBuffer == nullptr) {
    SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
    return fail("індексний буфер");
  }

  // Один проміжний буфер на обидва масиви: вершини, за ними індекси.
  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferInfo.size = vertexBytes + indexBytes;
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu, &transferInfo);
  if (transfer == nullptr) {
    SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
    SDL_ReleaseGPUBuffer(gpu, indexBuffer);
    return fail("проміжний буфер");
  }

  auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(gpu, transfer, false));
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(gpu, transfer);
    SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
    SDL_ReleaseGPUBuffer(gpu, indexBuffer);
    return fail("мапування проміжного буфера");
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

  // Сфера навколо габаритів меша: центр посередині, радіус до кута.
  const Vec3f minimum{source.bounds.min.x, source.bounds.min.y, source.bounds.min.z};
  const Vec3f maximum{source.bounds.max.x, source.bounds.max.y, source.bounds.max.z};
  gpuMesh.boundsCenter = (minimum + maximum) * 0.5f;
  gpuMesh.boundsRadius = length(maximum - minimum) * 0.5f;

  // Слот 0 матеріалу — базовий колір (`_c`): це видно і з назв technique
  // ("BaseDetailNDetail"), і з розподілу суфіксів по 4524 матеріалах гри.
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
    // Третій слот терену — детейл.
    if (source_range.maps.size() > 2 && resolve && source_range.lightmapInSecondSlot) {
      if (const auto decoded = resolve(source_range.maps[2])) {
        range.detail = uploadTexture(*decoded);
        if (range.detail != nullptr) gpuMesh.ownedTextures.push_back(range.detail);
      }
    }
    // Другий слот терену — запечене освітлення. Для звичайних мешів слот 1
    // це детейл, який ми поки не використовуємо, тому беремо лайтмапу лише
    // там, де її явно поклали (див. level::buildTerrainPatches).
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

  // Порядок малювання і є порядком накладання: спершу тло, далі текст.
  for (const DrawItem& item : items) {
    if (item.mesh == nullptr || item.mesh->vertices == nullptr) continue;

    // Зсув і масштаб беремо з матриці: перенос у xy, масштаб на діагоналі.
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
      // В інтерфейсі заглушка не годиться: біла текстура на весь екран
      // просто сховала б кадр. Немає картинки — нічого не малюємо.
      if (range.texture == nullptr) continue;
      SDL_GPUTextureSamplerBinding binding{range.texture, sampler_};
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

  // Піраміду видимості беремо з тієї самої матриці, якою малюємо, тож
  // відсікання гарантовано узгоджене з тим, що бачить камера.
  const Frustum frustum = extractFrustum(viewProjection);
  drawn_ = 0;
  culled_ = 0;

  // Туман і кольори освітлення однакові для кадру; режим лайтмапи —
  // ні, тому його доводиться штовхати перед кожним діапазоном.
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

      // Режим освітлення змінюється від діапазону до діапазону, тому
      // uniform штовхаємо перед кожним викликом малювання.
      uniforms.fogParams[2] = range.lightmap != nullptr ? 1.0f : 0.0f;
      // Скільки разів детейл повторюється на патч. Без текстури тайлінг
      // нульовий, і вибірка потрапляє в білу заглушку.
      uniforms.fogParams[3] = range.detail != nullptr ? detailTiling_ : 0.0f;
      SDL_PushGPUVertexUniformData(frame.commands, 0, &uniforms, sizeof(uniforms));

      SDL_DrawGPUIndexedPrimitives(pass, range.indexCount, 1, range.indexStart, 0, 0);
    }
  }

  SDL_EndGPURenderPass(pass);
}

}  // namespace obf2::gfx
