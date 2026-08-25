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
};

struct Uniforms {
    float4x4 modelViewProjection;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(0)]]) {
    VertexOut out;
    out.position = uniforms.modelViewProjection * float4(in.position, 1.0);
    out.normal = in.normal;
    out.uv = in.uv;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    float3 normal = normalize(in.normal);
    float3 lightDirection = normalize(float3(0.4, 0.9, 0.35));
    // Півламбертове освітлення: тіньовий бік не стає чорним, і силует
    // геометрії видно повністю.
    float lambert = dot(normal, lightDirection) * 0.5 + 0.5;
    float3 base = float3(0.62, 0.60, 0.55);
    return float4(base * (0.25 + 0.75 * lambert), 1.0);
}
)MSL";

SDL_GPUShader* createShader(SDL_GPUDevice* gpu, SDL_GPUShaderStage stage, const char* entrypoint) {
  SDL_GPUShaderCreateInfo info{};
  info.code = reinterpret_cast<const Uint8*>(kShaderSource);
  info.code_size = std::strlen(kShaderSource);
  info.entrypoint = entrypoint;
  info.format = SDL_GPU_SHADERFORMAT_MSL;
  info.stage = stage;
  info.num_uniform_buffers = stage == SDL_GPU_SHADERSTAGE_VERTEX ? 1 : 0;
  return SDL_CreateGPUShader(gpu, &info);
}

}  // namespace

static_assert(sizeof(mesh::Vertex) == 32, "розкладка вершини має збігатися з шейдером");

MeshRenderer::~MeshRenderer() {
  if (device_ != nullptr && pipeline_ != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device_->gpu(), pipeline_);
  }
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
  // Порядок обходу трикутників у BF2 ще не перевірений, тому вимикаємо
  // відсікання — інакше половина граней могла б зникнути без пояснень.
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
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
  return renderer;
}

std::optional<GpuMesh> MeshRenderer::upload(const mesh::RenderMesh& source, std::string* error) {
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
  gpuMesh.indexCount = static_cast<std::uint32_t>(source.indices.size());
  return gpuMesh;
}

void MeshRenderer::release(GpuMesh& gpuMesh) {
  SDL_GPUDevice* gpu = device_->gpu();
  if (gpuMesh.vertices != nullptr) SDL_ReleaseGPUBuffer(gpu, gpuMesh.vertices);
  if (gpuMesh.indices != nullptr) SDL_ReleaseGPUBuffer(gpu, gpuMesh.indices);
  gpuMesh = GpuMesh{};
}

void MeshRenderer::render(const Frame& frame, const GpuMesh& gpuMesh,
                          const Mat4& modelViewProjection, Color clearColor) {
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

  SDL_GPURenderPass* pass =
      SDL_BeginGPURenderPass(frame.commands, &colorTarget, 1, depth != nullptr ? &depthTarget : nullptr);

  SDL_BindGPUGraphicsPipeline(pass, pipeline_);
  const SDL_GPUBufferBinding vertexBinding{gpuMesh.vertices, 0};
  SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
  const SDL_GPUBufferBinding indexBinding{gpuMesh.indices, 0};
  SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
  SDL_PushGPUVertexUniformData(frame.commands, 0, &modelViewProjection, sizeof(Mat4));
  SDL_DrawGPUIndexedPrimitives(pass, gpuMesh.indexCount, 1, 0, 0, 0);

  SDL_EndGPURenderPass(pass);
}

}  // namespace obf2::gfx
