#include "obf2/app/main_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "obf2/core/path.h"

#include "obf2/font/font_file.h"
#include "obf2/font/text.h"
#include "obf2/mesh/primitives.h"

namespace obf2::app {
namespace {

// A STAND-IN for the main menu's background. The original's is `Movies/menu.bik`,
// which the menu movie loads into its own `background` clip (`loadMovie
// ("?menu.bik")`, `RendDX9.dll` 0x101ba466; docs/functions/startup.md). We do not
// decode Bink, so a picture from the menu's folder stands in for it — one nothing
// in the game references.
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
  engine.boot(files, modDir, options.homeDir);
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
  // The queue Init.con filled (`bf2Engine.playMovie`). `viewIntroMovie` is printed
  // only to say it gates nothing: the client never reads it (movie_queue.h).
  const auto& movies = engine.movies();
  std::printf("  movies queued: %zu (viewIntroMovie %d, read by nobody in the engine)\n",
              movies.queued().size(), settings.general.viewIntroMovie ? 1 : 0);
  double total = 0.0;
  for (const auto& movie : movies.queued()) {
    const auto seconds = engine.openMovie(movie.name);
    if (seconds) total += *seconds;
    std::printf("    %-22s flag %d  %s\n", movie.name.c_str(), movie.flag,
                seconds ? (std::to_string(*seconds).substr(0, 6) + " s").c_str()
                        : "does not open, skipped");
  }
  // Bink is decoded through FFmpeg when the build has it (obf2::video); without
  // it each movie is black for its own length — said out loud, because otherwise
  // the first thing the program does is show minutes of nothing and look hung.
  if (movies.playing()) {
#if OBF2_HAVE_VIDEO
    std::printf("    %.1f s of movies, decoded through FFmpeg; Escape skips a flag-0 one\n", total);
#else
    std::printf("    the movies are not decoded (built without FFmpeg) — %.1f s of black; "
                "Escape skips a flag-0 one\n",
                total);
#endif
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

void MainMenu::openMovie(FileSystem& files, engine::Engine& engine, const Options& options) {
  // The menu is the game's own movie. It lives in `Menu_client.zip`, mounted as
  // `Menu`, and nothing is unpacked (rule 5) — the player reads it, its pictures
  // and every file the bridge needs through the engine's file system.
  const std::string menuMoviePath = "Menu/External/FlashMenu/mainMenu.swf";
  std::string path = options.flashSwf.empty() ? menuMoviePath : options.flashSwf;
  if (background_.empty()) background_ = findBackground(files);

  host_ = makeMenuHost(files, engine);

  const bool opened = movie_.open(host_, path);
  if (opened) {
    std::printf("menu movie: %s, stage %u x %u\n", path.c_str(), movie_.width(), movie_.height());
    texture::Texture frame;
    frame.format = texture::Format::Bgra8;
    frame.width = movie_.width();
    frame.height = movie_.height();
    frame.mips.push_back(texture::MipLevel{
        frame.width, frame.height, 0, static_cast<std::uint32_t>(frame.width * frame.height * 4)});
    frame.data.assign(static_cast<std::size_t>(frame.width) * frame.height * 4, std::byte{0});
    flash_ = std::move(frame);
  } else {
    std::printf("menu movie: did not open — %s\n", path.c_str());
  }
}

std::vector<gfx::MeshRenderer::DrawItem> MainMenu::frame(
    engine::Engine& engine, gfx::Device& device, gfx::MeshRenderer& renderer,
    const TextureResolver& resolve, const gfx::Device::InputState& input, int frameNumber,
    const Options& options, const std::vector<gfx::GpuMesh>& sceneMeshes,
    const std::vector<bool>& sceneOk) {
  // Escape skips a movie while the intro plays and closes the program otherwise.
  const bool intro = engine.state() == engine::State::Intro;
  device.setEscapeQuits(!intro);
  const bool escape = device.consumeEscape();
  engine.update(1.0f / 60.0f, intro && escape);
  if (intro && engine.state() != engine::State::Intro) device.setEscapeQuits(true);

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
  // The startup movie, when one is playing and we can decode it: its own quad
  // over the whole window instead of the black fill.
  bool movieDrawn = false;
#if OBF2_HAVE_VIDEO
  const auto& movies = engine.movies();
  if (engine.state() == engine::State::Intro && movies.playing()) {
    if (movies.current() != movieName_) {
      movieName_ = movies.current();
      const std::filesystem::path path = engine.movieDir() / normalizeAssetPath(movieName_);
      if (video_.open(path.string())) {
        texture::Texture frame;
        frame.format = texture::Format::Bgra8;
        frame.width = video_.width();
        frame.height = video_.height();
        frame.mips.push_back(texture::MipLevel{frame.width, frame.height, 0,
                                               frame.width * frame.height * 4});
        frame.data.assign(static_cast<std::size_t>(frame.width) * frame.height * 4,
                          std::byte{0});
        movieFrame_ = std::move(frame);
        std::printf("movie: %s, %u x %u\n", movieName_.c_str(), video_.width(), video_.height());
      } else {
        movieFrame_.reset();
        std::printf("movie: %s does not decode, black for its length\n", movieName_.c_str());
      }
      // A new movie may be another size: its quad and texture are made anew.
      if (movieUploaded_) renderer.release(movieMesh_);
      movieUploaded_ = false;
    }
    if (movieFrame_ && !movieUploaded_) {
      if (auto uploaded = renderer.upload(mesh::buildScreenQuad("#movie"), resolve)) {
        movieMesh_ = std::move(*uploaded);
        movieUploaded_ = true;
      }
    }
    // Each decoded frame goes into the quad's own texture; the quad stays.
    if (movieFrame_ && movieUploaded_ && video_.advanceTo(movies.elapsed()) && !movieMesh_.ranges.empty()) {
      const auto& bgra = video_.bgra();
      if (bgra.size() == movieFrame_->data.size()) {
        renderer.updateTexture(movieMesh_.ranges[0].texture, reinterpret_cast<const std::byte*>(bgra.data()),
                               movieFrame_->width, movieFrame_->height);
      }
    }
    if (movieUploaded_) {
      screen.push_back(gfx::MeshRenderer::DrawItem{&movieMesh_, Mat4::identity()});
      movieDrawn = true;
    }
  } else if (!movieName_.empty()) {
    movieName_.clear();
    video_.close();
    movieFrame_.reset();
  }
#endif
  if (!movieDrawn) push(quad);
  if (loading) {
    for (const int index : loadingTextQuads_) push(index);
  }

  // While a movie plays the main loop takes only the movie arm (`BF2.exe`
  // 0x40cee9): the menu is neither stepped nor drawn.
  if (engine.state() == engine::State::Intro) return screen;

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
    // `Profile.loginOfflineAccount(n)` (SwiffPlayer.dll 0x1005e850) logs the
    // local profile manager in, which loads that profile's settings.
    if (order.rfind("login ", 0) == 0) {
      engine.login(order.substr(6));
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
    // The mode and size after the path (`level <path> <mode> <size>`, the
    // bridge's createServer) — what the loading screen shows of the round.
    if (to != std::string::npos) {
      const std::size_t sizeAt = order.find(' ', to + 1);
      requestedMode_ = order.substr(to + 1, sizeAt == std::string::npos ? std::string::npos : sizeAt - to - 1);
      if (sizeAt != std::string::npos) requestedSize_ = std::atoi(order.c_str() + sizeAt + 1);
    }
    if (requestedLevel_.empty()) {
      std::printf("  the level %s is not among those found\n", path.c_str());
    }
  }

  // A step of the movie and the transfer of the frame into a texture. RGBA -> BGRA:
  // our texture loader expects the channel order of a DDS. The step is the menu
  // frame's time, the same 1/60 the engine's update takes above.
  movie_.advance(1.0 / 60.0);
  const auto& rgba = movie_.render();
  if (flash_ && rgba.size() == flash_->data.size()) {
    // Raw pointers, a pixel at a time: through `optional->` and `operator[]`
    // per byte this loop alone took a third of the debug build's frame.
    const std::uint8_t* src = rgba.data();
    auto* dst = reinterpret_cast<std::uint8_t*>(flash_->data.data());
    const std::size_t n = rgba.size() / 4;
    for (std::size_t i = 0; i < n; ++i, src += 4, dst += 4) {
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
      dst[3] = src[3];
    }
  }
  // The movie is drawn as one rectangle over the whole frame with the texture
  // `#flash`. The rectangle is made once; each frame only its texture's pixels
  // go to the card (re-making the mesh copied the 3 MB picture twice and
  // created a texture per frame).
  if (!flashUploaded_) {
    if (auto uploaded = renderer.upload(mesh::buildScreenQuad("#flash"), resolve)) {
      flashMesh_ = std::move(*uploaded);
      flashUploaded_ = true;
    }
  } else if (flash_ && !flashMesh_.ranges.empty()) {
    renderer.updateTexture(flashMesh_.ranges[0].texture, flash_->data.data(), flash_->width, flash_->height);
  }
  if (flashUploaded_) {
    items.push_back(gfx::MeshRenderer::DrawItem{&flashMesh_, Mat4::identity(), {1, 1, 1, 1}});
  }
  return items.empty() ? screen : items;
}

void MainMenu::release(gfx::MeshRenderer& renderer) {
  if (movieUploaded_) renderer.release(movieMesh_);
  movieUploaded_ = false;
  if (flashUploaded_) renderer.release(flashMesh_);
  flashUploaded_ = false;
  if (backgroundReady_) renderer.release(backgroundMesh_);
  backgroundReady_ = false;
}

}  // namespace obf2::app
