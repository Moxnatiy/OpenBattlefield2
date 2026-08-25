// object_info — реєстр ObjectTemplate на справжніх даних гри.
//
//   object_info <modDir> --all          — зібрати все й показати статистику
//   object_info <modDir> <ім'я>          — показати один шаблон
//   object_info <modDir> --tree <ім'я>   — шаблон з ієрархією нащадків

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "obf2/core/path.h"
#include "obf2/game/object_template.h"
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

// Проганяє всі .con/.tweak гри через інтерпретатор, згодовуючи команди реєстру.
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

void printTemplate(const obf2::game::ObjectTemplate& object, const obf2::game::Registry& registry,
                   int depth, int maxDepth) {
  const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
  std::printf("%s%s (%s)\n", indent.c_str(), object.name.c_str(), object.className.c_str());
  if (depth == 0) {
    std::printf("%s  джерело: %s:%d\n", indent.c_str(), object.file.c_str(), object.line);
  }

  if (depth == 0) {
    std::printf("%s  властивостей: %zu\n", indent.c_str(), object.properties.size());
    std::vector<std::string> names;
    names.reserve(object.properties.size());
    for (const auto& [name, values] : object.properties) {
      names.push_back(name + " = " + (values.empty() ? std::string{} : [&] {
        std::string joined;
        for (const auto& argument : values.back().args) {
          if (!joined.empty()) joined += " ";
          joined += argument;
        }
        return joined;
      }()));
    }
    std::sort(names.begin(), names.end());
    for (std::size_t i = 0; i < names.size() && i < 14; ++i) {
      std::printf("%s    %s\n", indent.c_str(), names[i].c_str());
    }
    if (names.size() > 14) std::printf("%s    ... ще %zu\n", indent.c_str(), names.size() - 14);

    for (const auto& component : object.components) {
      std::printf("%s  компонент %s (%zu властивостей)\n", indent.c_str(), component.name.c_str(),
                  component.properties.size());
    }
  }

  for (const auto& child : object.children) {
    if (child.hasPosition) {
      std::printf("%s  -> %s @ %.4f/%.4f/%.4f\n", indent.c_str(), child.name.c_str(),
                  child.position.x, child.position.y, child.position.z);
    } else {
      std::printf("%s  -> %s\n", indent.c_str(), child.name.c_str());
    }
    if (depth + 1 < maxDepth) {
      if (const auto* resolved = registry.find(child.name)) {
        printTemplate(*resolved, registry, depth + 2, maxDepth);
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: object_info <modDir> <--all | --tree <ім'я> | <ім'я>>\n", stderr);
    return 2;
  }

  obf2::FileSystem files = mountGame(argv[1]);
  obf2::game::Registry registry;
  buildRegistry(files, registry);

  const std::string what = argv[2];
  const auto& stats = registry.stats();

  if (what == "--all") {
    std::printf("шаблонів: %zu\n", registry.size());
    std::printf("  create: %d, activeSafe на наявних: %d\n", stats.created, stats.reopened);
    std::printf("  компонентів: %d, прикріплень: %d\n", stats.componentsCreated,
                stats.childrenAdded);
    std::printf("  присвоєнь властивостей: %lld\n", stats.propertiesSet);
    std::printf("  команд без активного шаблону: %d\n", stats.orphanCommands);

    std::map<std::string, int> byClass;
    int withChildren = 0, withComponents = 0;
    for (const auto* object : registry.all()) {
      ++byClass[object->className];
      if (!object->children.empty()) ++withChildren;
      if (!object->components.empty()) ++withComponents;
    }

    std::printf("\nз нащадками: %d, з компонентами: %d\n", withChildren, withComponents);
    std::printf("\nкласів: %zu, топ-15:\n", byClass.size());
    std::vector<std::pair<std::string, int>> sorted(byClass.begin(), byClass.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sorted.size() && i < 15; ++i) {
      std::printf("  %-28s %d\n", sorted[i].first.c_str(), sorted[i].second);
    }
    return 0;
  }

  const bool tree = what == "--tree";
  const std::string name = tree ? (argc > 3 ? argv[3] : "") : what;
  const auto* object = registry.find(name);
  if (object == nullptr) {
    std::fprintf(stderr, "шаблон не знайдено: %s (усього в реєстрі %zu)\n", name.c_str(),
                 registry.size());
    return 1;
  }
  printTemplate(*object, registry, 0, tree ? 6 : 1);
  return 0;
}
