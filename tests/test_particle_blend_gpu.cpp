#include "check.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/mesh/primitives.h"
#include "obf2/texture/dds.h"

// Rendered RGB checks for MeshParticleMesh.fx:88-95/120-138. No game/network
// connection or playback. Expected values come from the shipped ONE/ONE
// equation, not from the pipeline description under test.
int main() {
  const char* output = std::getenv("OBF2_GPU_OUTPUT");
  if (!output || !std::getenv("OBF2_GPU_TESTS")) return 77;
  std::filesystem::create_directories(output);
  std::string error;
  auto device = obf2::gfx::Device::create({"M4 additive particle regression",320,240,true,true},&error);
  CHECK(bool(device)); if (!device) return 1;
  auto renderer = obf2::gfx::MeshRenderer::create(*device,&error);
  CHECK(bool(renderer)); if (!renderer) return 1;
  auto quad = obf2::mesh::buildScreenQuad({});
  auto gpu = renderer->upload(quad,{});
  CHECK(bool(gpu)); if (!gpu) return 1;
  gpu->ranges.front().texture = renderer->uploadSharedTexture(obf2::texture::solidColor(1,0,0,.5f));
  gpu->ranges.front().alphaBlend = true;
  gpu->ranges.front().bundledAlpha = true;
  for (int stage = 0; stage < 2; ++stage) {
    device->pumpEvents(); const auto frame = device->beginFrame();
    CHECK(bool(frame)); if (!frame) return 1;
    gpu->ranges.front().additiveBlend = stage == 1;
    obf2::gfx::MeshRenderer::DrawItem item{&*gpu,obf2::Mat4::identity()};
    item.particleOpacity = 1;
    renderer->renderScene(*frame,{item},obf2::Mat4::identity(),{.2f,.2f,.2f,1});
    const auto path = (std::filesystem::path(output)/(stage ? "additive.bmp" : "alpha-before.bmp")).string();
    CHECK(device->submitAndSave(*frame,path.c_str(),&error));
    auto* saved = SDL_LoadBMP(path.c_str());
    CHECK(saved && SDL_LockSurface(saved)); if (!saved) return 1;
    Uint32 value = 0;
    const auto* pixel = static_cast<const unsigned char*>(saved->pixels) +
      (saved->h/2)*saved->pitch + (saved->w/2)*SDL_BYTESPERPIXEL(saved->format);
    std::memcpy(&value,pixel,SDL_BYTESPERPIXEL(saved->format));
    Uint8 r,g,b,a;
    SDL_GetRGBA(value,SDL_GetPixelFormatDetails(saved->format),nullptr,&r,&g,&b,&a);
    // Alpha texture quantizes to127/255. ONE/ONE source RGB*alpha adds to
    // background; standard alpha dims background instead. Allow2 code values
    // for target-format rounding, independent of texture/normal lighting.
    const int expectedRed = stage ? 178 : 153;
    const int expectedOther = stage ? 51 : 26;
    CHECK(std::abs(int(r)-expectedRed) <= 2);
    CHECK(std::abs(int(g)-expectedOther) <= 2);
    CHECK(std::abs(int(b)-expectedOther) <= 2);
    std::printf("%s RGB=%u/%u/%u\n",stage ? "additive" : "alpha",r,g,b);
    SDL_UnlockSurface(saved); SDL_DestroySurface(saved);
  }
  // Sprite colour (Particles.fx psParticle: rgb *= 2·input.color, which is
  // color·intensity + term), Sorted blend: a grey texel at alpha .5 tinted
  // 0.6/0.5/0.4 over the .2 grey clear.
  {
    gpu->ranges.front().texture = renderer->uploadSharedTexture(obf2::texture::solidColor(.8f,.8f,.8f,.5f));
    gpu->ranges.front().additiveBlend = false;
    device->pumpEvents(); const auto frame = device->beginFrame();
    CHECK(bool(frame)); if (!frame) return 1;
    obf2::gfx::MeshRenderer::DrawItem item{&*gpu,obf2::Mat4::identity()};
    item.particleOpacity = 1;
    item.particleColor[0] = .6f; item.particleColor[1] = .5f; item.particleColor[2] = .4f;
    renderer->renderScene(*frame,{item},obf2::Mat4::identity(),{.2f,.2f,.2f,1});
    const auto path = (std::filesystem::path(output)/"sprite-colour.bmp").string();
    CHECK(device->submitAndSave(*frame,path.c_str(),&error));
    auto* saved = SDL_LoadBMP(path.c_str());
    CHECK(saved && SDL_LockSurface(saved)); if (!saved) return 1;
    Uint32 value = 0;
    const auto* pixel = static_cast<const unsigned char*>(saved->pixels) +
      (saved->h/2)*saved->pitch + (saved->w/2)*SDL_BYTESPERPIXEL(saved->format);
    std::memcpy(&value,pixel,SDL_BYTESPERPIXEL(saved->format));
    Uint8 r,g,b,a;
    SDL_GetRGBA(value,SDL_GetPixelFormatDetails(saved->format),nullptr,&r,&g,&b,&a);
    std::printf("sprite colour RGB=%u/%u/%u\n",r,g,b);
    // .8·c·.5 + .2·.5 in 0..255, with 127/255 alpha: 87/77/66.
    CHECK(std::abs(int(r)-87) <= 3);
    CHECK(std::abs(int(g)-77) <= 3);
    CHECK(std::abs(int(b)-66) <= 3);
    SDL_UnlockSurface(saved); SDL_DestroySurface(saved);
  }
  // Self-shadow (RendDX9.dll 0x10170070): the bottom corners carry the shadow
  // colour, the top ones the sprite colour, interpolated down the quad. uv2
  // as smoke_flare.cpp lays it out: the corner's offset from the cell centre,
  // v down, so the bottom corners have uv2.y > 0.
  {
    auto shaded = obf2::mesh::buildScreenQuad({});
    for (auto& v : shaded.vertices) {
      v.uv2[0] = v.position.x * 0.5f;
      v.uv2[1] = -v.position.y * 0.5f;
    }
    auto mesh = renderer->upload(shaded,{});
    CHECK(bool(mesh)); if (!mesh) return 1;
    mesh->ranges.front().texture = renderer->uploadSharedTexture(obf2::texture::solidColor(.8f,.8f,.8f,.5f));
    mesh->ranges.front().alphaBlend = true;
    mesh->ranges.front().bundledAlpha = true;
    device->pumpEvents(); const auto frame = device->beginFrame();
    CHECK(bool(frame)); if (!frame) return 1;
    obf2::gfx::MeshRenderer::DrawItem item{&*mesh,obf2::Mat4::identity()};
    item.particleOpacity = 1;
    item.particleColor[0] = .6f; item.particleColor[1] = .5f; item.particleColor[2] = .4f;
    item.particleShadow[0] = .2f; item.particleShadow[1] = .5f; item.particleShadow[2] = .4f;
    item.particleShadow[3] = 1;
    renderer->renderScene(*frame,{item},obf2::Mat4::identity(),{.2f,.2f,.2f,1});
    const auto path = (std::filesystem::path(output)/"sprite-shadow.bmp").string();
    CHECK(device->submitAndSave(*frame,path.c_str(),&error));
    auto* saved = SDL_LoadBMP(path.c_str());
    CHECK(saved && SDL_LockSurface(saved)); if (!saved) return 1;
    const auto red = [&](float down) {
      Uint32 value = 0;
      const auto* pixel = static_cast<const unsigned char*>(saved->pixels) +
        int(saved->h*down)*saved->pitch + (saved->w/2)*SDL_BYTESPERPIXEL(saved->format);
      std::memcpy(&value,pixel,SDL_BYTESPERPIXEL(saved->format));
      Uint8 r,g,b,a;
      SDL_GetRGBA(value,SDL_GetPixelFormatDetails(saved->format),nullptr,&r,&g,&b,&a);
      return int(r);
    };
    // .8·c·.5 + .1 with c = mix(.6, .2, down): 0.1 → .58 → 85; 0.9 → .24 → 50.
    const int top = red(.1f), bottom = red(.9f);
    std::printf("sprite shadow red top=%d bottom=%d\n",top,bottom);
    CHECK(std::abs(top-85) <= 3);
    CHECK(std::abs(bottom-50) <= 3);
    SDL_UnlockSurface(saved); SDL_DestroySurface(saved);
    renderer->release(*mesh);
  }
  renderer->release(*gpu);
  return obf2test::g_failures ? 1 : 0;
}
