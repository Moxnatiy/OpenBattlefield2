// kit_info — the spawn screen's seven kit rows, as the game's own data gives them.
//
//   kit_info <modDir> <level> [team]
//
// The level's `Init.con` names the kits (`gameLogic.setKit <team> <row> <kit>
// <soldier>`), and every picture in a row comes out of that kit's ObjectTemplate.
// This prints, per row, exactly the values the engine pours into the HUD's
// variables — so the output can be laid beside a frame dump of the original
// named by `tools/hud_atlas.py` and compared name by name.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "obf2/con/interpreter.h"
#include "obf2/core/path.h"
#include "obf2/game/object_template.h"
#include "obf2/hud/kit_list.h"
#include "obf2/level/level.h"
#include "obf2/vfs/filesystem.h"

namespace {

obf2::FileSystem mountGame(const std::filesystem::path& modDir) {
  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, modDir, modDir / list);
  }
  files.mountDirectory(modDir);
  return files;
}

void buildRegistry(obf2::FileSystem& files, obf2::game::Registry& registry) {
  std::vector<std::string> paths;
  for (auto& path : files.list()) {
    const std::string_view extension = obf2::assetExtension(path);
    if (extension == "con" || extension == "tweak") paths.push_back(std::move(path));
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  for (const auto& path : paths) {
    obf2::con::Interpreter interpreter(
        files, [&](const obf2::con::Command& command) { registry.feed(command); });
    interpreter.runFile(path);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: kit_info <modDir> <level> [team]\n", stderr);
    return 2;
  }
  const std::string levelName = argv[2];
  const int onlyTeam = argc > 3 ? std::atoi(argv[3]) : 0;

  obf2::FileSystem files = mountGame(argv[1]);
  std::string error;
  if (!obf2::level::mountLevel(files, argv[1], levelName, &error)) {
    std::fprintf(stderr, "level not mounted: %s\n", error.c_str());
    return 1;
  }
  const auto loaded = obf2::level::loadLevel(files, levelName, &error);
  if (!loaded) {
    std::fprintf(stderr, "level not loaded: %s\n", error.c_str());
    return 1;
  }
  const obf2::level::Level& level = *loaded;

  obf2::game::Registry registry;
  buildRegistry(files, registry);
  std::printf("%s: %zu templates\n", levelName.c_str(), registry.size());

  for (int team = 1; team <= 2; ++team) {
    if (onlyTeam != 0 && team != onlyTeam) continue;
    std::printf("\nteam %d (%s)\n", team, level.teamNames[team].c_str());
    for (int slot = 0; slot < obf2::level::Level::kKitsPerTeam; ++slot) {
      const std::string& name = level.kits[team][slot];
      if (name.empty()) {
        std::printf("  %d  <the level names no kit for this row>\n", slot);
        continue;
      }
      const obf2::hud::KitRow row = obf2::hud::buildKitRow(registry, name);
      std::printf("  %d  %-14s %s\n", slot, row.kitTemplate.c_str(), row.nameKey.c_str());
      std::printf("        icon      %s\n", row.icon.c_str());
      std::printf("        weapon    %s\n", row.weaponIcon.c_str());
      std::printf("        unlock    %s%s\n", row.altWeaponIcon.c_str(),
                  row.unlock ? "" : "   (the kit carries no unlock)");
      std::printf("        sprint    %.3f\n", static_cast<double>(row.sprintAbility));
      for (std::size_t i = 0; i < row.abilityIcons.size(); ++i) {
        std::printf("        ability%zu  %s\n", i, row.abilityIcons[i].c_str());
      }
    }
  }
  return 0;
}
