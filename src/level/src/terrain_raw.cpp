#include "obf2/level/terrain_raw.h"

#include <cstring>

namespace obf2::level {
namespace {

// A walk over the file that refuses to read past its end. The blob comes out of
// the user's own archives, so every read is bounds-checked and a short file
// fails rather than reading rubbish.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

  std::uint32_t u32() {
    std::uint32_t value = 0;
    read(&value, sizeof(value), "a 32-bit field");
    return value;
  }

  std::uint8_t u8() {
    std::uint8_t value = 0;
    read(&value, sizeof(value), "a byte");
    return value;
  }

  float f32() {
    float value = 0.0f;
    read(&value, sizeof(value), "a float");
    return value;
  }

  Vec3f vec3() {
    const float x = f32(), y = f32(), z = f32();
    return Vec3f{x, y, z};
  }

  // Every string in this file ends with a newline and carries no length.
  std::string string() {
    std::string out;
    while (ok_ && at_ < bytes_.size()) {
      const char c = static_cast<char>(bytes_[at_++]);
      if (c == '\n') return out;
      out.push_back(c);
      if (out.size() > 1024) break;
    }
    fail("a string that never ends");
    return out;
  }

 private:
  void read(void* into, std::size_t size, const char* what) {
    if (!ok_) return;
    if (at_ + size > bytes_.size()) { fail(what); return; }
    std::memcpy(into, bytes_.data() + at_, size);
    at_ += size;
  }

  void fail(const char* what) {
    if (ok_) error_ = std::string("terraindata.raw ends inside ") + what;
    ok_ = false;
  }

  std::span<const std::byte> bytes_;
  std::size_t at_ = 0;
  bool ok_ = true;
  std::string error_;
};

}  // namespace

std::optional<TerrainRaw> readTerrainRaw(std::span<const std::byte> bytes, std::string* error) {
  Reader r(bytes);
  TerrainRaw out;

  out.version = r.u32();
  out.primaryWorldScale = r.vec3();
  out.secondaryWorldScale = r.vec3();
  // A float the writer never initialises — it is 0xcdcdcdcd, MSVC's debug fill,
  // on every level of the game, written out as it lay in memory.
  r.u32();
  out.highestHeight = r.f32();
  out.lowestHeight = r.f32();
  out.patchSize = static_cast<int>(r.u32());
  out.subdividePatches = r.u8() != 0;
  out.patchesPerSide = static_cast<int>(r.u32());
  out.patchColormapSize = static_cast<int>(r.u32());
  out.lowDetailmapSize = static_cast<int>(r.u32());
  out.colormapBase = r.string();
  out.detailmapBase = r.string();
  out.lowDetailmapBase = r.string();
  out.lightmapBase = r.string();
  out.farSideTiling[0] = r.f32();
  out.farSideTiling[1] = r.f32();
  out.farTopTilingHi = r.f32();
  out.farTopTilingLow = r.f32();
  out.farYOffset = r.f32();
  out.sunColor = r.vec3();
  out.giColor = r.vec3();
  out.waterColor = r.vec3();

  const std::uint32_t count = r.u32();
  // Six on every level of the game, and the shader is built for six
  // (`vComponentsel` is a vec3 against two chart maps). A wilder number means
  // the walk has gone wrong, so it is refused rather than trusted.
  if (r.ok() && count > 64) {
    if (error) *error = "terraindata.raw claims an implausible number of terrain materials";
    return std::nullopt;
  }
  for (std::uint32_t i = 0; r.ok() && i < count; ++i) {
    // The order here is the file's, which is not the material's own order: the
    // pair goes to +0x1c/+0x20 and the single float that follows it to +0x18
    // (`RendDX9.dll`, 0x100ddc94).
    TerrainMaterial material;
    material.texture = r.string();
    material.triPlanar = r.u8() != 0;
    material.sideTilingX = r.f32();
    material.sideTilingY = r.f32();
    material.topTiling = r.f32();
    material.yOffset = r.f32();
    material.envMap = r.u8() != 0;
    out.materials.push_back(std::move(material));
  }

  if (!r.ok()) {
    if (error) *error = r.error();
    return std::nullopt;
  }
  return out;
}

std::optional<TerrainRaw> loadTerrainRaw(const FileSystem& files, std::string_view levelName,
                                         std::string* error) {
  const std::string path = "Levels/" + std::string(levelName) + "/terraindata.raw";
  const auto bytes = files.read(path);
  if (!bytes) {
    if (error) *error = "no " + path;
    return std::nullopt;
  }
  return readTerrainRaw(*bytes, error);
}

}  // namespace obf2::level
