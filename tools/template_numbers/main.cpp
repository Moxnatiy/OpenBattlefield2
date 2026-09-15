// template_numbers — the server's object template numbers, as obf2::game::TemplateNumbers
// reproduces them from the mod's archives (obf2/game/template_numbers.h).
//
//   template_numbers <modDir>              every number and name
//   template_numbers <modDir> <name>...    only those names
//
// The same list tools/template_order.py prints: the two have to agree line by
// line, and both have to agree with the pairs measured on a live server.
#include <cstdio>
#include <string>
#include <vector>

#include "obf2/game/template_numbers.h"
#include "obf2/vfs/filesystem.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: template_numbers <modDir> [name...]\n");
    return 1;
  }
  const std::filesystem::path modDir = argv[1];
  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, modDir, modDir / list);
  }
  files.mountDirectory(modDir);

  const auto listed = files.read("ServerArchives.con");
  if (!listed) {
    std::fprintf(stderr, "no ServerArchives.con in %s\n", argv[1]);
    return 1;
  }
  const std::string text(reinterpret_cast<const char*>(listed->data()), listed->size());
  std::vector<std::vector<std::string>> archives;
  for (const auto& archive : obf2::game::archivesFromCon(text)) {
    archives.push_back(files.archiveEntries(archive));
  }
  const auto numbers = obf2::game::TemplateNumbers::build(files, archives);

  if (argc > 2) {
    for (int i = 2; i < argc; ++i) {
      const auto number = numbers.numberOf(argv[i]);
      std::printf("%s %d\n", argv[i], number ? static_cast<int>(*number) : -1);
    }
    return 0;
  }
  for (std::uint32_t n = 0; n < numbers.size(); ++n) std::printf("%6u  %s\n", n, numbers.nameOf(n)->c_str());
  return 0;
}
