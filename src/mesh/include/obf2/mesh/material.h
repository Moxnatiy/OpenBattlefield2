#pragma once
#include "obf2/mesh/bf2_mesh.h"
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

// Whether the surface is cut out by its texture's alpha.
//
// A static mesh says so in `alphaMode`: 2 is the alpha-tested one, and its
// technique names only channels (`BaseDetailNDetail`), never the state. The
// other two kinds say it in the technique's own name instead — `Alpha_Test`,
// `Alpha_TestColormapGloss`, `AnimatedUVAlpha_TestColormapGloss` — and a
// skinned mesh leaves `alphaMode` at 0 while doing it.
//
// That is not a guess about names: over every mesh in the game
// (`mesh_info --alpha`) each of the 45 bundled materials whose technique carries
// `Alpha_Test` also carries `alphaMode 2`, so on that kind the two always agree.
// The six that disagree are the washing on Strike at Karkand's lines
// (`objects/common/cloth_line/meshes/cloth_line.skinnedmesh`, `SkinnedMesh.fx`,
// technique `Alpha_Test`, `alphaMode 0`) — and its texture's alpha is exactly
// binary, 37.6% at zero and 62.4% at 255, so it is a cutout and nothing else.
bool materialAlphaTest(std::string_view technique, int alphaMode);

// Whether the surface is blended rather than cut out: `alphaMode` 1, or a
// technique that carries `Alpha` without `Alpha_Test` — glass, canopies, the
// 144 `Alpha` materials of the game's bundled meshes. Read, and not drawn
// differently yet: we have no blended pass for the world.
bool materialAlphaBlend(std::string_view technique, int alphaMode);

// Whether the engine draws this mesh as vegetation, which is decided by its
// **path** and nothing else. `StaticMeshTemplate::load` (`RendDX9.dll`,
// 0x1011acd0 — the assert beside it names
// `Code\BF2\Geom\StaticMeshTemplate.cpp`) searches the file name it is about
// to load and keeps three flags:
//
//   this->isVegetation = name.find("vegitation") != npos;   // +0x29e
//   this->isWater      = name.find("objects/water/") != npos;
//   this->isRoad       = name.find("objects/roads/") != npos;
//   if (isRoad) this->noBlend = name.find("noBlend") != npos;
//
// The misspelling is the game's own, in the paths and in the binary alike.
// A vegetation mesh is then drawn by the tree shaders rather than the
// static-mesh one, and its materials split into leaves and trunk.
bool isVegetationPath(std::string_view meshPath);

// Mark the ranges of a vegetation mesh that are drawn as leaves.
//
// Which material is the leaves is the one thing here that is **not** proved.
// The engine keeps it as a flag on the material (`+0x1ec`, read while drawing
// at `RendDX9.dll` 0x100fcc70) and nothing we can read says where that flag
// comes from: it is not in the mesh's material (measured, `mesh_info
// --leafflag`), not in the object's `.tweak`, and not in the material manager.
//
// What we go by instead is the state the leaf shader itself sets — alpha test
// with `AlphaRef = 127` and no culling (`RaShaderLeaf.fx:233`), which for us is
// `alphaMode == 2`. Where the artists' texture names allow a check, the two
// agree: of 251 materials in the game's vegetation meshes whose base texture is
// named after a leaf, 249 are alpha-tested; 29 more alpha-tested materials are
// named otherwise, and those are leaves too by their geometry. It is an
// inference, and it is written down as one.
void markVegetationLeaves(RenderMesh& mesh, std::string_view meshPath);

}  // namespace obf2::mesh
