// texture_info — регресія парсера DDS на справжніх даних.
//
//   texture_info <modDir> --all
//   texture_info <modDir> <шлях/у/vfs.dds>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "obf2/core/path.h"
#include "obf2/texture/dds.h"
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: texture_info <modDir> <--all | path.dds>\n", stderr);
    return 2;
  }
  obf2::FileSystem files = mountGame(argv[1]);
  const std::string what = argv[2];

  if (what != "--all") {
    const std::string path = obf2::normalizeAssetPath(what);
    const auto bytes = files.read(path);
    if (!bytes) { std::fprintf(stderr, "не знайдено: %s\n", path.c_str()); return 1; }

    std::string error;
    const auto texture = obf2::texture::loadDds(*bytes, &error);
    if (!texture) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str()); return 1; }

    std::printf("%s\n  %ux%u, %s, рівнів %zu, %zu байт даних\n", path.c_str(), texture->width,
                texture->height, std::string(obf2::texture::formatName(texture->format)).c_str(),
                texture->mips.size(), texture->data.size());
    for (std::size_t i = 0; i < texture->mips.size() && i < 4; ++i) {
      const auto& mip = texture->mips[i];
      std::printf("    mip %zu: %ux%u, %zu байт\n", i, mip.width, mip.height, mip.size);
    }
    return 0;
  }

  std::map<std::string, int> byFormat;
  std::map<std::string, int> failures;
  std::vector<std::string> examples;
  long long totalBytes = 0;
  int withMips = 0;

  auto paths = files.list();
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

  for (const auto& path : paths) {
    if (obf2::assetExtension(path) != "dds") continue;
    const auto bytes = files.read(path);
    if (!bytes) continue;

    std::string error;
    const auto texture = obf2::texture::loadDds(*bytes, &error);
    if (!texture) {
      ++failures[error];
      if (examples.size() < 5) examples.push_back(path + ": " + error);
      continue;
    }
    ++byFormat[std::string(obf2::texture::formatName(texture->format))];
    totalBytes += static_cast<long long>(texture->data.size());
    if (texture->mips.size() > 1) ++withMips;
  }

  std::puts("розібрано:");
  int parsed = 0;
  for (const auto& [format, count] : byFormat) {
    std::printf("  %-14s %d\n", format.c_str(), count);
    parsed += count;
  }
  std::printf("  разом %d, з мапами %d, %lld МБ пікселів\n", parsed, withMips,
              totalBytes / (1024 * 1024));

  if (!failures.empty()) {
    std::puts("не розібрано:");
    for (const auto& [reason, count] : failures) std::printf("  %-44s %d\n", reason.c_str(), count);
    for (const auto& example : examples) std::printf("  %s\n", example.c_str());
  }
  return failures.empty() ? 0 : 1;
}
