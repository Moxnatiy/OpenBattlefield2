// command_audit — що з мови .con ми вже вміємо, а що ні.
//
// Проганяє ВСІ .con і .tweak гри (включно з рівнями) через ті самі
// обробники, що й рушій, і показує:
//   * скільки команд виконано і скільки лишилося без обробника;
//   * які саме команди без обробника, з ПРИКЛАДОМ реальних аргументів
//     і файлом, де вони трапилися.
//
// Приклад аргументів важливіший за саму назву: він одразу показує, що
// команда очікує, і скільки роботи в ній насправді.
//
//   command_audit <modDir> [рівень]         — зведення
//   command_audit <modDir> --missing [N]    — перелік нереалізованого
//   command_audit <modDir> --target <ціль>  — усе про одну ціль

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "obf2/core/path.h"
#include "obf2/engine/control_map.h"
#include "obf2/engine/settings.h"
#include "obf2/game/object_template.h"
#include "obf2/hud/hud.h"
#include "obf2/level/level.h"
#include "obf2/vfs/filesystem.h"

namespace {

struct Sample {
  int count = 0;
  std::string arguments;  // приклад реальних аргументів
  std::string file;
  int line = 0;
};

std::string joinArguments(const obf2::con::Command& command) {
  std::string joined;
  for (const std::string& argument : command.args) {
    if (!joined.empty()) joined += " ";
    // Довгі шляхи вкорочуємо: у звіті важлива форма, а не вміст.
    joined += argument.size() > 40 ? argument.substr(0, 37) + "..." : argument;
  }
  return joined;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fputs("usage: command_audit <modDir> [--missing N | --target <ціль> | <рівень>]\n",
               stderr);
    return 2;
  }

  const std::filesystem::path modDir = argv[1];
  const std::string mode = argc > 2 ? argv[2] : "";

  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, modDir, modDir / list);
  }
  files.mountDirectory(modDir);

  // Рівень монтуємо теж: у ньому власний набір команд (терен, розстановка,
  // ігрова логіка), і без нього картина неповна.
  std::string levelName;
  if (!mode.empty() && mode.rfind("--", 0) != 0) levelName = mode;
  if (!levelName.empty()) obf2::level::mountLevel(files, modDir, levelName);

  // Ті самі обробники, що й у рушії.
  obf2::engine::Console console;
  obf2::engine::Settings settings;
  obf2::engine::ControlMap controls;
  settings.bind(console);
  controls.bind(console);

  obf2::game::Registry registry;
  obf2::hud::Builder hud;

  std::map<std::string, Sample> missing;
  std::map<std::string, long long> byTarget;
  long long total = 0;
  long long handled = 0;

  auto onCommand = [&](const obf2::con::Command& command) {
    ++total;
    if (!command.path.empty()) {
      std::string target = command.path.front();
      for (char& c : target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      ++byTarget[target];
    }

    // Порядок як у рушії: спершу спеціалізовані підсистеми, потім консоль.
    const bool isObjectTemplate =
        command.lowerPath.rfind("objecttemplate.", 0) == 0 || command.lowerPath == "objecttemplate";
    const bool isHud = command.lowerPath.rfind("hudbuilder.", 0) == 0;

    if (isObjectTemplate) {
      registry.feed(command);
      ++handled;
      return;
    }
    if (isHud) {
      hud.feed(command);
      ++handled;
      return;
    }
    if (console.execute(command)) {
      ++handled;
      return;
    }

    Sample& sample = missing[command.lowerPath];
    ++sample.count;
    if (sample.arguments.empty()) {
      sample.arguments = joinArguments(command);
      sample.file = command.file;
      sample.line = command.line;
    }
  };

  std::vector<std::string> configs;
  for (auto& path : files.list()) {
    const std::string_view extension = obf2::assetExtension(path);
    if (extension == "con" || extension == "tweak") configs.push_back(std::move(path));
  }
  std::sort(configs.begin(), configs.end());
  configs.erase(std::unique(configs.begin(), configs.end()), configs.end());

  const std::vector<std::string> editorArgs{"BF2Editor"};
  for (const auto& path : configs) {
    obf2::con::Interpreter interpreter(files, onCommand);
    interpreter.runFile(path, editorArgs);
  }

  std::printf("файлів: %zu\nкоманд виконано: %lld з %lld (%.1f%%)\n", configs.size(), handled,
              total, total == 0 ? 0.0 : 100.0 * static_cast<double>(handled) / static_cast<double>(total));
  std::printf("унікальних без обробника: %zu\n", missing.size());

  std::vector<std::pair<std::string, Sample>> sorted(missing.begin(), missing.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second.count > b.second.count; });

  if (mode == "--target" && argc > 3) {
    std::string wanted = argv[3];
    for (char& c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::printf("\nбез обробника у цілі \"%s\":\n", wanted.c_str());
    for (const auto& [name, sample] : sorted) {
      if (name.rfind(wanted + ".", 0) != 0) continue;
      std::printf("  %-44s x%-6d %s\n", name.c_str(), sample.count, sample.arguments.c_str());
    }
    return 0;
  }

  int limit = 40;
  if (mode == "--missing" && argc > 3) limit = std::atoi(argv[3]);

  std::printf("\nнереалізовані команди (топ-%d), з прикладом аргументів:\n", limit);
  int shown = 0;
  for (const auto& [name, sample] : sorted) {
    if (shown++ >= limit) break;
    std::printf("  %-42s x%-7d %s\n", name.c_str(), sample.count, sample.arguments.c_str());
    std::printf("  %-42s %s:%d\n", "", sample.file.c_str(), sample.line);
  }

  std::puts("\nцілі команд (топ-12):");
  std::vector<std::pair<std::string, long long>> targets(byTarget.begin(), byTarget.end());
  std::sort(targets.begin(), targets.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  for (std::size_t i = 0; i < targets.size() && i < 12; ++i) {
    std::printf("  %-24s %lld\n", targets[i].first.c_str(), targets[i].second);
  }
  return 0;
}
