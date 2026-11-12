#include "obf2/app/loading_screen.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "obf2/core/math.h"
#include "obf2/mesh/primitives.h"

namespace obf2::app {

bool LoadingScreen::open(FileSystem& files, engine::Engine& engine, const std::string& level,
                         const std::string& mode, int size) {
  host_ = makeMenuHost(files, engine);
  std::string path = level;
  for (char& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  host_.loading.mapPath = path;
  host_.loading.gameMode = mode;
  host_.loading.mapSize = size;
  // From the menu the first load of the session (isFirstTimeLoading); the
  // movie then goes straight to its briefing page, not to the last round's
  // scores.
  host_.loading.firstLoad = true;
  host_.loading.active = [this] { return loading_; };
  host_.loading.progress = [this] { return stat_.progress(); };
  host_.loading.currentFile = [this] { return stat_.currentFile(); };
  host_.loading.readyToPlay = [this] { ready_ = true; };

  // The movie the client brings up for a load (BF2.exe names it beside
  // mainMenu.swf; the maintainers' WINEDEBUG run saw it opened on a level load).
  if (!movie_.open(host_, "Menu/External/FlashMenu/endOfRound.swf")) {
    std::printf("loading screen: endOfRound.swf did not open\n");
    return false;
  }
  frame_.format = texture::Format::Bgra8;
  frame_.width = movie_.width();
  frame_.height = movie_.height();
  frame_.mips.assign(1, texture::MipLevel{frame_.width, frame_.height, 0,
                                          static_cast<std::uint32_t>(frame_.width * frame_.height * 4)});
  frame_.data.assign(static_cast<std::size_t>(frame_.width) * frame_.height * 4, std::byte{0});
  last_ = std::chrono::steady_clock::now();
  std::printf("loading screen: endOfRound.swf, %s %s %d\n", path.c_str(), mode.c_str(), size);
  return true;
}

void LoadingScreen::frame(gfx::Device& device, gfx::MeshRenderer& renderer) {
  if (!movie_.isOpen()) return;
  if (!device.pumpEvents()) closed_ = true;

  // The mouse to the movie, as the menu gives it (main_menu.cpp): a press on
  // this frame, its release on the next. Stage pixels from the window's own size.
  const gfx::Device::InputState input = device.readInput();
  int windowWidth = 0, windowHeight = 0;
  SDL_GetWindowSize(device.window(), &windowWidth, &windowHeight);
  if (windowWidth > 0 && windowHeight > 0) {
    double stageX = 0.0, stageY = 0.0;
    movie_.toStage(windowWidth, windowHeight, input.mouseX, input.mouseY, &stageX, &stageY);
    movie_.mouseMove(stageX, stageY);
    if (pressed_) {
      movie_.mouseButton(pressX_, pressY_, false);
      pressed_ = false;
    } else if (input.clicked) {
      movie_.mouseButton(stageX, stageY, true);
      pressX_ = stageX;
      pressY_ = stageY;
      pressed_ = true;
    }
  }
  // "…SELECT THE JOIN GAME BUTTON OR PRESS ESCAPE TO ENTER THE GAME" — the
  // movie's own caption once the map is loaded. Escape then ends the screen;
  // the engine's handler of it is not read.
  if (device.consumeEscape() && !loading_) ready_ = true;

  const auto now = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(now - last_).count();
  last_ = now;
  movie_.advance(seconds > 0.25 ? 0.25 : seconds);

  // RGBA → BGRA for the texture, as the menu does.
  const auto& rgba = movie_.render();
  if (rgba.size() == frame_.data.size()) {
    const std::uint8_t* src = rgba.data();
    auto* dst = reinterpret_cast<std::uint8_t*>(frame_.data.data());
    for (std::size_t i = 0, n = rgba.size() / 4; i < n; ++i, src += 4, dst += 4) {
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
      dst[3] = src[3];
    }
  }
  if (!uploaded_) {
    const auto resolve = [this](const std::string& name) -> std::optional<texture::Texture> {
      if (name == "#loading") return frame_;
      return std::nullopt;
    };
    if (auto mesh = renderer.upload(mesh::buildScreenQuad("#loading"), resolve)) {
      mesh_ = std::move(*mesh);
      uploaded_ = true;
    }
  } else if (!mesh_.ranges.empty()) {
    renderer.updateTexture(mesh_.ranges[0].texture, frame_.data.data(), frame_.width, frame_.height);
  }

  auto acquired = device.beginFrame();
  if (!acquired) return;
  std::vector<gfx::MeshRenderer::DrawItem> items;
  if (uploaded_) items.push_back(gfx::MeshRenderer::DrawItem{&mesh_, Mat4::identity(), {1, 1, 1, 1}});
  renderer.renderOverlay(*acquired, items, gfx::Color{0.0f, 0.0f, 0.0f, 1.0f});
  // `OBF2_LOADING_SCREENSHOT=<file.bmp>`: every frame of the screen saved over
  // the last — the measure for what it shows (the main loop's --screenshot
  // starts only after the load).
  static const char* shot = std::getenv("OBF2_LOADING_SCREENSHOT");
  if (shot != nullptr) {
    device.submitAndSave(*acquired, shot);
  } else {
    device.submit(*acquired);
  }
}

void LoadingScreen::finish(gfx::Device& device, gfx::MeshRenderer& renderer, double maxSeconds) {
  if (!movie_.isOpen()) return;
  stat_.complete();
  loading_ = false;
  const auto started = std::chrono::steady_clock::now();
  while (!ready_ && !closed_ &&
         (maxSeconds <= 0.0 ||
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < maxSeconds)) {
    frame(device, renderer);
  }
  std::printf("loading screen: %s\n", ready_ ? "the movie said readyToPlay" : "closed after its time");
}

void LoadingScreen::release(gfx::MeshRenderer& renderer) {
  if (uploaded_) renderer.release(mesh_);
  uploaded_ = false;
}

}  // namespace obf2::app
