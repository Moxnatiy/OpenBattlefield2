#pragma once
// Loading a BF2 level.
//
// A level describes itself in ordinary .con: Heightdata.con gives the size and
// scale of the height map, Terrain.con the patch split and the texture names,
// StaticObjects.con the object placement. So no reversing is needed here at all,
// the interpreter we already have is enough.
//
// One subtlety: a level's Init.con has two branches. The game one reads the
// compiled terraindata.raw, the editor one the source .raw height maps. We take
// the editor branch (`v_arg1 = BF2Editor`), because its format is fully
// described by data, while the compiled blob would have to be reversed.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::level {

struct HeightmapInfo {
  int size = 0;                  // 1025 — nodes per side
  Vec3f scale{2.0f, 1.0f, 2.0f};  // world units per node / per unit of height
  int bitResolution = 16;
  std::string dataPath;
  int clusterX = 0;
  int clusterY = 0;
};

struct TerrainInfo {
  int patchSize = 128;
  std::string colormapBase;
  std::string lightmapBase;
  std::string detailmapBase;
  float seaLevel = 0.0f;
  Vec3f waterColor{0.10f, 0.13f, 0.16f};  // renderer.waterColor from Water.con

  // Fog. `Renderer.fogColor` is 0..255, not 0..1, and
  // `Renderer.fogStartEndAndBase` carries four numbers, not three: start, end,
  // base, and a floor on visibility that ends up as the fog colour's alpha.
  // What each does is measured, see docs/formats/shaders.md.
  Vec3f fogColor{0.69f, 0.72f, 0.77f};
  float fogStart = 0.0f;
  float fogEnd = 0.0f;   // 0 = no fog
  float fogBase = 0.0f;  // the near ramp's slope
  float fogFloor = 1.0f; // the floor that ramp cannot go below

  // Lightmanager.* from Sky.con — stored only, for now.
  Vec3f ambientColor{0.9f, 0.9f, 0.9f};
  Vec3f sunColor{1.0f, 1.0f, 1.0f};

  // LightSettings.TerrainSunColor / TerrainSkyColor — the terrain's baked light
  // map is multiplied by exactly these. The values can exceed 1: they do not
  // merely tint, they brighten.
  Vec3f terrainSunColor{1.0f, 1.0f, 1.0f};
  Vec3f terrainSkyColor{0.6f, 0.7f, 0.9f};
};

// The `Lightmanager.*` block of a level's Sky.con — how the level is lit.
// All twenty-one commands are here; every one of the game's 13 levels sets
// every one of them (rule 3). Which of them the renderer uses is said beside
// each field.
struct Lighting {
  // What static meshes are lit by (`RaShaderSTM.fx:382`): the sun's colour, the
  // sky's, and the direction the sun's light travels — negative Y on every
  // level, since it comes down.
  Vec3f staticSunColor{0.8f, 0.8f, 0.8f};
  Vec3f staticSkyColor{0.3f, 0.35f, 0.4f};
  Vec3f staticSpecularColor{0.4f, 0.38f, 0.32f};  // read; no specular yet
  Vec3f sunDirection{-0.26f, -0.80f, -0.54f};
  // The shader adds this whole, gated by the light map's red channel. Zero on
  // most levels, 0.30 grey on some.
  Vec3f singlePointColor{0.0f, 0.0f, 0.0f};
  bool enableSun = true;

  // Vegetation has lighting of its own (`RaShaderTrunkOG.fx`, `RaShaderLeaf.fx`).
  // Read; not used yet.
  Vec3f treeAmbientColor{0.2f, 0.25f, 0.3f};
  Vec3f treeSunColor{0.7f, 0.6f, 0.5f};
  Vec3f treeSkyColor{0.0f, 0.0f, 0.0f};

  // Effects (particles, explosions). Read; not used yet.
  Vec3f effectSunColor{0.9f, 0.88f, 0.86f};
  Vec3f effectShadowColor{0.15f, 0.22f, 0.3f};
  float defaultEffectLightAffectionFactor = 1.0f;

  // The general ambient and sun, and the specular tint of the sun. The
  // renderer's own passes use the `static*` triple instead.
  Vec3f skyColor{0.4f, 0.55f, 0.7f};
  Vec3f ambientColor{0.6f, 0.7f, 0.8f};
  Vec3f sunColor{0.7f, 0.7f, 0.7f};
  Vec3f sunSpecColor{0.45f, 0.4f, 0.35f};

