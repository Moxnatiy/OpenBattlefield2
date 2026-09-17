#pragma once
// A font as it lies in the game's archives: a pair of `.dif` (the metrics) and
// `.dds` (the atlas) under `Fonts/`. The engine reads them in
// `Code/BF2/Menu/GameMenu/DifFont.cpp`.
#include <string>

#include "obf2/font/dif_font.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::font {

struct LoadedFont {
  Font font;
  std::string atlasPath;
  bool valid = false;
};

// `base` is the path without an extension, e.g. "Fonts/800/dynamicText_13".
// A font whose metrics do not read comes back with `valid` false and says why on
// stderr; the caller then draws without it rather than stopping.
LoadedFont loadFont(FileSystem& files, const std::string& base);

}  // namespace obf2::font
