#include "obf2/swf/player.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "base/tu_file.h"
#include "bridge.h"
#include "gameswf/gameswf.h"
#include "raster.h"
#include "obf2/texture/dds.h"
#include "stb_image.h"
#if OBF2_HAVE_VIDEO
#include "obf2/video/video.h"
#endif

namespace obf2::swf {
namespace {

// gameswf's file opener and log are global callbacks; one player runs at a
// time, so they reach it through these.
const Host* g_host = nullptr;
std::vector<std::vector<std::byte>>* g_buffers = nullptr;
std::map<std::string, int>* g_complaints = nullptr;
Raster* g_raster = nullptr;

// A video playing in a loadMovie fill: its decoder and the picture it feeds.
struct PlayingVideo {
#if OBF2_HAVE_VIDEO
  std::unique_ptr<video::Video> video;
#endif
  gameswf::bitmap_info* image = nullptr;
  double seconds = 0.0;
};
std::vector<PlayingVideo>* g_videos = nullptr;

// `loadMovie`'s pictures, routed by the first character as SwiffRender's
// loadTexture does (RendDX9.dll): `?name` a Bink video from the mod's
// `Movies/` (0x101ba466), laid out 256 x 256 whatever its size (0x101ba5d7);
// anything else a picture under `Menu/External/FlashMenu/` (the other
// prefixes — `@`, `$`, `#`, a logo URL — are B2BF's reading, not done here).
gameswf::bitmap_info* loadImage(const char* url, int* width, int* height) {
  if (g_host == nullptr || g_raster == nullptr || url == nullptr || *url == 0) return nullptr;
  if (std::getenv("OBF2_SWF_TRACE")) std::printf("[swf] loadMovie picture: %s\n", url);
  if (url[0] == '?') {
#if OBF2_HAVE_VIDEO
    auto player = std::make_unique<video::Video>();
    const std::string path = g_host->modDirectory + "/Movies/" + (url + 1);
    if (!player->open(path)) return nullptr;
    std::vector<std::uint8_t> black(static_cast<std::size_t>(player->width()) * player->height() * 4, 0);
    gameswf::bitmap_info* image =
        g_raster->makeImage(static_cast<int>(player->width()), static_cast<int>(player->height()), black.data());
    *width = 256;
    *height = 256;
    g_videos->push_back(PlayingVideo{std::move(player), image, 0.0});
    return image;
#else
    return nullptr;
#endif
  }
  std::string name = url;
  for (char& c : name) {
    if (c == '\\') c = '/';
  }
  // `$` (RendDX9.dll 0x101ba3e2): the character is dropped and the path is
  // the mod's own — the singleplayer page's `$/Levels/<level>/Info/
  // <mode>_<size>_menuMap.png`. Anything else is under the menu's folder
  // (0x101ba1dc).
  std::optional<std::vector<std::byte>> bytes;
  if (name[0] == '$') {
    std::string rest = name.substr(1);
    while (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
    bytes = g_host->readFile(rest);
  } else {
    bytes = g_host->readFile("Menu/External/FlashMenu/" + name);
  }
  if (!bytes) {
    if (std::getenv("OBF2_SWF_TRACE")) std::printf("[swf] loadMovie picture not found: %s\n", url);
    return nullptr;
  }
  // Most of the menu's `.png` files are DDS (scrollArrow_up.png, button-bkg-N.png,
  // the levels' menuMap.png all start with "DDS "): the original's renderer
  // loads them by content (D3DX), not by name. Decoded to RGBA here.
  if (bytes->size() >= 4 && std::memcmp(bytes->data(), "DDS ", 4) == 0) {
    const auto dds = texture::loadDds(*bytes);
    const std::vector<std::uint8_t> rgba = dds ? texture::decodeRgba8(*dds) : std::vector<std::uint8_t>{};
    if (rgba.empty()) {
      if (std::getenv("OBF2_SWF_TRACE")) std::printf("[swf] loadMovie picture %s: a DDS not decoded\n", url);
      return nullptr;
    }
    gameswf::bitmap_info* image =
        g_raster->makeImage(static_cast<int>(dds->width), static_cast<int>(dds->height), rgba.data());
    *width = static_cast<int>(dds->width);
    *height = static_cast<int>(dds->height);
    return image;
  }
  int w = 0, h = 0, channels = 0;
  unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes->data()),
                                                static_cast<int>(bytes->size()), &w, &h, &channels, 4);
  if (pixels == nullptr) {
    if (std::getenv("OBF2_SWF_TRACE")) {
      std::printf("[swf] loadMovie picture %s: %zu bytes, not decoded (%s)\n", url, bytes->size(),
                  stbi_failure_reason());
    }
    return nullptr;
  }
  gameswf::bitmap_info* image = g_raster->makeImage(w, h, pixels);
  stbi_image_free(pixels);
  *width = w;
  *height = h;
  return image;
}

tu_file* openFile(const char* path) {
  if (g_host == nullptr || !g_host->readFile) return nullptr;
  std::string name = path;
  for (char& c : name) {
    if (c == '\\') c = '/';
  }
  auto bytes = g_host->readFile(name);
  if (!bytes) bytes = g_host->readFile("Menu/External/FlashMenu/" + name);
  if (!bytes) return nullptr;
  g_buffers->push_back(std::move(*bytes));
  auto& kept = g_buffers->back();
  return new tu_file(tu_file::memory_buffer, static_cast<int>(kept.size()), kept.data());
}

void logLine(bool error, const char* message) {
  // `OBF2_SWF_TRACE=1` prints everything the library and the movie's trace()
  // say — how a run is followed page by page.
  static const bool print = std::getenv("OBF2_SWF_TRACE") != nullptr;
  if (print) std::printf("[swf%s] %s%s", error ? " error" : "", message,
                         (*message && message[std::strlen(message) - 1] == '\n') ? "" : "\n");
  if (!error || g_complaints == nullptr) return;
  std::string key;
  for (const char* p = message; *p; ++p) {
    if (*p == '\n' || *p == '\r') continue;
    key += (*p >= '0' && *p <= '9') ? '#' : *p;
  }
  (*g_complaints)[key]++;
}

}  // namespace