  // The hemisphere map: a top-down map of the ground's colour that the engine
  // tints objects near the ground with. `hemiMapManager.setBaseHemiMap <path>
  // <centre> <size> <height>`. Read; not used yet.
  float hemiLerpBias = 0.0f;
  std::string baseHemiMap;
  float hemiMapSize = 0.0f;
  float hemiMapHeight = 0.0f;
};

// The `Skydome.*` block of a level's Sky.con. All fourteen commands are here
// even though only three are drawn yet (rule 3): every one of the game's 13
// levels sets all of them, and leaving a field out is how a format quietly
// stops being whole.
//
// The paths come with Windows separators and no extension —
// `common\textures\sky\karkand_cloudy` — so they are normalised on the way in
// and `.dds` is appended.
struct Sky {
  std::string domeTemplate;   // Skydome.skyTemplate; "skydome" on every level
  std::string cloudTemplate;  // Skydome.cloudTemplate; "cloudlayer" on every level
  std::string texture;        // Skydome.skyTexture — the dome's own map
  float domeRotation = 0.0f;  // Skydome.domeRotation, degrees about Y

  // The cloud layers. Not drawn: they need the scrolling offsets the engine
  // advances per frame, and a second dome.
  bool hasCloudLayer = false;
  bool hasCloudLayer2 = false;
  std::string cloudTexture;
  std::string cloudTexture2;
  float scrollDirection[2]{};
  float scrollDirection2[2]{};
  float fadeCloudsDistances[2]{};  // near/far, the fade the dome's own shader does
  float cloudLerpFactors[2]{};

  // The sun's flare: its own sprite object, drawn additively. Not done.
  std::string flareTexture;
  Vec3f flareDirection{0.0f, 0.0f, 0.0f};
};

// A road from CompiledRoads.con. The `.mesh` format differs from the other
// meshes: the vertices lie relative to the `start` point, and the position is
// accompanied by two pairs of texture coordinates and an alpha for edge fading.
//
// The layout (after Project Dalian, engine/formats/mesh/bf2_road_mesh.cpp):
//   0  u32   version
//   4  float3 start
//   16 float length
//   20 float3 end
//   32 float3 misc
//   48 u32   vertex count      (the header is 52 bytes)
//   then   vertices of 32 bytes each: position, u/v, u1/v1, alpha
//   then   u32 index count and the indices themselves, 16 bits each
struct Road {
  std::string templateName;
  // How hard the markings are mixed over the tiling surface, from the
  // template's `SetBlendFactor`.
  float blendFactor = 1.0f;
  std::string meshPath;
  Vec3f position;
  mesh::RenderMesh geometry;
};

std::optional<mesh::RenderMesh> loadRoadMesh(std::span<const std::byte> bytes,
                                             std::string* error = nullptr);

// Placement from StaticObjects.con: `Object.create` + absolutePosition/rotation.
struct StaticObject {
  std::string templateName;
  Vec3f position;
  Vec3f rotation;  // yaw/pitch/roll in degrees
  bool hasRotation = false;

  // Vegetation is given not by angles but by a ready matrix
  // (`Object.absoluteTransformation`) — with rotation and scale at once.
  bool hasTransform = false;
  Mat4 transform = Mat4::identity();

  // `Object.isOvergrowth 1`: in the original such objects are drawn by a separate
  // vegetation system, and in the file they exist for collision.
  bool isOvergrowth = false;
};

struct Level {
  std::string name;
  TerrainInfo terrain;
  Sky sky;
  Lighting lighting;
  HeightmapInfo primary;
  std::vector<StaticObject> objects;
  std::vector<Road> roads;
  // A road template's name -> how the game paints it. A template names **two**
  // textures and a factor to mix them by:
  //
  //   RoadTemplate.SetBlendFactor 0.85
  //   RoadTemplate.SetIsPrimaryTexture 1
  //   RoadTemplateTexture.SetTextureFile "…/road_desert_2lane_512_2"
  //   RoadTemplate.SetIsPrimaryTexture 0
  //   RoadTemplateTexture.SetTextureFile "…/tarmac_layer1_lowtiling"
  //
  // and the shader mixes them exactly that way (`Road.fx:77`,
  // `lerp(tex1.rgb, tex0.rgb, saturate(fBlendFactor))`): the primary carries
  // the markings on its own UV set, the secondary is the tiling surface under
  // them on a second set.
  struct RoadTemplate {
    std::string primary;    // the markings, sampled with the mesh's uv
    std::string secondary;  // the tiling surface, sampled with uv2
    float blendFactor = 1.0f;
  };
  std::unordered_map<std::string, RoadTemplate> roadTextures;

