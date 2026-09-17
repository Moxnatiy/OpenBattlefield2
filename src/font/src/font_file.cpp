#include "obf2/font/font_file.h"

#include <cstdio>

namespace obf2::font {

LoadedFont loadFont(FileSystem& files, const std::string& base) {
  LoadedFont out;
  const auto metrics = files.read(base + ".dif");
  if (!metrics) return out;

  const std::string text(reinterpret_cast<const char*>(metrics->data()), metrics->size());
  std::string error;
  auto parsed = parseDif(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "font %s: %s\n", base.c_str(), error.c_str());
    return out;
  }

  out.font = std::move(*parsed);
  out.atlasPath = base + ".dds";
  out.valid = files.exists(out.atlasPath);
  if (!out.valid) std::fprintf(stderr, "no font atlas: %s\n", out.atlasPath.c_str());
  return out;
}

}  // namespace obf2::font
