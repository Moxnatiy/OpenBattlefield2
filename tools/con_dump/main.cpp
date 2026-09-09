// con_dump — a check of the .con/.tweak interpreter on the game's real data.
//
//   con_dump <modDir> <file.con>   — run one file and print the commands
//   con_dump <modDir> --all        — run EVERY .con/.tweak from the archives and directory
//
// It unpacks nothing: the archives (Objects_server.zip and so on) are read in
// place, and the archive list comes from the game's own ServerArchives.con.

#include <algorithm>
#include <cctype>
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

  // The archives first, then the mod directory — so loose files override archives.
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += obf2::mountArchivesFromCon(fs, modDir, modDir / list, &mountErrors);
  }
  fs.mountDirectory(modDir);

  std::printf("platform: %s/%s | archives mounted: %d | mount points: %zu\n",
              OBF2_PLATFORM_NAME, OBF2_ARCH_NAME, mounted, fs.mountCount());
  for (const auto& e : mountErrors) std::printf("  [mount] %s\n", e.c_str());

  if (what != "--all" && what != "--commands") {
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
    std::printf("\ncommands: %lld, errors: %d, warnings: %d\n", commands, interp.errorCount(),
                interp.warningCount());
    return ok && interp.errorCount() == 0 ? 0 : 1;
  }

  // The --commands mode: the frequency of one target's commands. Needed to build
  // the registry from the real data rather than from assumptions.
  if (what == "--commands") {
    const std::string target = argc > 3 ? argv[3] : "objecttemplate";
    std::map<std::string, long long> byCommand;
    std::map<std::string, long long> byComponent;

    std::vector<std::string> scan;
    for (auto& path : fs.list()) {
      const std::string_view ext = obf2::assetExtension(path);
      if (ext == "con" || ext == "tweak") scan.push_back(std::move(path));
    }
    std::sort(scan.begin(), scan.end());
    scan.erase(std::unique(scan.begin(), scan.end()), scan.end());

    for (const auto& file : scan) {
      obf2::con::Interpreter interp(fs, [&](const obf2::con::Command& cmd) {
        if (cmd.path.empty()) return;
        std::string head = cmd.path.front();
        for (char& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (head != target) return;
        ++byCommand[cmd.lowerPath];
        // Two-part paths (ObjectTemplate.fire.x) are a reference to a component.
        if (cmd.path.size() == 3) {
          std::string component = cmd.path[1];
          for (char& c : component) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          ++byComponent[component];
        }
      });
      interp.runFile(file);
    }

    std::vector<std::pair<std::string, long long>> sorted(byCommand.begin(), byCommand.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::printf("commands %s: %zu distinct\n\ntop 25:\n", target.c_str(), sorted.size());
    for (std::size_t i = 0; i < sorted.size() && i < 25; ++i) {
      std::printf("  %-46s %lld\n", sorted[i].first.c_str(), sorted[i].second);
    }

    std::vector<std::pair<std::string, long long>> components(byComponent.begin(), byComponent.end());
    std::sort(components.begin(), components.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    std::printf("\nsub-objects (%zu distinct), top 15:\n", components.size());
    for (std::size_t i = 0; i < components.size() && i < 15; ++i) {
      std::printf("  %-24s %lld\n", components[i].first.c_str(), components[i].second);
    }
    return 0;
  }

  // The --all mode: a regression run over the whole corpus.
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
  std::map<std::string, int> missing;  // the distinct paths that were not found

  for (const auto& file : files) {
    obf2::con::Interpreter interp(
        fs,
        [&](const obf2::con::Command& cmd) {
          ++commands;
          ++byTarget[std::string(cmd.path.front())];
        },
        [&](const obf2::con::Diagnostic& d) {
          if (d.isError()) ++errors; else ++warnings;
          // A key without the concrete path, so that alike problems group together.
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

  std::printf("\nfiles: %zu\ncommands: %lld\nerrors: %d\nwarnings: %d\n", files.size(),
              commands, errors, warnings);
  std::puts("\nthe commonest command targets:");
  std::vector<std::pair<std::string, long long>> sorted(byTarget.begin(), byTarget.end());
  std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
  for (std::size_t i = 0; i < sorted.size() && i < 12; ++i) {
    std::printf("  %-24s %lld\n", sorted[i].first.c_str(), sorted[i].second);
  }
  if (!errorKinds.empty()) {
    std::puts("\ndiagnostic kinds:");
    for (const auto& [kind, count] : errorKinds) std::printf("  %-40s %d\n", kind.c_str(), count);
  }
  if (!missing.empty()) {
    std::printf("\nmissing include/run — the same in the original (%zu distinct, first 5):\n",
                missing.size());
    int shown = 0;
    for (const auto& [path, count] : missing) {
      if (shown++ >= 5) break;
      std::printf("  %s  (x%d)\n", path.c_str(), count);
    }
  }
  return 0;
}
