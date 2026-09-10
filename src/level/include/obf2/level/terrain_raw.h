#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::level {

// One of the terrain's materials: the texture the ground is textured with close
// up where this material owns it, and how it is laid on.
//
// A level has exactly six, and which one owns a texel is a level's own chart
// maps — `Detailmaps/txCCxRR_1.dds` and `_2.dds`, three materials to an image,
// one per colour channel. The shader picks between them with `vComponentsel`
// (`Shaders_client.zip:TerrainShader_Hi.fx:86`) and draws the terrain once per
// material, adding the results.
// The four floats are the near counterpart of the level's far tilings, and in
// the same order — `vNearTexTiling = (side x, side y, top, y offset)`. The
// order is the loader's, not a guess: the material is filled field by field at
// `RendDX9.dll`, 0x100ddc94, and the vec2 lands at +0x1c *before* the single
// float at +0x18, which is why the third number reads like a distance in metres
// and is not one (docs/formats/terraindata.md).
struct TerrainMaterial {
  std::string texture;       // "common\terrain\textures\detail\detail_rock04"
  bool triPlanar = false;    // +0x15: draw this one from three directions
  float sideTilingX = 2.0f;  // +0x1c, the x plane — only the tri-planar pass uses it
  float sideTilingY = 2.0f;  // +0x20, the z plane
  float topTiling = 32.0f;   // +0x18, the y plane: what flat ground is textured with
  float yOffset = 0.0f;      // +0x24, slides the side planes up the texture
  bool envMap = false;       // +0x28: reflect the level's environment map off it
};

// What `Levels/<name>/terraindata.raw` says about the terrain, as far as we read
// it: the header and the six materials.
//
// The file is the compiled terrain the **game** loads — `terrain.create Terrain`
// and `terrain.load …/terraindata.raw`, the branch of Terrain.con that runs when
// the level is not opened in the editor (docs/research/12-renddx9.md). We build
// the terrain from the editor's loose files instead, which is the same data for
// the heights and the tiles; the materials are the one thing only this file has.
//
// The layout comes from the writer, `TerrainEditable::save` (`RendDX9.dll`,
// 0x1010cd70), field by field — docs/formats/terraindata.md, and
// `tools/terrain_raw.py` walks the same order.
struct TerrainRaw {
  std::uint32_t version = 0;  // 0x0001001a on every level of the game
  Vec3f primaryWorldScale{2.0f, 1.0f, 2.0f};
  Vec3f secondaryWorldScale{4.0f, 1.0f, 4.0f};
  float highestHeight = 0.0f;
  float lowestHeight = 0.0f;
  int patchSize = 128;
  bool subdividePatches = false;
  int patchesPerSide = 0;
  int patchColormapSize = 512;
  int lowDetailmapSize = 512;
  std::string colormapBase, detailmapBase, lowDetailmapBase, lightmapBase;
  float farSideTiling[2]{5.0f, 5.0f};
  float farTopTilingHi = 24.0f;
  float farTopTilingLow = 4.0f;
  float farYOffset = 0.0f;
  Vec3f sunColor{1.0f, 1.0f, 1.0f};
  Vec3f giColor{1.0f, 1.0f, 1.0f};
  Vec3f waterColor{0.0f, 0.0f, 0.0f};
  std::vector<TerrainMaterial> materials;
};

// Nothing but the header and the materials is read: the rest of the file is one
// block per patch — heights and the morph deltas that carry a patch between LOD
// levels — and we build our geometry from the height map instead.
std::optional<TerrainRaw> readTerrainRaw(std::span<const std::byte> bytes,
                                         std::string* error = nullptr);

// The same, by level name: `Levels/<name>/terraindata.raw`. Nothing when the
// level does not ship one.
std::optional<TerrainRaw> loadTerrainRaw(const FileSystem& files, std::string_view levelName,
                                         std::string* error = nullptr);

}  // namespace obf2::level