  // Heights in world units, size*size of them, rows running north to south.
  std::vector<float> heights;

  // The spawn screen's camera — set by the level itself in Init.con:
  //
  //   gameLogic.setBeforeSpawnCamera -50/185/-285 -16/-3/0
  //
  // The first triple is the position, the second the rotation in degrees (yaw,
  // pitch, roll). Until the player has spawned the game looks from exactly
  // there, rather than orbiting the map.
  bool hasBeforeSpawnCamera = false;

  // The team names also come from the level's Init.con:
  //
  //   gameLogic.setTeamName 1 "CH"
  //   gameLogic.setTeamName 2 "US"
  //
  // This is not merely a caption: it is the string the game substitutes for %s
  // in the point icon's template `Ingame/Flags/Icons/Minimap/%s/miniMap_CP.tga`
  // (BF2.exe, 0x74fb70 — the calls gameLogic->[0x48](1) and (2) are right
  // before the sprintf). The icon directories in the game are called Ch, Eu,
  // Mec, US, Neutral, and the set of team names across all 22 levels is exactly
  // CH, EU, MEC, US. Index 0 is the neutral side.
  std::string teamNames[3];
  Vec3f beforeSpawnCameraPos;
  Vec3f beforeSpawnCameraRot;

  float heightAt(int x, int z) const {
    if (x < 0 || z < 0 || x >= primary.size || z >= primary.size) return 0.0f;
    return heights[static_cast<std::size_t>(z) * primary.size + x];
  }

  // The terrain is centred on the origin, as the objects' positions are.
  float worldX(int x) const { return (static_cast<float>(x) - halfExtent()) * primary.scale.x; }
  float worldZ(int z) const { return (static_cast<float>(z) - halfExtent()) * primary.scale.z; }
  float halfExtent() const { return static_cast<float>(primary.size - 1) * 0.5f; }

  // The terrain's height under a point — by bilinear sampling of the height map.
  //
  // Without smoothing a soldier would step from grid node to grid node. It is
  // needed both by the server (it moves the bodies) and by the client (it
  // predicts its own soldier's movement while waiting for a correction), so it
  // lives here, next to the height map itself.
  float groundHeightAt(const Vec3f& position) const;
};

// Mounts the level's server.zip and client.zip at the point Levels/<name>.
bool mountLevel(FileSystem& files, const std::filesystem::path& modDir, std::string_view levelName,
                std::string* error = nullptr);

std::optional<Level> loadLevel(FileSystem& files, std::string_view levelName,
                               std::string* error = nullptr);

// One terrain patch: a grid of patchSize x patchSize quads with its own texture.
struct TerrainPatch {
  int column = 0;
  int row = 0;
  mesh::RenderMesh geometry;
  std::string colormap;  // the path to this patch's .dds
  std::string lightmap;  // the same patch's baked lighting, if present
  std::string detailmap; // detail: fine structure seen close up
};

// Patches with no colour map in the game are entirely under water — the game
// does not draw them either. So they are skipped, and the water plane covers the sea.
std::vector<TerrainPatch> buildTerrainPatches(const Level& level, const FileSystem& files);

// The sky dome: the mesh the level's `Skydome.skyTemplate` names, with the
// level's own sky texture put in place of the template's default.
//
// The dome is drawn around the camera, so its geometry comes back in its own
// coordinates — the caller places it. Empty when the level names no sky or the
// mesh is not in the archives.
std::optional<mesh::RenderMesh> buildSkyDome(const Level& level, const FileSystem& files);

// The name the engine substitutes for the water surface's texture file: the
// water's colour is given as a number in Water.con, not as a picture.
inline constexpr const char* kWaterColorMap = "#waterColor";

mesh::RenderMesh buildWaterPlane(const Level& level);

}  // namespace obf2::level
