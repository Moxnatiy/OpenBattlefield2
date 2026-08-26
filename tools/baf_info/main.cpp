// baf_info — що всередині анімації `.baf`.
//
//   baf_info <modDir> <шлях/до/файлу.baf> [кадр]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "obf2/mesh/animation.h"
#include "obf2/vfs/filesystem.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: baf_info <modDir> <файл.baf> [кадр]\n", stderr);
    return 2;
  }

  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, argv[1], std::filesystem::path(argv[1]) / list);
  }
  files.mountDirectory(argv[1]);

  const auto bytes = files.read(argv[2]);
  if (!bytes) {
    std::fprintf(stderr, "не знайдено: %s\n", argv[2]);
    return 1;
  }

  std::string error;
  const auto animation = obf2::mesh::loadBoneAnimation(*bytes, &error);
  if (!animation) {
    std::fprintf(stderr, "не розібрано: %s\n", error.c_str());
    return 1;
  }

  std::printf("версія %u, кісток %zu, кадрів %u, точність %u, тривалість %.2f с\n",
              animation->version, animation->boneIds.size(), animation->frameCount,
              animation->precision, animation->duration());

  const std::uint32_t frame =
      argc > 3 ? static_cast<std::uint32_t>(std::atoi(argv[3])) : 0;
  std::printf("кадр %u:\n", frame);
  for (std::size_t i = 0; i < animation->tracks.size() && i < 12; ++i) {
    float rotation[4];
    obf2::mesh::Vec3 position;
    animation->sample(i, frame, rotation, &position);
    std::printf("  кістка %3u  поворот %6.3f %6.3f %6.3f %6.3f  зсув %7.3f %7.3f %7.3f\n",
                animation->boneIds[i], rotation[0], rotation[1], rotation[2], rotation[3],
                position.x, position.y, position.z);
  }
  return 0;
}
