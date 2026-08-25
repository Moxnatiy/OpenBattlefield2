#include <cstring>

#include "obf2/texture/dds.h"
#include "stb_image.h"

namespace obf2::texture {

std::optional<Texture> loadPng(std::span<const std::byte> bytes, std::string* error) {
  int width = 0;
  int height = 0;
  int channels = 0;

  // Завжди просимо RGBA: так розкладка передбачувана, а альфа є навіть там,
  // де у файлі її не було.
  stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                          static_cast<int>(bytes.size()), &width, &height,
                                          &channels, 4);
  if (pixels == nullptr) {
    if (error) *error = "не вдалося розібрати PNG";
    return std::nullopt;
  }

  Texture texture;
  texture.format = Format::Bgra8;
  texture.width = static_cast<std::uint32_t>(width);
  texture.height = static_cast<std::uint32_t>(height);

  const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
  texture.data.resize(pixelCount * 4);
  // stb віддає RGBA, а наш Bgra8 очікує порядок B, G, R, A.
  for (std::size_t i = 0; i < pixelCount; ++i) {
    texture.data[i * 4 + 0] = static_cast<std::byte>(pixels[i * 4 + 2]);
    texture.data[i * 4 + 1] = static_cast<std::byte>(pixels[i * 4 + 1]);
    texture.data[i * 4 + 2] = static_cast<std::byte>(pixels[i * 4 + 0]);
    texture.data[i * 4 + 3] = static_cast<std::byte>(pixels[i * 4 + 3]);
  }
  stbi_image_free(pixels);

  texture.mips.push_back(MipLevel{texture.width, texture.height, 0, texture.data.size()});
  return texture;
}

std::optional<Texture> loadImage(std::span<const std::byte> bytes, std::string* error) {
  static constexpr std::byte kPng[4] = {std::byte{0x89}, std::byte{'P'}, std::byte{'N'},
                                        std::byte{'G'}};
  if (bytes.size() >= 4 && std::memcmp(bytes.data(), kPng, 4) == 0) {
    return loadPng(bytes, error);
  }
  return loadDds(bytes, error);
}

}  // namespace obf2::texture
