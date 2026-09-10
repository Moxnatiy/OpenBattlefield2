#include "check.h"
#include "obf2/mesh/material.h"

using namespace obf2::mesh;

namespace {

// The five techniques that cover the game's static meshes, with the counts
// `tools/mesh_info --materials` reports over the whole corpus. The expected
// slots are not a guess: the same run prints the suffix of every slot, and the
// two `_cr` rows are what pins the ordering down —
//
//   BaseDetailCrackNDetailNCrack       125 materials, and "slot 2: _cr" 125
//   BaseDetailDirtCrackNDetailNCrack   243 materials, and "slot 3: _cr" 243
//
// The crack map moves from slot 2 to slot 3 exactly when `Dirt` appears before
// it in the name. So the slots follow the tokens of the technique, in order.
void testTechniqueOrderMatchesTheCorpus() {
  const MaterialLayout base = materialLayout("Base");
  CHECK_EQ(base.base, 0);
  CHECK_EQ(base.detail, -1);
  CHECK_EQ(base.count, 1);  // the specular lookup is the next slot

  const MaterialLayout detail = materialLayout("BaseDetail");
  CHECK_EQ(detail.base, 0);
  CHECK_EQ(detail.detail, 1);
  CHECK_EQ(detail.count, 2);

  // 4191 materials — the commonest of all, and the one tree trunks use.
  const MaterialLayout trunk = materialLayout("BaseDetailNDetail");
  CHECK_EQ(trunk.base, 0);
  CHECK_EQ(trunk.detail, 1);
  CHECK_EQ(trunk.normalDetail, 2);
  CHECK_EQ(trunk.dirt, -1);
  CHECK_EQ(trunk.count, 3);

  // 1125 materials. Dirt pushes the detail normal along by one.
  const MaterialLayout dirt = materialLayout("BaseDetailDirtNDetail");
  CHECK_EQ(dirt.base, 0);
  CHECK_EQ(dirt.detail, 1);
  CHECK_EQ(dirt.dirt, 2);
  CHECK_EQ(dirt.normalDetail, 3);
  CHECK_EQ(dirt.count, 4);

  // 125 materials: the crack lands in slot 2.
  const MaterialLayout crack = materialLayout("BaseDetailCrackNDetailNCrack");
  CHECK_EQ(crack.crack, 2);
  CHECK_EQ(crack.normalDetail, 3);
  CHECK_EQ(crack.normalCrack, 4);
  CHECK_EQ(crack.count, 5);

  // 243 materials: the same crack, now in slot 3.
  const MaterialLayout both = materialLayout("BaseDetailDirtCrackNDetailNCrack");
  CHECK_EQ(both.dirt, 2);
  CHECK_EQ(both.crack, 3);
  CHECK_EQ(both.normalDetail, 4);
  CHECK_EQ(both.normalCrack, 5);
  CHECK_EQ(both.count, 6);
}

// `NDetail` must not be read as `Detail`, and `parallaxdetail` must not be read
// as another detail channel: it is a flag on the one that is already there.
void testLongerTokensWinOverShorterOnes() {
  const MaterialLayout normalOnly = materialLayout("BaseNDetail");
  CHECK_EQ(normalOnly.base, 0);
  CHECK_EQ(normalOnly.detail, -1);
  CHECK_EQ(normalOnly.normalDetail, 1);
  CHECK_EQ(normalOnly.count, 2);

  // 53 materials in the game.
  const MaterialLayout parallax = materialLayout("BaseDetailNDetailparallaxdetail");
  CHECK(parallax.parallaxDetail);
  CHECK_EQ(parallax.base, 0);
  CHECK_EQ(parallax.detail, 1);
  CHECK_EQ(parallax.normalDetail, 2);
  CHECK_EQ(parallax.count, 3);  // the flag takes no slot of its own
}

// 31 of the game's materials have an empty technique, and the editor writes
// mixed case. Neither may be read as something it is not.
void testEmptyAndOddNamesGiveNothing() {
  const MaterialLayout empty = materialLayout("");
  CHECK_EQ(empty.base, -1);
  CHECK_EQ(empty.count, 0);

  const MaterialLayout unknown = materialLayout("Wobble");
  CHECK_EQ(unknown.count, 0);

  const MaterialLayout mixed = materialLayout("basedetail");
  CHECK_EQ(mixed.base, 0);
  CHECK_EQ(mixed.detail, 1);

  // Text we do not know stops the walk instead of shifting every slot after it.
  const MaterialLayout stops = materialLayout("BaseWobbleDetail");
  CHECK_EQ(stops.base, 0);
  CHECK_EQ(stops.detail, -1);
  CHECK_EQ(stops.count, 1);
}

// The engine decides vegetation by the mesh's path and nothing else
// (`RendDX9.dll`, 0x1011acd0). The misspelling is the game's own.
void testVegetationIsDecidedByThePath() {
  CHECK(isVegetationPath("objects/vegitation/middle-east/palmtree/meshes/me_palmtree01.staticmesh"));
  CHECK(isVegetationPath("booster_client/vegitation/america/meshes/xp2_appletree.staticmesh"));
  // Spelled the way English would, it is not the game's directory and the
  // engine would not match it either.
  CHECK(!isVegetationPath("objects/vegetation/tree/meshes/tree.staticmesh"));
  CHECK(!isVegetationPath("objects/staticobjects/city/meshes/house_high_06.staticmesh"));
  CHECK(!isVegetationPath(""));
}

void testOnlyAlphaTestedMaterialsOfAVegetationMeshAreLeaves() {
  RenderMesh mesh;
  mesh.ranges.resize(3);
  mesh.ranges[0].alphaMode = 2;  // the leaves
  mesh.ranges[1].alphaMode = 0;  // the trunk
  mesh.ranges[2].alphaMode = 2;  // more leaves

  markVegetationLeaves(mesh, "objects/vegitation/middle-east/palmtree/meshes/me_palmtree01.staticmesh");
  CHECK(mesh.ranges[0].leaf);
  CHECK(!mesh.ranges[1].leaf);
  CHECK(mesh.ranges[2].leaf);

  // The same materials in a building are not leaves — a fence is alpha-tested
  // too, and it is lit like a wall.
  RenderMesh house;
  house.ranges.resize(2);
  house.ranges[0].alphaMode = 2;
  house.ranges[1].alphaMode = 0;
  markVegetationLeaves(house, "objects/staticobjects/city/meshes/fence_01.staticmesh");
  CHECK(!house.ranges[0].leaf);
  CHECK(!house.ranges[1].leaf);
}

// Where transparency is written differs by mesh kind, and the corpus behind
// each of these lines is in `materialAlphaTest`'s comment.
static void testAlphaComesFromEitherTheModeOrTheName() {
  // A static mesh: the mode says it, the technique names only channels.
  CHECK(!materialAlphaTest("BaseDetailNDetail", 0));
  CHECK(!materialAlphaBlend("BaseDetailNDetail", 0));
  CHECK(materialAlphaTest("Base", 2));
  CHECK(materialAlphaTest("BaseDetailNDetail", 2));
  // A material with no technique at all still has its mode.
  CHECK(materialAlphaTest("", 2));

  // The washing on Karkand's lines: a skinned mesh whose mode is 0 and whose
  // technique is the whole answer. This is the one that was drawn as a plate.
  CHECK(materialAlphaTest("Alpha_Test", 0));
  CHECK(!materialAlphaBlend("Alpha_Test", 0));
  // Case and neighbouring tokens must not matter.
  CHECK(materialAlphaTest("tangent_alpha_test1Alpha_Test", 0));
  CHECK(materialAlphaTest("Alpha_TestColormapGloss", 2));
  CHECK(materialAlphaTest("AnimatedUVAlpha_TestColormapGloss", 2));

  // Blended: glass and canopies. Read, not drawn differently yet.
  CHECK(materialAlphaBlend("Alpha", 1));
  CHECK(materialAlphaBlend("AlphaEnvMap", 1));
  CHECK(materialAlphaBlend("AlphaNoHemiLight", 1));
  CHECK(materialAlphaBlend("Alpha_One", 1));  // the glow meshes
  CHECK(!materialAlphaTest("Alpha", 1));

  // And the ordinary opaque ones.
  CHECK(!materialAlphaTest("ColormapGloss", 0));
  CHECK(!materialAlphaBlend("ColormapGloss", 0));
  CHECK(!materialAlphaTest("EnvMapColormapGloss", 0));
  CHECK(!materialAlphaBlend("", 0));
}

}  // namespace

TEST_MAIN({
  testAlphaComesFromEitherTheModeOrTheName();
  testTechniqueOrderMatchesTheCorpus();
  testLongerTokensWinOverShorterOnes();
  testEmptyAndOddNamesGiveNothing();
  testVegetationIsDecidedByThePath();
  testOnlyAlphaTestedMaterialsOfAVegetationMeshAreLeaves();
})
