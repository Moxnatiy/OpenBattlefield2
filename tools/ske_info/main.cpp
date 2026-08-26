// ske_info — що всередині скелета `.ske`.
//
//   ske_info <modDir> <шлях/до/файлу.ske>
#include <cstdio>
#include <string>

#include "obf2/mesh/skeleton.h"
#include "obf2/vfs/filesystem.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: ske_info <modDir> <файл.ske>\n", stderr);
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
  const auto skeleton = obf2::mesh::loadSkeleton(*bytes, &error);
  if (!skeleton) {
    std::fprintf(stderr, "не розібрано: %s\n", error.c_str());
    return 1;
  }

  std::printf("версія %u, кісток %zu\n", skeleton->version, skeleton->bones.size());
  for (std::size_t i = 0; i < skeleton->bones.size(); ++i) {
    const auto& bone = skeleton->bones[i];
    // Відступ за глибиною — так ієрархію видно з першого погляду.
    int depth = 0;
    for (int parent = bone.parent; parent >= 0; ++depth) {
      parent = skeleton->bones[static_cast<std::size_t>(parent)].parent;
    }
    std::printf("%3zu %*s%-24s зсув %7.3f %7.3f %7.3f\n", i, depth * 2, "", bone.name.c_str(),
                bone.position.x, bone.position.y, bone.position.z);
  }
  return 0;
}
