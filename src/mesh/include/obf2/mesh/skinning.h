#pragma once
// Скелетна анімація: поза зі скелета й кліпу, і деформація меша.
//
// Ланцюжок такий самий, як в оригіналі:
//
//   .ske  — кістки з локальним поворотом і зсувом (поза спокою);
//   .baf  — на кожен кадр свій поворот і зсув для частини кісток;
//   .skinnedmesh — вершини з парою номерів кісток **у риґу** і вагою,
//                  плюс сам риґ: номер кістки в скелеті та обернена
//                  матриця прив'язки.
//
// Матриця, якою рухається вершина:
//   світова(кістка) * обернена_прив'язка(риґ)
#include <cstdint>
#include <vector>

#include "obf2/mesh/animation.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/skeleton.h"

namespace obf2::mesh {

// Один кліп у позі: анімація, кадр і вага.
//
// Модель узята з рушія (`Skeleton::applySimpleAnimationStage`): у кожної
// кістки свій стек застосованих кліпів **не більше п'яти**, і кліп із
// вагою 1 або більше цей стек **очищає** — тобто повністю володіє кісткою.
// Кліп торкається лише тих кісток, які перелічені в ньому самому, і саме
// так у BF2 виходить верх тіла від зброї й ноги від руху одночасно.
struct PoseStage {
  const BoneAnimation* animation = nullptr;
  std::uint32_t frame = 0;
  float weight = 1.0f;
};

// Скільки кліпів рушій тримає на одну кістку.
inline constexpr int kMaxPoseStagesPerBone = 5;

// Поза з кількох кліпів.
std::vector<Mat4> poseSkeleton(const Skeleton& skeleton, const std::vector<PoseStage>& stages);

// Світові матриці всіх кісток. Якщо кліп заданий, кістки з нього беруть
// поворот і зсув на вказаному кадрі, решта лишається в позі спокою.
//
// Ієрархія у файлі вже впорядкована (батько завжди раніше за дитину), тож
// вистачає одного проходу вперед.
std::vector<Mat4> poseSkeleton(const Skeleton& skeleton, const BoneAnimation* animation,
                               std::uint32_t frame);

// Деформує вершини за позою. `bindPose` — меш як він лежить у файлі,
// `out` отримує зміщені позиції й нормалі. Обидва мають однаковий розмір.
void skinMesh(const RenderMesh& bindPose, const std::vector<Mat4>& boneWorld, RenderMesh& out);

}  // namespace obf2::mesh
