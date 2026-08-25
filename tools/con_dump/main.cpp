// con_dump — перевірка інтерпретатора .con/.tweak на справжніх даних гри.
//
//   con_dump <modDir> <file.con>   — виконати один файл і роздрукувати команди
//   con_dump <modDir> --all        — прогнати ВСІ .con/.tweak з архівів і теки
//
// Нічого не розпаковує: архіви (Objects_server.zip і т.д.) читаються на місці,
// список архівів береться з ServerArchives.con самої гри.

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "obf2/con/interpreter.h"
#include "obf2/core/path.h"
#include "obf2/core/platform.h"
#include "obf2/vfs/filesystem.h"

namespace {

int usage() {
  std::fputs("usage: con_dump <modDir> <file.con|--all>\n", stderr);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return usage();

  const std::filesystem::path modDir = argv[1];
  const std::string what = argv[2];

  obf2::FileSystem fs;
  std::vector<std::string> mountErrors;

  // Спершу архіви, потім тека моду — щоб вільні файли перекривали архіви.
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += obf2::mountArchivesFromCon(fs, modDir, modDir / list, &mountErrors);
  }
  fs.mountDirectory(modDir);

  std::printf("платформа: %s/%s | змонтовано архівів: %d | точок монтування: %zu\n",
              OBF2_PLATFORM_NAME, OBF2_ARCH_NAME, mounted, fs.mountCount());
  for (const auto& e : mountErrors) std::printf("  [mount] %s\n", e.c_str());

  if (what != "--all") {
    long long commands = 0;
    obf2::con::Interpreter interp(
        fs,
        [&](const obf2::con::Command& cmd) {
          ++commands;
          std::printf("%s:%d  %s", cmd.file.c_str(), cmd.line, cmd.lowerPath.c_str());
          for (const auto& a : cmd.args) std::printf(" [%s]", a.c_str());
          std::putchar('\n');
        },
        [](const obf2::con::Diagnostic& d) {
          std::fprintf(stderr, "  %s %s:%d: %s\n", d.isError() ? "!!" : "..", d.file.c_str(),
                       d.line, d.message.c_str());
        });

    const bool ok = interp.runFile(what);
    std::printf("\nкоманд: %lld, помилок: %d, попереджень: %d\n", commands, interp.errorCount(),
                interp.warningCount());
    return ok && interp.errorCount() == 0 ? 0 : 1;
  }

  // Режим --all: регресійний прогін по всьому корпусу.
  std::vector<std::string> files;
  for (auto& path : fs.list()) {
    const std::string_view ext = obf2::assetExtension(path);
    if (ext == "con" || ext == "tweak") files.push_back(std::move(path));
  }
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());

  long long commands = 0;
  int errors = 0;
  int warnings = 0;
  std::map<std::string, long long> byTarget;
  std::map<std::string, int> errorKinds;
  std::map<std::string, int> missing;  // унікальні ненайдені шляхи

  for (const auto& file : files) {
    obf2::con::Interpreter interp(
        fs,
        [&](const obf2::con::Command& cmd) {
          ++commands;
          ++byTarget[std::string(cmd.path.front())];
        },
        [&](const obf2::con::Diagnostic& d) {
          if (d.isError()) ++errors; else ++warnings;
          // Ключ без конкретного шляху, щоб згрупувати однотипні проблеми.
          const std::size_t dash = d.message.find(" — ");
          if (dash == std::string::npos) {
            ++errorKinds[d.message];
          } else {
            ++errorKinds[d.message.substr(0, dash)];
            ++missing[d.message.substr(dash + std::string(" — ").size())];
          }
        });
    interp.runFile(file);
  }

  std::printf("\nфайлів: %zu\nкоманд: %lld\nпомилок: %d\nпопереджень: %d\n", files.size(),
              commands, errors, warnings);
  std::puts("\nнайчастіші цілі команд:");
  std::vector<std::pair<std::string, long long>> sorted(byTarget.begin(), byTarget.end());
  std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
  for (std::size_t i = 0; i < sorted.size() && i < 12; ++i) {
    std::printf("  %-24s %lld\n", sorted[i].first.c_str(), sorted[i].second);
  }
  if (!errorKinds.empty()) {
    std::puts("\nтипи діагностик:");
    for (const auto& [kind, count] : errorKinds) std::printf("  %-40s %d\n", kind.c_str(), count);
  }
  if (!missing.empty()) {
    std::printf("\nвідсутні include/run — так і в оригіналі (%zu унікальних, перші 5):\n",
                missing.size());
    int shown = 0;
    for (const auto& [path, count] : missing) {
      if (shown++ >= 5) break;
      std::printf("  %s  (x%d)\n", path.c_str(), count);
    }
  }
  return 0;
}
