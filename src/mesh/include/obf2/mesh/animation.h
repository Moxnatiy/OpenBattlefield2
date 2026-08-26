#pragma once
// Анімація кісток Refractor 2 — файли `.baf`.
//
// Розкладку взято з коду рушія (`dice::anim::BoneAnimation::load` і
// `dice::anim::CompressedAnim::GetValue` у Linux-сервері), а не вгадано:
//
//   u32 версія (має бути 4)
//   u16 кількість кісток
//   u16 номери кісток[кількість]
//   u32 кількість кадрів
//   u8  точність
//   для кожної кістки:
//     u16 скільки всього 16-бітних слів у цієї кістки
//     для кожного з 7 каналів (кватерніон x,y,z,w і зсув x,y,z):
//       u16 скільки слів у цього каналу
//       далі — потік «пробігів» (див. нижче)
//
// Потік каналу складається з пробігів. Кожен пробіг займає одне слово
// заголовка й далі значення:
//
//   байт 0: довжина пробігу в кадрах (біти 0..6); біт 7 — «стале значення»
//   байт 1: скільки слів до наступного пробігу
//   int16 значення[стале ? 1 : довжина]
//
// Значення переводяться в дійсні так: кватерніон — поділити на 32767,
// зсув — на `(1 << точність) - 1`. Тривалість кліпу — кадри / 24.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "obf2/mesh/bf2_mesh.h"

namespace obf2::mesh {

// Кількість каналів на кістку: чотири на поворот і три на зсув.
inline constexpr int kAnimationChannels = 7;
// Кадрів на секунду. У рушії тривалість це `кадри / 24.0`.
inline constexpr float kAnimationFramesPerSecond = 24.0f;

struct BoneAnimationTrack {
  // Сирі слова каналу — розпаковуються на льоту, як і в оригіналі.
  std::vector<std::int16_t> channels[kAnimationChannels];
};

struct BoneAnimation {
  std::uint32_t version = 0;
  std::uint32_t frameCount = 0;
  std::uint8_t precision = 0;
  std::vector<std::uint16_t> boneIds;
  std::vector<BoneAnimationTrack> tracks;

  float duration() const {
    return static_cast<float>(frameCount) / kAnimationFramesPerSecond;
  }

  // Одне значення каналу на заданому кадрі.
  float value(std::size_t bone, std::uint32_t frame, int channel) const;

  // Поворот і зсув кістки на кадрі.
  bool sample(std::size_t bone, std::uint32_t frame, float outRotation[4], Vec3* outPosition) const;
};

std::optional<BoneAnimation> loadBoneAnimation(std::span<const std::byte> bytes,
                                               std::string* error = nullptr);

}  // namespace obf2::mesh
