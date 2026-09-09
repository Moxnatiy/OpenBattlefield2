#pragma once
#include <string>
#include <string_view>

namespace obf2 {

// "Ingame\Weapons\Icons\Hud\icon.tga" -> "ingame/weapons/icons/hud/icon.tga"
// Backslashes -> forward, lower case, collapsed and trimmed slashes, "."
// segments removed and ".." expanded. This is the canonical key we look a
// file up by, both in a zip archive and on disk.
std::string normalizeAssetPath(std::string_view raw);

// Joins two asset paths and normalises the result.
std::string joinAssetPath(std::string_view base, std::string_view rel);

// The directory a path lives in ("a/b/c.con" -> "a/b"). No trailing slash.
std::string_view assetParentDir(std::string_view normalized);

// The extension without the dot ("a/b.tweak" -> "tweak"). Expects an already
// normalised path, so it leaves case alone and returns a view into the buffer.
std::string_view assetExtension(std::string_view normalized);

}  // namespace obf2
