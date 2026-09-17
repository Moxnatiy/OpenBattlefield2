#include "obf2/app/main_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "obf2/font/font_file.h"
#include "obf2/font/text.h"
#include "obf2/mesh/primitives.h"

namespace obf2::app {
namespace {

// The main menu's background. In the game the menu is Flash, which the engine
// runs with its own player; its assets are ordinary PNGs, and those are what we
// take.
std::string findBackground(FileSystem& files) {
  for (const char* candidate : {
           "menu/external/flashmenu/images/background/background_2.png",
           "menu/external/flashmenu/images/background/background_3.png",
           "menu/external/flashmenu/images/background/background_1.png",
       }) {
    if (files.exists(candidate)) return candidate;
  }
  return {};
}

}  // namespace

void MainMenu::boot(FileSystem& files, engine::Engine& engine,
                    const std::filesystem::path& modDir, Scene& scene, const Options& options) {
  engine.boot(files, modDir);
  const auto& settings = engine.settings();

  std::printf("engine start\n  read: ");
  for (const auto& file : engine.bootFiles()) std::printf("%s ", file.c_str());
  std::printf("\n  player: \"%s\", fullscreen: %d, field of view: %.2f\n",
              settings.general.playerName.c_str(), settings.video.fullScreen ? 1 : 0,
              settings.video.fieldOfView);
  // The ten the options screen has. Printed because they were bound to a name the
  // game never writes and nobody noticed for months (docs/TODO-graphics.md).
  const engine::VideoSettings& v = settings.video;
  std::printf("  quality: terrain %d, effects %d, geometry %d, texture %d, lighting %d\n"
              "           dynamic shadows %d, dynamic lights %d, antialiasing %d,"
              " filtering %d, view distance %.2f\n",
              v.terrainQuality, v.effectsQuality, v.geometryQuality, v.textureQuality,
              v.lightingQuality, v.dynamicShadowsQuality, v.dynamicLightingQuality, v.antialiasing,
              v.textureFilteringQuality, static_cast<double>(v.viewDistanceScale));
  std::printf("  show the intro: %d, movies found: %zu\n", settings.general.viewIntroMovie ? 1 : 0,
              engine.movies().size());
  for (const auto& movie : engine.movies()) {
    std::printf("    %s (%.1f MB)\n", movie.path.c_str(),
                static_cast<double>(movie.sizeBytes) / (1024.0 * 1024.0));
  }
  // We do not decode Bink, so each movie is a black screen for a second and a
  // half. Said out loud, because otherwise the first thing the program does is show
  // six seconds of nothing and look hung.
  if (settings.general.viewIntroMovie && !engine.movies().empty()) {
    std::printf("    the movies are not decoded — %.1f s of black, Space skips one\n",
                1.5 * static_cast<double>(engine.movies().size()));
  }

  const auto& console = engine.console();
  std::printf("  console: handlers %zu, aliases %zu, commands run %lld, unknown %lld\n",
              console.handlerCount(), console.aliasCount(), console.executedCount(),
              console.unknownCount());
  int shown = 0;
  for (const auto& [name, count] : console.unknownCommands()) {
    if (shown++ >= 24) break;
    std::printf("    without a handler: %s (x%d)\n", name.c_str(), count);
  }

  // The commands the menu's buttons run. The interface drives the game through the
  // console — the same as in the original.
  engine.console().bind("openbf2.startLevel", [this, &engine](const con::Command& command) {
    const std::string_view level = command.argStr(0);
    requestedLevel_ = level.empty() && !engine.levels().empty()
                          ? engine.levels().front().directory
                          : std::string(level);
    std::printf("menu: launching level %s\n", requestedLevel_.c_str());
  });
  engine.console().bind("openbf2.quit", [this](const con::Command&) {
    std::printf("menu: quit\n");
    quit_ = true;
  });
  background_ = findBackground(files);
  std::printf("  state: %s, the menu's background: %s\n",
              std::string(engine::stateName(engine.state())).c_str(),
              background_.empty() ? "(none)" : background_.c_str());

  scene.meshes.push_back(mesh::buildScreenQuad("#000000"));
  introQuad_ = static_cast<int>(scene.meshes.size()) - 1;

  std::printf("  localisation: %zu strings, levels: %zu\n", engine.lexicon().size(),
              engine.levels().size());

  // The loading screen. Not part of the menu: the original draws it with the
  // engine too, out of the level's own `Info/<level>.desc` and `loadmap.png`.
  const font::LoadedFont screenFont = font::loadFont(files, "Fonts/800/dynamicText_13");
  const auto* first = engine.levels().empty() ? nullptr : &engine.levels().front();
  if (screenFont.valid && first != nullptr) {
    font::TextLayout layout;
    layout.screenWidth = 1280;
    layout.screenHeight = 720;
    const auto addText = [&](std::string_view text, float x, float y, float scale) {
      layout.x = x;
      layout.y = y;
      layout.scale = scale;
      auto geometry = font::buildText(screenFont.font, text, layout, screenFont.atlasPath);
      if (geometry.indices.empty()) return -1;
      scene.meshes.push_back(std::move(geometry));
      return static_cast<int>(scene.meshes.size()) - 1;
    };

    const std::string image = first->loadImage.empty() ? std::string("#101418") : first->loadImage;
    scene.meshes.push_back(mesh::buildScreenQuad(image));
    loadingQuad_ = static_cast<int>(scene.meshes.size()) - 1;

    loadingTextQuads_.push_back(addText(first->displayName, 64.0f, 520.0f, 2.0f));

    // The map's description is the same lexicon string the game shows (the locid
    // from <briefing> in the .desc).
    if (!first->briefingKey.empty()) {
      const std::string_view briefing = engine.lexicon().text(first->briefingKey);
      float y = 570.0f;
      for (const auto& line : font::wrapText(screenFont.font, briefing, 900.0f, 1.1f)) {
        if (y > 690.0f) break;
        loadingTextQuads_.push_back(addText(line, 64.0f, y, 1.1f));
        y += 18.0f;
      }
    }
    loadingTextQuads_.erase(std::remove(loadingTextQuads_.begin(), loadingTextQuads_.end(), -1),
                            loadingTextQuads_.end());
  }

  if (options.screen == "menu") engine.skipAllMovies();
  if (options.screen == "loading" && !engine.levels().empty()) {
    engine.skipAllMovies();
    engine.startLoading(engine.levels().front().directory);
  }
}

void MainMenu::openMovie(FileSystem& files, const Options& options) {
#if OBF2_HAVE_FLASH
  // The menu is the game's own movie. It lives in `Menu_client.zip`, mounted as
  // `Menu`, and nothing is unpacked (rule 5) — so the player is given a reader over
  // the same file system the rest of the engine uses, and the movie is opened from
  // bytes. Its pictures and the movies it loads next to itself come through the
  // same reader.
  const std::string menuMoviePath = "Menu/External/FlashMenu/mainMenu.swf";
  std::string path = options.flashSwf;
  if (background_.empty()) background_ = findBackground(files);
  flash::Movie::setFileReader([&files](const std::string& name) { return files.read(name); });

  bool opened = false;
  if (!path.empty()) {
    opened = movie_.open(path);
  } else if (files.exists(menuMoviePath)) {
    path = menuMoviePath;
    const auto bytes = files.read(menuMoviePath);
    opened = bytes && movie_.openFromMemory(*bytes, menuMoviePath);
  } else {
    std::printf("Flash: the menu's movie is not in the archives — %s\n", menuMoviePath.c_str());
  }

  if (opened) {
    std::printf("Flash: %s, stage %u x %u\n", path.c_str(), movie_.width(), movie_.height());
    texture::Texture frame;
    frame.format = texture::Format::Bgra8;
    frame.width = movie_.width();
    frame.height = movie_.height();
    frame.mips.push_back(texture::MipLevel{
        frame.width, frame.height, 0, static_cast<std::uint32_t>(frame.width * frame.height * 4)});
    frame.data.assign(static_cast<std::size_t>(frame.width) * frame.height * 4, std::byte{0});
    flash_ = std::move(frame);
  } else if (!path.empty()) {
    std::printf("Flash: did not open — %s\n", path.c_str());
  }
#else
  (void)files;
  (void)options;
#endif
}

std::vector<gfx::MeshRenderer::DrawItem> MainMenu::frame(
    engine::Engine& engine, gfx::Device& device, gfx::MeshRenderer& renderer,
    const TextureResolver& resolve, const gfx::Device::InputState& input, int frameNumber,
    const Options& options, const std::vector<gfx::GpuMesh>& sceneMeshes,
    const std::vector<bool>& sceneOk) {
  engine.update(1.0f / 60.0f);
  if (device.consumeSkip()) engine.skipMovie();

  gfx::Device::InputState menuInput = input;
  // `--mouse` sets the cursor directly: in a screenshot the window may have no
  // focus, and SDL then does not give the real position.
  if (options.mouseX >= 0.0f) {
    menuInput.mouseX = options.mouseX;
    menuInput.mouseY = options.mouseY;
    if (options.click && frameNumber == 1) menuInput.clicked = true;
  }

  // Screens the engine still draws itself: the intro fill and the loading screen.
  // The menu proper is the Flash movie below.
  const bool loading = engine.state() == engine::State::Loading;
  const int quad = engine.state() == engine::State::Intro ? introQuad_ : loading ? loadingQuad_
                                                                                 : -1;
  std::vector<gfx::MeshRenderer::DrawItem> screen;
  const auto push = [&](int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= sceneOk.size()) return;
    if (!sceneOk[static_cast<std::size_t>(index)]) return;
    screen.push_back(gfx::MeshRenderer::DrawItem{&sceneMeshes[static_cast<std::size_t>(index)],
                                                 Mat4::identity()});
  };
  push(quad);
  if (loading) {
    for (const int index : loadingTextQuads_) push(index);
  }

#if OBF2_HAVE_FLASH
  if (!movie_.isOpen()) return screen;

