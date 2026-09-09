#pragma once
#include <string_view>

namespace obf2::mesh {

// Which texture slot each channel of a material sits in.
//
// A BF2 material names its technique by listing the channels it uses —
// `Base`, `BaseDetail`, `BaseDetailNDetail`, `BaseDetailDirtCrackNDetailNCrack`
// and so on — and the texture list is in the very same order. The specular
// lookup (`SpecularLUT_pow36.dds`) is appended after them and is not named in
// the technique.
//
// The channels are the ones the game's own shader switches on: `_BASE_`,
// `_DETAIL_`, `_DIRT_`, `_CRACK_`, `_NBASE_`, `_NDETAIL_`, `_NCRACK_`,
// `_PARALLAXDETAIL_` (`Shaders_client.zip:RaShaderSTM.fx:54`).
//
// -1 means the material has no such channel.
struct MaterialLayout {
  int base = -1;
  int detail = -1;
  int dirt = -1;
  int crack = -1;
  // The normal maps. Not sampled yet — we have no tangent frame — but written
  // down so the slot numbering stays whole (rule 3).
  int normalBase = -1;
  int normalDetail = -1;
  int normalCrack = -1;
  bool parallaxDetail = false;

  // How many slots the technique named. The specular lookup is the next one.
  int count = 0;
};

// Take a technique name apart. Unknown text stops the walk: better a short
// layout than a wrong one.
MaterialLayout materialLayout(std::string_view technique);

}  // namespace obf2::mesh
