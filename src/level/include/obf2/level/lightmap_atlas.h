#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "obf2/core/math.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::level {

// Where one placed object's baked light map sits inside the level's atlas.
//
// The shader wants it as `LightMapOffset` and uses it like this
// (`Shaders_client.zip:RaShaderSTM.fx:216`):
//
//   Out.Interpolated[__TEXLMAP_INTER].xy =
//       indata.TexSets[TexLightMapInd].xy * TexUnpack * LightMapOffset.xy
//       + LightMapOffset.zw;
//
// so `scale` is the xy and `offset` the zw.
struct LightmapPlacement {
  int atlas = -1;  // which `LightmapAtlas<N>.dds`
  float scaleU = 1.0f;
  float scaleV = 1.0f;
  float offsetU = 0.0f;
  float offsetV = 0.0f;
};

// A level's object light maps: `Levels/<name>/lightmaps/Objects/LightmapAtlas.tai`.
//
// The file is plain text and documents its own format in its header:
//
//   # <filename>		<atlas filename>, <atlas idx>, <woffset>, <hoffset>, <width>, <height>
//
// and the left-hand name is what ties an entry to a placed object:
//
//   levels/strike_at_karkand/lightmaps/objects/house_high_06=00=-226=166=59.dds
//
// — the template's name, then two digits, then the object's world position with
// the fraction **cut off**, not rounded. Measured over Strike at Karkand: of
// 1336 placed objects, truncation matches every one of the 823 that has an
// entry, and rounding disagrees on 1115 positions and loses.
//
// The two digits are the geometry and the lod: `00` is geom 0 lod 0, and the
// file also carries `01`, `02`, `03` for the lower lods and `10`..`12` for a
// second geometry. We draw lod 0, so we ask for `00`.
//
// The 513 objects with no entry have none at any lod either: they are the
// vegetation and the thin props, which the game lights with its own tree
// shaders rather than with a baked map.
class ObjectLightmaps {
 public:
  // Empty when the level has no such file — most do, but nothing depends on it.
  static ObjectLightmaps load(FileSystem& files, std::string_view levelName);

  // Parses the text directly. Split out so the format can be tested without a
  // level on disk.
  static ObjectLightmaps parse(std::string_view text, std::string_view levelName);

  // The entry for one placement, or nothing when the object has no light map.
  const LightmapPlacement* find(std::string_view templateName, Vec3f position) const;

  // `Levels/<name>/lightmaps/Objects/LightmapAtlas<N>.dds`.
  std::string atlasPath(int index) const;

  std::size_t size() const { return byKey_.size(); }
  int atlasCount() const { return atlasCount_; }

 private:
  std::unordered_map<std::string, LightmapPlacement> byKey_;
  std::string levelName_;
  int atlasCount_ = 0;
};

}  // namespace obf2::level
