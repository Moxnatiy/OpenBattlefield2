// anim_info — система анімацій солдата: дерево тригерів і що грає у стані.
//
//   anim_info <modDir> <скрипт.inc> [поза] [швидкість]
//
// поза: 0 стоїть, 1 присів, 2 лежить, 3 пливе.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "obf2/anim/system.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: anim_info <modDir> <скрипт.inc> [поза] [швидкість]\n", stderr);
    return 2;
  }

  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, argv[1], std::filesystem::path(argv[1]) / list);
  }
  files.mountDirectory(argv[1]);

  std::string error;
  const auto system = obf2::anim::System::load(files, argv[2], &error);
  if (!system) {
    std::fprintf(stderr, "не прочитано: %s\n", error.c_str());
    return 1;
  }

  std::printf("анімацій %zu, бандлів %zu, тригерів %zu, діапазонів %zu\n",
              system->animations().size(), system->bundles().size(), system->triggers().size(),
              system->valueHolders().size());

  if (argc <= 3) {
    // Без стану просто показуємо дерево.
    for (const std::string& root : system->roots()) std::printf("корінь: %s\n", root.c_str());
    return 0;
  }

  obf2::anim::State state;
  state.pose = static_cast<obf2::anim::Pose>(std::atoi(argv[3]));
  state.speed = argc > 4 ? static_cast<float>(std::atof(argv[4])) : 0.0f;

  std::printf("поза %d, швидкість %.2f -> бандли:\n", std::atoi(argv[3]), state.speed);
  for (const obf2::anim::Bundle* bundle : system->select(state)) {
    std::printf("  %-28s анімацій %zu", bundle->name.c_str(), bundle->animations.size());
    if (!bundle->animations.empty()) {
      const std::string& first = bundle->animations.front();
      const std::size_t slash = first.find_last_of("/\\");
      std::printf("  %s", slash == std::string::npos ? first.c_str() : first.c_str() + slash + 1);
    }
    std::printf("\n");
  }
  return 0;
}
