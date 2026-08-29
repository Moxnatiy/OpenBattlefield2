// hud_dump — розбір інтерфейсу гри з файлів HUD/.
//
//   hud_dump <modDir>            — які групи є і скільки в них вузлів
//   hud_dump <modDir> <група>    — вузли однієї групи

#include <cstdio>
#include <algorithm>
#include <map>
#include <vector>
#include <string>

#include "obf2/hud/hud.h"
#include "obf2/vfs/filesystem.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fputs("usage: hud_dump <modDir> [група]\n", stderr);
    return 2;
  }

  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, argv[1], std::filesystem::path(argv[1]) / list);
  }
  files.mountDirectory(argv[1]);

  obf2::hud::Builder builder;
  obf2::con::Interpreter interpreter(
      files, [&](const obf2::con::Command& command) { builder.feed(command); });
  interpreter.runFile("Menu/HUD/HudSetup/HudSetupMain.con");
  builder.finish();

  std::printf("вузлів: %zu, невідомих команд: %lld\n", builder.nodes().size(),
              builder.unknownCommands());
  if (argc > 2 && std::string(argv[2]) == "--unknown") {
    std::puts("\nбез обробника (топ-30):");
    std::vector<std::pair<std::string, int>> sorted(builder.unknownByName().begin(),
                                                    builder.unknownByName().end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sorted.size() && i < 30; ++i) {
      std::printf("  %-44s %d\n", sorted[i].first.c_str(), sorted[i].second);
    }
    return 0;
  }

  if (argc < 3) {
    std::puts("\nгрупи:");
    std::map<std::string, int> counts;
    for (const auto& node : builder.nodes()) ++counts[node.group];
    for (const auto& [name, count] : counts) std::printf("  %-28s %d\n", name.c_str(), count);
    return 0;
  }

  for (const auto* node : builder.group(argv[2])) {
    // Показуємо і власні координати (відносні до батька), і зведені —
    // саме за розбіжністю видно, як глибоко вузол сидить у дереві.
    std::printf("  %-10s %-30s віднос %6.0f %6.0f  абс %6.0f %6.0f  %5.0fx%-5.0f %s",
                std::string(obf2::hud::nodeTypeName(node->type)).c_str(), node->name.c_str(),
                node->x, node->y, node->absX, node->absY, node->width, node->height,
                node->area.c_str());
    if (!node->texture.empty()) std::printf("  tex=%s", node->texture.c_str());
    if (!node->text.empty()) std::printf("  text=\"%s\"", node->text.c_str());
    if (!node->command.empty()) std::printf("  cmd=%s", node->command.c_str());
    std::putchar('\n');
  }
  return 0;
}
