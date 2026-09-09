// DDS decoding for the Flash menu.
//
// Almost every image the menu loads is a DDS file with a `.png`
// extension: map previews, flags, award icons. The original player
// decodes them because the game hands it engine textures, not files.
//
// The decoder itself is ours and already exists (`obf2::texture`), so we
// do not write a second one in Rust — the library asks us instead. C++
// hands the function over once at start-up; a plain link would be
// circular, since the executable links the library.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "obf2/texture/dds.h"

namespace {

// Decode `data` into RGBA8. With `out` null only the size is reported,
// so the caller can allocate exactly once.
std::size_t decodeDds(const std::uint8_t* data, std::size_t length, std::uint8_t* out,
                      std::size_t capacity, std::uint32_t* width, std::uint32_t* height) {
  if (data == nullptr || length < 4) return 0;

  std::vector<std::byte> bytes(length);
  std::memcpy(bytes.data(), data, length);
  const auto texture = obf2::texture::loadDds(bytes);
  if (!texture || texture->mips.empty()) return 0;

  const auto rgba = obf2::texture::decodeRgba8(*texture, 0);
  if (rgba.empty()) return 0;

  if (width != nullptr) *width = texture->mips[0].width;
  if (height != nullptr) *height = texture->mips[0].height;
  if (out == nullptr) return rgba.size();
  if (capacity < rgba.size()) return 0;
  std::memcpy(out, rgba.data(), rgba.size());
  return rgba.size();
}

}  // namespace

extern "C" {
// Defined in the Rust library.
void obf2_flash_set_dds_decoder(std::size_t (*decoder)(const std::uint8_t*, std::size_t,
                                                       std::uint8_t*, std::size_t, std::uint32_t*,
                                                       std::uint32_t*));
}

namespace obf2::flash {

// Called once before the first movie is opened.
void installImageDecoder() { obf2_flash_set_dds_decoder(&decodeDds); }

}  // namespace obf2::flash
