#include "obf2/level/lightmap_atlas.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace obf2::level {
namespace {

std::string lowerCase(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// The key an entry is filed under, and the key a placement asks for: the two
// have to be built the same way or nothing ever matches.
std::string keyFor(std::string_view templateName, Vec3f position) {
  // Truncation towards zero, not rounding — see the header for the measurement.
  return lowerCase(templateName) + "=00=" + std::to_string(static_cast<int>(position.x)) + "=" +
         std::to_string(static_cast<int>(position.y)) + "=" +
         std::to_string(static_cast<int>(position.z));
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

}  // namespace

ObjectLightmaps ObjectLightmaps::parse(std::string_view text, std::string_view levelName) {
  ObjectLightmaps out;
  out.levelName_ = std::string(levelName);

  std::size_t at = 0;
  while (at <= text.size()) {
    const std::size_t end = text.find('\n', at);
    const std::string_view line = trim(text.substr(at, end == std::string_view::npos ? end : end - at));
    at = end == std::string_view::npos ? text.size() + 1 : end + 1;
    if (line.empty() || line.front() == '#') continue;

    // `<path>.dds` <tabs> `<atlas>.dds, <idx>, <woffset>, <hoffset>, <width>, <height>`
    const std::size_t tab = line.find('\t');
    if (tab == std::string_view::npos) continue;
    std::string_view name = trim(line.substr(0, tab));
    std::string_view rest = trim(line.substr(tab));

    // The name is a path; only its last part is the key, and the `.dds` goes.
    const std::size_t slash = name.find_last_of('/');
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    if (name.size() > 4 && lowerCase(name.substr(name.size() - 4)) == ".dds") {
      name.remove_suffix(4);
    }
    if (name.empty()) continue;

    // Six comma-separated fields, of which the first is the atlas's own path and
    // is redundant with its index.
    std::vector<std::string> fields;
    std::size_t field = 0;
    while (field <= rest.size()) {
      const std::size_t comma = rest.find(',', field);
      fields.emplace_back(
          trim(rest.substr(field, comma == std::string_view::npos ? comma : comma - field)));
      if (comma == std::string_view::npos) break;
      field = comma + 1;
    }
    if (fields.size() < 6) continue;

    LightmapPlacement placement;
    placement.atlas = std::atoi(fields[1].c_str());
    placement.offsetU = std::strtof(fields[2].c_str(), nullptr);
    placement.offsetV = std::strtof(fields[3].c_str(), nullptr);
    placement.scaleU = std::strtof(fields[4].c_str(), nullptr);
    placement.scaleV = std::strtof(fields[5].c_str(), nullptr);
    if (placement.atlas < 0 || placement.scaleU <= 0.0f || placement.scaleV <= 0.0f) continue;

    out.atlasCount_ = std::max(out.atlasCount_, placement.atlas + 1);
    out.byKey_.emplace(lowerCase(name), placement);
  }
  return out;
}

ObjectLightmaps ObjectLightmaps::load(FileSystem& files, std::string_view levelName) {
  const std::string path =
      "Levels/" + std::string(levelName) + "/lightmaps/Objects/LightmapAtlas.tai";
  const auto bytes = files.read(path);
  if (!bytes) return ObjectLightmaps{};
  const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  return parse(text, levelName);
}

const LightmapPlacement* ObjectLightmaps::find(std::string_view templateName,
                                               Vec3f position) const {
  const auto found = byKey_.find(keyFor(templateName, position));
  return found == byKey_.end() ? nullptr : &found->second;
}

std::string ObjectLightmaps::atlasPath(int index) const {
  return "Levels/" + levelName_ + "/lightmaps/Objects/LightmapAtlas" + std::to_string(index) +
         ".dds";
}

}  // namespace obf2::level
