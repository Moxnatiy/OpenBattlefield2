// openbf2 — точка входу рушія.
//
//   openbf2 [--mod <шлях до mods/bf2>] [--frames N]
//
// Поки що: піднімає вікно з GPU-пристроєм і монтує дані гри так само, як це
// робить fileManager у Refractor 2 — тобто читає архіви на місці, за списком
// із ServerArchives.con / ClientArchives.con.

#include <cstdio>
#include <cstring>
#include <string>

#include "obf2/con/interpreter.h"
#include "obf2/core/platform.h"
#include "obf2/gfx/device.h"
#include "obf2/vfs/filesystem.h"

namespace {

struct Args {
  std::filesystem::path modDir = "Game Files/mods/bf2";
  int frames = 0;  // 0 = крутитися, доки не закриють вікно
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--mod" && i + 1 < argc) args.modDir = argv[++i];
    else if (flag == "--frames" && i + 1 < argc) args.frames = std::atoi(argv[++i]);
  }
  return args;
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parseArgs(argc, argv);

  obf2::FileSystem files;
  std::vector<std::string> mountErrors;
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += obf2::mountArchivesFromCon(files, args.modDir, args.modDir / list, &mountErrors);
  }
  files.mountDirectory(args.modDir);

  std::printf("OpenBattlefield2 | %s/%s\n", OBF2_PLATFORM_NAME, OBF2_ARCH_NAME);
  std::printf("мод: %s | архівів: %d | точок монтування: %zu\n", args.modDir.string().c_str(),
              mounted, files.mountCount());
  for (const auto& e : mountErrors) std::printf("  [mount] %s\n", e.c_str());
  if (mounted == 0) {
    std::puts("  увага: дані гри не змонтовано — вкажи --mod <шлях до mods/bf2>");
  }

  obf2::gfx::WindowDesc desc;
  desc.title = "OpenBattlefield2";
  std::string error;
  auto device = obf2::gfx::Device::create(desc, &error);
  if (!device) {
    std::fprintf(stderr, "не вдалося створити пристрій: %s\n", error.c_str());
    return 1;
  }
  std::printf("GPU-бекенд: %s\n", std::string(device->driver()).c_str());

  int frame = 0;
  while (device->pumpEvents()) {
    if (auto f = device->beginFrame()) {
      // Тимчасовий фон, доки нема рендера сцени.
      device->endFrame(*f, obf2::gfx::Color{0.05f, 0.07f, 0.09f, 1.0f});
    }
    if (args.frames > 0 && ++frame >= args.frames) break;
  }

  std::printf("кадрів намальовано: %d\n", frame);
  return 0;
}