  std::vector<gfx::MeshRenderer::DrawItem> items;
  // The background is loaded once: it does not change.
  if (!backgroundReady_ && !background_.empty()) {
    if (auto uploaded = renderer.upload(mesh::buildScreenQuad(background_), resolve)) {
      backgroundMesh_ = std::move(*uploaded);
      backgroundReady_ = true;
    }
  }
  if (backgroundReady_) {
    items.push_back(gfx::MeshRenderer::DrawItem{&backgroundMesh_, Mat4::identity(), {1, 1, 1, 1}});
  }

  // Input. The menu is buttons, so without a mouse it stays a picture. We take the
  // same input the rest of the frame took rather than reading it twice: `clicked`
  // is a **transition** from released to pressed, and `readInput()` eats it.
  gfx::Device::InputState flashInput = menuInput;
  if (options.mouseX >= 0.0f) {
    flashInput.mouseX = options.mouseX;
    flashInput.mouseY = options.mouseY;
  }
  if (options.click && frameNumber == 20) flashInput.clicked = true;
  for (const auto& scheduled : options.clicks) {
    if (frameNumber != scheduled.frame) continue;
    flashInput.mouseX = scheduled.x;
    flashInput.mouseY = scheduled.y;
    flashInput.clicked = true;
  }
  // Convert from window to stage using the **actual** window size, not the one
  // asked for: the two differ whenever the window manager gives us something else,
  // and the cursor then lands a third of a screen off. SDL reports the mouse in
  // window points, and the movie is stretched across the whole window.
  int windowWidth = options.width, windowHeight = options.height;
  SDL_GetWindowSize(device.window(), &windowWidth, &windowHeight);
  if (windowWidth <= 0) windowWidth = options.width;
  if (windowHeight <= 0) windowHeight = options.height;
  double stageX = 0.0, stageY = 0.0;
  movie_.toStage(windowWidth, windowHeight, flashInput.mouseX, flashInput.mouseY, &stageX, &stageY);
  movie_.mouseMove(stageX, stageY);
  if (pressed_) {
    movie_.mouseMove(pressX_, pressY_);
    movie_.mouseButton(pressX_, pressY_, false);
    pressed_ = false;
  } else if (flashInput.clicked) {
    movie_.mouseButton(stageX, stageY, true);
    pressX_ = stageX;
    pressY_ = stageY;
    pressed_ = true;
  }

