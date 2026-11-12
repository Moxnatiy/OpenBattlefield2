#include "obf2/app/menu_movie.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "obf2/gfx/device.h"

namespace obf2::app {

swf::Host makeMenuHost(FileSystem& files, engine::Engine& engine) {
  swf::Host host;
  // Through the mounted archives first; then the mod's own folder on disk —
  // `Levels/<level>/Info/` (the menu's map pictures, `$` paths) lies there
  // loose, outside every archive.
  host.readFile = [&files, modDir = engine.movieDir().parent_path()](
                      const std::string& name) -> std::optional<std::vector<std::byte>> {
    if (auto bytes = files.read(name)) return bytes;
    std::ifstream in(modDir / name, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    std::memcpy(out.data(), raw.data(), raw.size());
    return out;
  };
  // A key the dictionary has with an empty text is empty — English.utxt's
  // `MODAL_disclaimer` is such a line, and the loading modal showed the key;
  // only an absent key comes back as itself.
  host.localize = [&engine](const std::string& key) {
    const auto text = engine.lexicon().find(key);
    return text ? std::string(*text) : key;
  };
  for (const auto& p : engine.localProfiles()) {
    host.profiles.push_back(swf::Host::Profile{p.id, p.nick.empty() ? p.name : p.nick, p.gamespyNick});
  }
  host.defaultUser = engine.defaultUser();
  host.namePrefix = engine.namePrefix();
  for (const auto& level : engine.levels()) host.levelDirectories.push_back(level.directory);
  host.hostVersion = std::string("OpenBattlefield2 ") + OBF2_VERSION_STRING;
  host.modDirectory = engine.movieDir().string();
  // ServerSettings and GeneralSettings read and write the engine's objects
  // live: the profile logged in at runtime fills them.
  host.setting = [&engine](const std::string& object, const std::string& name) {
    return engine.setting(object, name);
  };
  host.console = [&engine](const std::string& line) { engine.console().executeLine(line); };
  host.gameRunning = [&engine] { return engine.state() == engine::State::InGame; };
  host.capsLock = [] { return gfx::Device::capsLockOn(); };
  host.cacheCredits = [&engine, &files] {
    engine.cacheCredits(files);
    std::printf("menu: credits, %zu lines, fade %d, scroll %d\n", engine.credits().lines.size(),
                engine.credits().fadeSpeed, engine.credits().scrollSpeed);
  };
  host.scrollFadeSpeed = [&engine] { return engine.credits().fadeSpeed; };
  return host;
}

bool MenuMovie::open(const swf::Host& host, const std::string& path) {
#if OBF2_HAVE_FLASH
  if (ruffle_) {
    flash::Movie::setFileReader(host.readFile);
    const auto bytes = host.readFile ? host.readFile(path) : std::nullopt;
    return bytes && flash_.openFromMemory(*bytes, path);
  }
#endif
  return native_.open(host, path);
}

bool MenuMovie::isOpen() const {
#if OBF2_HAVE_FLASH
  if (ruffle_) return flash_.isOpen();
#endif
  return native_.isOpen();
}

std::uint32_t MenuMovie::width() const {
#if OBF2_HAVE_FLASH
  if (ruffle_) return flash_.width();
#endif
  return native_.width();
}

std::uint32_t MenuMovie::height() const {
#if OBF2_HAVE_FLASH
  if (ruffle_) return flash_.height();
#endif
  return native_.height();
}

void MenuMovie::toStage(int windowWidth, int windowHeight, double x, double y, double* sx,
                        double* sy) const {
#if OBF2_HAVE_FLASH
  if (ruffle_) {
    flash_.toStage(windowWidth, windowHeight, x, y, sx, sy);
    return;
  }
#endif
  *sx = windowWidth > 0 ? x * width() / windowWidth : x;
  *sy = windowHeight > 0 ? y * height() / windowHeight : y;
}

void MenuMovie::mouseMove(double x, double y) {
#if OBF2_HAVE_FLASH
  if (ruffle_) {
    flash_.mouseMove(x, y);
    return;
  }
#endif
  mouseX_ = x;
  mouseY_ = y;
  native_.mouse(mouseX_, mouseY_, down_);
}

void MenuMovie::mouseButton(double x, double y, bool down) {
#if OBF2_HAVE_FLASH
  if (ruffle_) {
    flash_.mouseButton(x, y, down);
    return;
  }
#endif
  mouseX_ = x;
  mouseY_ = y;
  down_ = down;
  native_.mouse(mouseX_, mouseY_, down_);
}

std::string MenuMovie::takeCommand() {
#if OBF2_HAVE_FLASH
  if (ruffle_) return flash_.takeCommand();
#endif
  return native_.takeCommand();
}

void MenuMovie::advance(double seconds) {
#if OBF2_HAVE_FLASH
  if (ruffle_) {
    flash_.advance();
    return;
  }
#endif
  native_.advance(seconds);
}

const std::vector<std::uint8_t>& MenuMovie::render() {
#if OBF2_HAVE_FLASH
  if (ruffle_) return flash_.render();
#endif
  return native_.render();
}

}  // namespace obf2::app
