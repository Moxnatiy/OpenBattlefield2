#pragma once
// DDS — контейнер текстур DirectDraw Surface. У BF2 1.5 зустрічаються рівно
// шість варіантів (перевірено по всіх 2230 файлах в архівах):
//
//   DXT5     999   DXT1     502   A4R4G4B4  337
//   A8R8G8B8 251   DXT3     131   R5G6B5      9   + один 8-бітний іконочний
//
// Тобто ні DX10-заголовків, ні кубічних мап, ні об'ємних текстур тут немає —
// і саме тому парсер може лишатися маленьким.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obf2::texture {

enum class Format {
  Bc1,      // DXT1 — 8 байт на блок 4x4
  Bc2,      // DXT3 — 16 байт на блок
  Bc3,      // DXT5 — 16 байт на блок
  Bgra8,    // A8R8G8B8
  Bgra4,    // A4R4G4B4
  Bgr565,   // R5G6B5
  R8,       // 8 біт, один канал
};

struct MipLevel {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t offset = 0;  // зсув у Texture::data
  std::size_t size = 0;
};

struct Texture {
  Format format = Format::Bc1;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<MipLevel> mips;   // mips[0] — повний розмір
  std::vector<std::byte> data;  // усі рівні підряд

  bool isCompressed() const {
    return format == Format::Bc1 || format == Format::Bc2 || format == Format::Bc3;
  }
};

std::string_view formatName(Format format);

// Скільки байтів займає один рівень заданого розміру.
std::size_t levelSize(Format format, std::uint32_t width, std::uint32_t height);

// nullopt + пояснення замість винятку: зіпсована текстура — очікувана
// ситуація, а не виняткова. Дані не копіюються зайвий раз, але й не
// зберігають посилання на вхідний буфер — Texture::data самодостатній.
std::optional<Texture> loadDds(std::span<const std::byte> bytes, std::string* error = nullptr);

// Текстура 1x1 заданого кольору. Потрібна там, де гра задає колір числом,
// а не файлом — наприклад renderer.waterColor у Water.con рівня.
Texture solidColor(float red, float green, float blue, float alpha = 1.0f);

}  // namespace obf2::texture