  // Orders the menu gives us. The movie cannot start a level by itself — in the
  // original the engine hosts the player, so the calls land in the engine
  // directly. Here they arrive as lines and go through the console, the same path
  // our own menu used.
  for (std::string order = movie_.takeCommand(); !order.empty(); order = movie_.takeCommand()) {
    std::printf("Flash menu: %s\n", order.c_str());
    if (order == "quit") {
      quit_ = true;
      continue;
    }
    if (order.rfind("level ", 0) != 0) continue;
    const std::size_t from = 6;
    const std::size_t to = order.find(' ', from);
    const std::string path = order.substr(from, to == std::string::npos ? to : to - from);
    // The menu spells level paths in lower case (`dalian_plant`), the directories
    // are not (`Dalian_plant`).
    for (const auto& level : engine.levels()) {
      if (level.directory.size() != path.size()) continue;
      const bool same =
          std::equal(level.directory.begin(), level.directory.end(), path.begin(),
                     [](char a, char b) { return std::tolower(a) == std::tolower(b); });
      if (same) {
        requestedLevel_ = level.directory;
        break;
      }
    }
    if (requestedLevel_.empty()) {
      std::printf("  the level %s is not among those found\n", path.c_str());
    }
  }

  // A step of the movie and the transfer of the frame into a texture. RGBA -> BGRA:
  // our texture loader expects the channel order of a DDS.
  movie_.advance();
  const auto& rgba = movie_.render();
  if (flash_ && rgba.size() == flash_->data.size()) {
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
      flash_->data[i + 0] = static_cast<std::byte>(rgba[i + 2]);
      flash_->data[i + 1] = static_cast<std::byte>(rgba[i + 1]);
      flash_->data[i + 2] = static_cast<std::byte>(rgba[i + 0]);
      flash_->data[i + 3] = static_cast<std::byte>(rgba[i + 3]);
    }
  }
  // The movie is drawn as one rectangle over the whole frame with the texture
  // `#flash`. Every frame of the movie is its own, so the mesh is reloaded each
  // time; for the menu that is cheap.
  if (flashUploaded_) renderer.release(flashMesh_);
  flashUploaded_ = false;
  if (auto uploaded = renderer.upload(mesh::buildScreenQuad("#flash"), resolve)) {
    flashMesh_ = std::move(*uploaded);
    flashUploaded_ = true;
    items.push_back(gfx::MeshRenderer::DrawItem{&flashMesh_, Mat4::identity(), {1, 1, 1, 1}});
  }
  return items.empty() ? screen : items;
#else
  (void)renderer;
  (void)resolve;
  return screen;
#endif
}

void MainMenu::release(gfx::MeshRenderer& renderer) {
  if (flashUploaded_) renderer.release(flashMesh_);
  flashUploaded_ = false;
  if (backgroundReady_) renderer.release(backgroundMesh_);
  backgroundReady_ = false;
}

}  // namespace obf2::app
