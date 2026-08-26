// hud_dump — розбір інтерфейсу гри з файлів HUD/.
//
//   hud_dump <modDir>            — які групи є і скільки в них вузлів
//   hud_dump <modDir> <група>    — вузли однієї групи

#include <cstdio>
#include <map>
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

  std::printf("вузлів: %zu, невідомих команд: %lld\n", builder.nodes().size(),
              builder.unknownCommands());

  if (argc < 3) {
    std::puts("\nгрупи:");
    std::map<std::string, int> counts;
    for (const auto& node : builder.nodes()) ++counts[node.group];
    for (const auto& [name, count] : counts) std::printf("  %-28s %d\n", name.c_str(), count);
    return 0;
  }

  for (const auto* node : builder.group(argv[2])) {
    std::printf("  %-10s %-32s %6.0f %6.0f %5.0f %5.0f", 
                std::string(obf2::hud::nodeTypeName(node->type)).c_str(), node->name.c_str(),
                node->x, node->y, node->width, node->height);
    if (!node->texture.empty()) std::printf("  tex=%s", node->texture.c_str());
    if (!node->text.empty()) std::printf("  text=\"%s\"", node->text.c_str());
    if (!node->command.empty()) std::printf("  cmd=%s", node->command.c_str());
    std::putchar('\n');
  }
  return 0;
}