struct Player::Impl {
  Host host;
  Raster raster;
  std::vector<std::vector<std::byte>> buffers;
  std::map<std::string, int> complaints;
  std::vector<std::pair<std::string, int>> complaintList;
  std::vector<std::string> commands;
  gameswf::movie_definition* definition = nullptr;
  gameswf::movie_interface* movie = nullptr;
  std::vector<std::uint8_t> bgra;
  std::vector<PlayingVideo> videos;
  int mouseX = 0, mouseY = 0, buttons = 0;
  int steps = 0;
};

Player::Player() : impl_(std::make_unique<Impl>()) {}

Player::~Player() {
  if (impl_->movie) impl_->movie->drop_ref();
  if (impl_->definition) impl_->definition->drop_ref();
  if (g_host == &impl_->host) {
    g_host = nullptr;
    g_buffers = nullptr;
    g_complaints = nullptr;
    g_raster = nullptr;
    g_videos = nullptr;
  }
}

bool Player::open(const Host& host, const std::string& path) {
  impl_->host = host;
  g_host = &impl_->host;
  g_buffers = &impl_->buffers;
  g_complaints = &impl_->complaints;
  g_raster = &impl_->raster;
  g_videos = &impl_->videos;
  gameswf::register_image_loader_callback(loadImage);
  gameswf::register_file_opener_callback(openFile);
  gameswf::register_log_callback(logLine);
  // `OBF2_SWF_VERBOSE=1`: every action the VM runs, into the trace above.
  gameswf::set_verbose_action(std::getenv("OBF2_SWF_VERBOSE") != nullptr);
  gameswf::set_render_handler(&impl_->raster);
  installBridge(&impl_->host, &impl_->commands);

  impl_->definition = gameswf::create_movie(path.c_str());
  if (impl_->definition == nullptr) return false;
  impl_->definition->add_ref();
  impl_->movie = impl_->definition->create_instance();
  if (impl_->movie == nullptr) return false;
  impl_->movie->add_ref();
  const int w = static_cast<int>(impl_->definition->get_width_pixels());
  const int h = static_cast<int>(impl_->definition->get_height_pixels());
  impl_->raster.resize(w, h);
  impl_->movie->set_display_viewport(0, 0, w, h);
  impl_->bgra.assign(static_cast<std::size_t>(w) * h * 4, 0);
  return true;
}

bool Player::isOpen() const { return impl_->movie != nullptr; }
std::uint32_t Player::width() const { return static_cast<std::uint32_t>(impl_->raster.width()); }
std::uint32_t Player::height() const { return static_cast<std::uint32_t>(impl_->raster.height()); }

void Player::advance(double seconds) {
  if (!impl_->movie) return;
  impl_->movie->notify_mouse_state(impl_->mouseX, impl_->mouseY, impl_->buttons);
  impl_->movie->advance(static_cast<float>(seconds));
  flushDelayedCaptions();
  // `OBF2_SWF_DUMP=<step>`: the display tree after that step, into the log.
  ++impl_->steps;
  static const int dumpAt = std::getenv("OBF2_SWF_DUMP") ? std::atoi(std::getenv("OBF2_SWF_DUMP")) : -1;
  if (impl_->steps == dumpAt) {
    std::printf("[swf] display tree after step %d:\n", dumpAt);
    gameswf::dump_display_tree(impl_->movie);
  }
#if OBF2_HAVE_VIDEO
  // The videos in loadMovie fills step on their own clock (SwiffRender
  // advances one each time its fill is drawn, Bink's clock gating it); a
  // video at its end starts again — the menu's background loops. How the
  // original loops it is not read.
  for (auto& v : impl_->videos) {
    v.seconds += seconds;
    if (v.video->ended()) {
      v.video->open(v.video->path());
      v.seconds = 0.0;
    }
    if (v.video->advanceTo(v.seconds)) Raster::updateImage(v.image, v.video->bgra().data(), true);
  }
#endif
}

const std::vector<std::uint8_t>& Player::render() {
  if (!impl_->movie) return impl_->bgra;
  impl_->movie->display();
  return impl_->raster.pixels();
}

void Player::mouse(double x, double y, bool down) {
  impl_->mouseX = static_cast<int>(x);
  impl_->mouseY = static_cast<int>(y);
  impl_->buttons = down ? 1 : 0;
}

std::string Player::takeCommand() {
  if (impl_->commands.empty()) return {};
  std::string c = impl_->commands.front();
  impl_->commands.erase(impl_->commands.begin());
  return c;
}

const std::vector<std::pair<std::string, int>>& Player::complaints() const {
  impl_->complaintList.assign(impl_->complaints.begin(), impl_->complaints.end());
  return impl_->complaintList;
}

}  // namespace obf2::swf
