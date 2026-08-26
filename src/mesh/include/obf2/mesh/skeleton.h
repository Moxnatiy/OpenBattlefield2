#pragma once
// Скелет Refractor 2 — файли `.ske`.
//
// Формат простий і повністю перевірений на даних гри: розмір, порахований
// за цією розкладкою, збігається з розміром файлу байт у байт
// (`soldiers/Common/Animations/3p_setup.ske` — 80 кісток, 3399 байтів).
//
//   u32  версія (2)
//   u32  кількість кісток
//   для кожної кістки:
//     u16  довжина імені разом із нулем
//     char ім'я[довжина]
//     i16  батько (-1 у кореня)
//     f32  поворот x, y, z, w (кватерніон)
//     f32  зсув x, y, z
//
// Поворот і зсув — **локальні, відносно батька**: у солдата від коліна до
// гомілки рівно 0.075, від гомілки до стопи 0.385. Тобто це поза спокою,
// а не обернена матриця прив'язки.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "obf2/mesh/bf2_mesh.h"

namespace obf2::mesh {

// Ім'я окреме від `mesh::Bone` з bf2_mesh.h: там це кістка **риґу меша**
// (номер плюс матриця), а тут — кістка скелета з іменем та ієрархією.
struct SkeletonBone {
  std::string name;
  int parent = -1;  // -1 у кореня
  // Кватерніон повороту (x, y, z, w) і зсув відносно батька.
  float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  Vec3 position;
};

struct Skeleton {
  std::uint32_t version = 0;
  std::vector<SkeletonBone> bones;

  // Індекс кістки за іменем; -1, якщо такої немає.
  int find(std::string_view name) const;
};

std::optional<Skeleton> loadSkeleton(std::span<const std::byte> bytes, std::string* error = nullptr);

}  // namespace obf2::mesh
