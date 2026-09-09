#include "obf2/flash/movie.h"

namespace obf2::flash {
namespace {

// The C interface of the `obf2_flash` library (Rust, src/flash/src/lib.rs).
extern "C" {
void* obf2_flash_open(const char* path, std::uint32_t width, std::uint32_t height);
void obf2_flash_close(void* movie);
void obf2_flash_size(const void* movie, std::uint32_t* width, std::uint32_t* height);
void obf2_flash_advance(void* movie);
std::size_t obf2_flash_render(void* movie, std::uint8_t* pixels, std::size_t length);
void obf2_flash_mouse_move(void* movie, double x, double y);
void obf2_flash_mouse_button(void* movie, double x, double y, int down);
void obf2_flash_text(void* movie, std::uint32_t codepoint);
void obf2_flash_key(void* movie, int code, int down);
std::size_t obf2_flash_take_command(char* buffer, std::size_t length);
void obf2_flash_set_host_version(const char* text);
}

}  // namespace

Movie::~Movie() { close(); }

// Defined in `images.cpp`: hands our DDS decoder to the library.
void installImageDecoder();

bool Movie::open(const std::string& path, std::uint32_t width, std::uint32_t height) {
  close();
  installImageDecoder();
  obf2_flash_set_host_version("OpenBattlefield2 " OBF2_VERSION);
  handle_ = obf2_flash_open(path.c_str(), width, height);
  if (handle_ == nullptr) return false;
  obf2_flash_size(handle_, &width_, &height_);
  pixels_.assign(static_cast<std::size_t>(width_) * height_ * 4, 0);
  return true;
}

void Movie::close() {
  if (handle_ != nullptr) obf2_flash_close(handle_);
  handle_ = nullptr;
  width_ = height_ = 0;
  pixels_.clear();
}

void Movie::advance() {
  if (handle_ != nullptr) obf2_flash_advance(handle_);
}

const std::vector<std::uint8_t>& Movie::render() {
  if (handle_ == nullptr || pixels_.empty()) return pixels_;
  const std::size_t written = obf2_flash_render(handle_, pixels_.data(), pixels_.size());
  if (written == 0) pixels_.assign(pixels_.size(), 0);
  return pixels_;
}

void Movie::mouseMove(double x, double y) {
  if (handle_ != nullptr) obf2_flash_mouse_move(handle_, x, y);
}

void Movie::mouseButton(double x, double y, bool down) {
  if (handle_ != nullptr) obf2_flash_mouse_button(handle_, x, y, down ? 1 : 0);
}

void Movie::text(std::uint32_t codepoint) {
  if (handle_ != nullptr) obf2_flash_text(handle_, codepoint);
}

void Movie::key(Key which, bool down) {
  if (handle_ != nullptr) obf2_flash_key(handle_, static_cast<int>(which), down ? 1 : 0);
}

std::string Movie::takeCommand() {
  if (handle_ == nullptr) return {};
  // The queue lives in the library, not in the movie: orders outlive a
  // single frame and there is only one menu at a time.
  char buffer[512];
  const std::size_t written = obf2_flash_take_command(buffer, sizeof(buffer));
  return std::string(buffer, written);
}

void Movie::toStage(int windowWidth, int windowHeight, float windowX, float windowY,
                    double* stageX, double* stageY) const {
  // The movie is drawn over the whole frame, so the conversion is a plain stretch.
  const double scaleX = windowWidth > 0 ? static_cast<double>(width_) / windowWidth : 1.0;
  const double scaleY = windowHeight > 0 ? static_cast<double>(height_) / windowHeight : 1.0;
  if (stageX != nullptr) *stageX = windowX * scaleX;
  if (stageY != nullptr) *stageY = windowY * scaleY;
}

}  // namespace obf2::flash
