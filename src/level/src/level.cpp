#include "obf2/level/level.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "obf2/con/interpreter.h"
#include "obf2/core/path.h"

namespace obf2::level {
namespace {

// Parses a slash-separated list of numbers. In a .con both vectors and longer
// sets such as fogStartEndAndBase's four values are written that way.
// Returns how many numbers were read.
std::size_t parseSlashList(std::string_view text, float* out, std::size_t capacity) {
  std::size_t count = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size() && count < capacity; ++i) {
    if (i != text.size() && text[i] != '/') continue;
    const std::string part(text.substr(start, i - start));
    start = i + 1;
    if (part.empty()) continue;

    char* end = nullptr;
    const float value = std::strtof(part.c_str(), &end);
    if (end == part.c_str()) continue;
    out[count++] = value;
  }
  return count;
}

// A texture path out of Sky.con. The data writes them the Windows way and
// without an extension (`common\textures\sky\karkand_cloudy`) while the
// archives are mounted with forward slashes and hold `.dds`.
std::string skyTexturePath(std::string_view raw) {
  if (raw.empty()) return {};
  std::string out(raw);
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  return out + ".dds";
}

// The state collector while a level's .con runs. The language is a stream of
// commands, so heightmap.* apply to the last heightmapcluster.addHeightmap and
// Object.* to the last Object.create.
class LevelBuilder {
 public:
  LevelBuilder(Level& level, const FileSystem& files) : level_(level), files_(files) {}

  void operator()(const con::Command& command) {
    const std::string& path = command.lowerPath;

    // --- Terrain.con, the branch the game takes ---
    // `terrain.load Levels/<name>/terraindata.raw`. Everything the editor's
    // branch says in twenty lines of `terrain.*` is in that blob, written from
    // the same state by the same function (`TerrainEditable::save`,
    // `RendDX9.dll`, 0x1010cd70) — and where the two disagree the blob is what
    // the game reads, which on Karkand is `farTopTilingLow`: 4 in the `.con`
    // and 24 in the blob.
    //
    // It is read here, and not after the level's files have all been run, so
    // that it lands where the engine lands it: Init.con runs Terrain.con before
    // Sky.con, and Sky.con's `terrain.sunColor` then has the last word over the
    // pair the blob carries.
    if (path == "terrain.load") {
      loadCompiledTerrain(command.argStr(0));
      return;
    }

    // --- Heightdata.con ---
    // `heightmapcluster.addHeightmap Heightmap 0 0` — the zeroth argument is the
    // name, the cluster's coordinates follow. The main map is (0,0), the other
    // eight around it are the low-detail surroundings on the horizon.
    if (path == "heightmapcluster.addheightmap") {
      pendingCluster_ = true;
      clusterX_ = command.argInt(1).value_or(9999);
      clusterY_ = command.argInt(2).value_or(9999);
      return;
    }
    if (path == "heightmapcluster.setseawaterlevel") {
      level_.terrain.seaLevel = command.argFloat(0).value_or(0.0f);
      return;
    }
    //   gameLogic.setTeamName 1 "CH"
    if (path == "gamelogic.maximumlevelviewdistance") {
      level_.maximumViewDistance = command.argFloat(0).value_or(0.0f);
      return;
    }
    if (path == "gamelogic.setteamname") {
      const int team = command.argInt(0).value_or(-1);
      if (team >= 0 && team <= 2) level_.teamNames[team] = std::string(command.argStr(1));
      return;
    }
    // The spawn screen's camera is set by the level itself:
    //   gameLogic.setBeforeSpawnCamera -50/185/-285 -16/-3/0
    // Both triples are written as one slash-separated word — the position and
    // the rotation in degrees.
    if (path == "gamelogic.setbeforespawncamera") {
      const auto triple = [&](std::size_t i, Vec3f& out) {
        if (i >= command.args.size()) return false;
        const std::string& text = command.args[i];
        float v[3] = {0.0f, 0.0f, 0.0f};
        std::size_t at = 0;
        for (float& part : v) {
          const std::size_t slash = text.find('/', at);
          part = std::strtof(text.substr(at, slash - at).c_str(), nullptr);
          if (slash == std::string::npos) break;
          at = slash + 1;
        }
        out = Vec3f{v[0], v[1], v[2]};
        return true;
      };
      if (triple(0, level_.beforeSpawnCameraPos) && triple(1, level_.beforeSpawnCameraRot)) {
        level_.hasBeforeSpawnCamera = true;
      }
      return;
    }
    // We only care about the central fragment of cluster (0,0) — that is the
    // actual playable map. The other eight are the low-detail horizon.
    if (path == "heightmap.setsize" && isPrimary()) {
      level_.primary.size = command.argInt(0).value_or(0);
      level_.primary.clusterX = clusterX_;
      level_.primary.clusterY = clusterY_;
      return;
    }
    if (path == "heightmap.setscale" && isPrimary()) {
      if (const auto scale = command.argVec3(0)) {
        level_.primary.scale = Vec3f{scale->x, scale->y, scale->z};
      }
      return;
    }
    if (path == "heightmap.setbitresolution" && isPrimary()) {
      level_.primary.bitResolution = command.argInt(0).value_or(16);
      return;
    }
    if (path == "heightmap.loadheightdata" && isPrimary()) {
      level_.primary.dataPath = std::string(command.argStr(0));
      return;
    }

    // --- Terrain.con ---
    if (path == "terrain.patchsize") {
      level_.terrain.patchSize = command.argInt(0).value_or(128);
      return;
    }
    if (path == "terrain.colormapbasename") {
      level_.terrain.colormapBase = std::string(command.argStr(0));
      return;
    }
    // --- Sky.con ---
    if (path == "renderer.fogcolor") {
      if (const auto color = command.argVec3(0)) {
        // 0..255 in the file, we need 0..1.
        level_.terrain.fogColor = Vec3f{color->x / 255.0f, color->y / 255.0f, color->z / 255.0f};
      }
      return;
    }
    if (path == "renderer.fogstartendandbase") {
      // Here there are FOUR slash-separated components ("0.00/610.00/0.00/0.50"),
      // so argVec3 will not do — we parse it ourselves. Only the first two are needed.
      float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      const std::size_t read = parseSlashList(command.argStr(0), values, 4);
      if (read >= 2) {
        level_.terrain.fogStart = values[0];
        level_.terrain.fogEnd = values[1];
      }
      // All four matter, and the name accounts for only three. The third is
      // the slope of the near ramp and the fourth the floor under it; both
      // were measured out of the engine's own uploaded constants
      // (docs/formats/shaders.md).
      if (read >= 3) level_.terrain.fogBase = values[2];
      if (read >= 4) level_.terrain.fogFloor = values[3];
      return;
    }
    // --- Sky.con, the `Skydome.*` block ---
    //
    // The dome is what the horizon is made of: the terrain fades into the fog
    // and then meets the sky. Every one of the game's levels sets all fourteen
    // of these; we read them all and draw the three that need no per-frame
    // work (docs/formats/shaders.md).
    if (path == "skydome.skytemplate") {
      level_.sky.domeTemplate = std::string(command.argStr(0));
      return;
    }
    if (path == "skydome.cloudtemplate") {
      level_.sky.cloudTemplate = std::string(command.argStr(0));
      return;
    }
    if (path == "skydome.skytexture") {
      level_.sky.texture = skyTexturePath(command.argStr(0));
      return;
    }
    if (path == "skydome.cloudtexture") {
      level_.sky.cloudTexture = skyTexturePath(command.argStr(0));
      return;
    }
    if (path == "skydome.cloudtexture2") {
      level_.sky.cloudTexture2 = skyTexturePath(command.argStr(0));
      return;
    }
    if (path == "skydome.flaretexture") {
      level_.sky.flareTexture = skyTexturePath(command.argStr(0));
      return;
    }
    if (path == "skydome.domerotation") {
      level_.sky.domeRotation = command.argFloat(0).value_or(0.0f);
      return;
    }
    if (path == "skydome.hascloudlayer") {
      level_.sky.hasCloudLayer = command.argInt(0).value_or(0) != 0;
      return;
    }
    if (path == "skydome.hascloudlayer2") {
      level_.sky.hasCloudLayer2 = command.argInt(0).value_or(0) != 0;
      return;
    }
    if (path == "skydome.scrolldirection") {
      parseSlashList(command.argStr(0), level_.sky.scrollDirection, 2);
      return;
    }
    if (path == "skydome.scrolldirection2") {
      parseSlashList(command.argStr(0), level_.sky.scrollDirection2, 2);
      return;
    }
    if (path == "skydome.fadeclouddistances" || path == "skydome.fadeclouddsdistances" ||
        path == "skydome.fadecloudsdistances") {
      parseSlashList(command.argStr(0), level_.sky.fadeCloudsDistances, 2);
      return;
    }
    if (path == "skydome.cloudlerpfactors") {
      parseSlashList(command.argStr(0), level_.sky.cloudLerpFactors, 2);
      return;
    }
    if (path == "skydome.flaredirection") {
      if (const auto direction = command.argVec3(0)) {
        level_.sky.flareDirection = Vec3f{direction->x, direction->y, direction->z};
      }
      return;
    }
    // Two spellings of the same pair: `LightSettings.*` in the editor's branch
    // of Sky.con and `terrain.*` in the game's. See TerrainInfo for the block.
    if (path == "lightsettings.terrainsuncolor" || path == "terrain.suncolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.terrainSunColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }
    if (path == "lightsettings.terrainskycolor" || path == "terrain.gicolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.terrainSkyColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }
    if (path == "lightmanager.ambientcolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.ambientColor = Vec3f{color->x, color->y, color->z};
        level_.lighting.ambientColor = level_.terrain.ambientColor;
      }
      return;
    }
    if (path == "lightmanager.suncolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.sunColor = Vec3f{color->x, color->y, color->z};
        level_.lighting.sunColor = level_.terrain.sunColor;
      }
      return;
    }

    // --- Sky.con, the `Lightmanager.*` block ---
    if (path == "lightmanager.staticsuncolor" || path == "lightmanager.staticskycolor" ||
        path == "lightmanager.staticspecularcolor" || path == "lightmanager.sundirection" ||
        path == "lightmanager.singlepointcolor" || path == "lightmanager.treeambientcolor" ||
        path == "lightmanager.treesuncolor" || path == "lightmanager.treeskycolor" ||
        path == "lightmanager.effectsuncolor" || path == "lightmanager.effectshadowcolor" ||
        path == "lightmanager.skycolor" || path == "lightmanager.sunspeccolor") {
      const auto color = command.argVec3(0);
      if (!color) return;
      const Vec3f value{color->x, color->y, color->z};
      Lighting& lighting = level_.lighting;
      if (path == "lightmanager.staticsuncolor") lighting.staticSunColor = value;
      else if (path == "lightmanager.staticskycolor") lighting.staticSkyColor = value;
      else if (path == "lightmanager.staticspecularcolor") lighting.staticSpecularColor = value;
      else if (path == "lightmanager.sundirection") lighting.sunDirection = value;
      else if (path == "lightmanager.singlepointcolor") lighting.singlePointColor = value;
      else if (path == "lightmanager.treeambientcolor") lighting.treeAmbientColor = value;
      else if (path == "lightmanager.treesuncolor") lighting.treeSunColor = value;
      else if (path == "lightmanager.treeskycolor") lighting.treeSkyColor = value;
      else if (path == "lightmanager.effectsuncolor") lighting.effectSunColor = value;
      else if (path == "lightmanager.effectshadowcolor") lighting.effectShadowColor = value;
      else if (path == "lightmanager.skycolor") lighting.skyColor = value;
      else if (path == "lightmanager.sunspeccolor") lighting.sunSpecColor = value;
      return;
    }
    if (path == "lightmanager.enablesun") {
      level_.lighting.enableSun = command.argInt(0).value_or(1) != 0;
      return;
    }
    if (path == "lightmanager.hemilerpbias") {
      level_.lighting.hemiLerpBias = command.argFloat(0).value_or(0.0f);
      return;
    }
    if (path == "lightmanager.defaulteffectlightaffectionfactor") {
      level_.lighting.defaultEffectLightAffectionFactor = command.argFloat(0).value_or(1.0f);
      return;
    }
    if (path == "hemimapmanager.setbasehemimap") {
      // <path> <centre x/y/z> <size> <height>
      level_.lighting.baseHemiMap = std::string(command.argStr(0));
      level_.lighting.hemiMapSize = command.argFloat(2).value_or(0.0f);
      level_.lighting.hemiMapHeight = command.argFloat(3).value_or(0.0f);
      return;
    }

    if (path == "terrain.lightmapbasename") {
      level_.terrain.lightmapBase = std::string(command.argStr(0));
      return;
    }
    if (path == "terrain.detailmapbasename") {
      level_.terrain.detailmapBase = std::string(command.argStr(0));
      return;
    }
    if (path == "terrain.lowdetailmapbasename") {
      level_.terrain.lowDetailmapBase = std::string(command.argStr(0));
      return;
    }
    if (path == "terrain.patchcolormapsize") {
      level_.terrain.patchColormapSize = command.argInt(0).value_or(512);
      return;
    }
    if (path == "terrain.lowdetailmapsize") {
      level_.terrain.lowDetailmapSize = command.argInt(0).value_or(512);
      return;
    }
    if (path == "terrain.farsidetiling") {
      parseSlashList(command.argStr(0), level_.terrain.farSideTiling, 2);
      return;
    }
    if (path == "terrain.fartoptilinghi") {
      level_.terrain.farTopTilingHi = command.argFloat(0).value_or(24.0f);
      return;
    }
    if (path == "terrain.fartoptilinglow") {
      level_.terrain.farTopTilingLow = command.argFloat(0).value_or(4.0f);
      return;
    }
    if (path == "terrain.faryoffset") {
      level_.terrain.farYOffset = command.argFloat(0).value_or(0.0f);
      return;
    }
    if (path == "terrain.primaryworldscale") {
      if (const auto scale = command.argVec3(0)) {
        level_.terrain.subdivideScale = Vec3f{scale->x, scale->y, scale->z};
      }
      return;
    }

    // --- Water.con ---
    if (path == "renderer.watercolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.waterColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }

    // --- RoadTemplate: the roads' textures ---
    // The definitions lie in Roads/Splines/*.con under the editor branch.
    if (path == "roadtemplate.setname") {
      roadTemplateName_ = std::string(command.argStr(0));
      return;
    }
    if (path == "roadtemplate.setisprimarytexture") {
      // Which of the two the next SetTextureFile is. The template writes the
      // primary first and the secondary after it.
      roadPrimary_ = command.argInt(0).value_or(1) != 0;
      return;
    }
    if (path == "roadtemplate.setblendfactor" && !roadTemplateName_.empty()) {
      level_.roadTextures[roadTemplateName_].blendFactor =
          command.argFloat(0).value_or(1.0f);
      return;
    }
    if (path == "roadtemplatetexture.settexturefile" && !roadTemplateName_.empty()) {
      // Windows separators in the data, forward ones in the archives.
      std::string file(command.argStr(0));
      for (char& c : file) {
        if (c == '\\') c = '/';
      }
      file += ".dds";
      Level::RoadTemplate& road = level_.roadTextures[roadTemplateName_];
      (roadPrimary_ ? road.primary : road.secondary) = std::move(file);
      return;
    }

    // --- CompiledRoads.con ---
    // Roads also start with object.create, but loadMesh follows — and that is
    // exactly what tells them apart from ordinary placement.
    if (path == "object.geometry.loadmesh") {
      if (!level_.objects.empty()) {
        Road road;
        road.templateName = level_.objects.back().templateName;
        road.meshPath = std::string(command.argStr(0));
        level_.roads.push_back(std::move(road));
        // Remove it from the placement: this is a road, not a static object.
        level_.objects.pop_back();
        pendingRoad_ = true;
      }
      return;
    }

    // --- StaticObjects.con ---
    if (path == "object.create") {
      pendingRoad_ = false;
      StaticObject object;
      object.templateName = std::string(command.argStr(0));
      level_.objects.push_back(std::move(object));
      return;
    }
    // The road has already taken its object.create, so from here we work either
    // with it or with the last static object.
    if (level_.objects.empty() && !pendingRoad_) return;
    static StaticObject dummy;
    StaticObject& current = level_.objects.empty() ? dummy : level_.objects.back();

    if (path == "object.absoluteposition") {
      if (const auto position = command.argVec3(0)) {
        if (pendingRoad_ && !level_.roads.empty()) {
          level_.roads.back().position = Vec3f{position->x, position->y, position->z};
        } else {
          current.position = Vec3f{position->x, position->y, position->z};
        }
      }
      return;
    }
    if (path == "object.absolutetransformation") {
      // Four groups of four numbers: three rows of rotation with scale and a row
      // of translation. The format is the editor's: [x/y/z/w].
      float values[16] = {};
      int count = 0;
      for (const std::string& argument : command.args) {
        // The numbers are slash-separated and the whole group is bracketed — we
        // simply take every number in a row.
        const char* cursor = argument.c_str();
        while (*cursor != '\0' && count < 16) {
          if (*cursor == '[' || *cursor == ']' || *cursor == '/') { ++cursor; continue; }
          char* end = nullptr;
          const float value = std::strtof(cursor, &end);
          if (end == cursor) { ++cursor; continue; }
          values[count++] = value;
          cursor = end;
        }
      }
      if (count >= 16) {
        for (int i = 0; i < 16; ++i) current.transform.m[i] = values[i];
        current.hasTransform = true;
        current.position = Vec3f{values[12], values[13], values[14]};
      }
      return;
    }
    if (path == "object.isovergrowth") {
      current.isOvergrowth = command.argBool(0).value_or(false);
      return;
    }
    if (path == "object.rotation") {
      if (const auto rotation = command.argVec3(0)) {
        current.rotation = Vec3f{rotation->x, rotation->y, rotation->z};
        current.hasRotation = true;
      }
      return;
    }
  }

 private:
  bool isPrimary() const { return pendingCluster_ && clusterX_ == 0 && clusterY_ == 0; }

  // The compiled terrain, by the path the `.con` names. Every field of the
  // `terrain.*` block is in it — docs/formats/terraindata.md — and the six near
  // materials are in it and in nothing else.
  void loadCompiledTerrain(std::string_view path) {
    const auto bytes = files_.read(normalizeAssetPath(path));
    if (!bytes) return;
    auto raw = readTerrainRaw(*bytes);
    if (!raw) return;

    TerrainInfo& terrain = level_.terrain;
    terrain.patchSize = raw->patchSize;
    terrain.patchColormapSize = raw->patchColormapSize;
    terrain.lowDetailmapSize = raw->lowDetailmapSize;
    terrain.colormapBase = raw->colormapBase;
    terrain.detailmapBase = raw->detailmapBase;
    terrain.lowDetailmapBase = raw->lowDetailmapBase;
    terrain.lightmapBase = raw->lightmapBase;
    terrain.farSideTiling[0] = raw->farSideTiling[0];
    terrain.farSideTiling[1] = raw->farSideTiling[1];
    terrain.farTopTilingHi = raw->farTopTilingHi;
    terrain.farTopTilingLow = raw->farTopTilingLow;
    terrain.farYOffset = raw->farYOffset;
    terrain.subdivideScale = raw->primaryWorldScale;
    // The pair Sky.con sets again a moment later, under its own names.
    terrain.terrainSunColor = raw->sunColor;
    terrain.terrainSkyColor = raw->giColor;
    terrain.materials = std::move(raw->materials);
  }

  Level& level_;
  const FileSystem& files_;
  bool pendingCluster_ = false;
  bool pendingRoad_ = false;
  std::string roadTemplateName_;
  // `RoadTemplate.SetIsPrimaryTexture` applies to the SetTextureFile after it.
  bool roadPrimary_ = true;
  int clusterX_ = 0;
  int clusterY_ = 0;
};

bool loadHeights(FileSystem& files, Level& level, std::string* error) {
  const int size = level.primary.size;
  if (size <= 1) {
    if (error) *error = "Heightdata.con has no height map size";
    return false;
  }

  const auto bytes = files.read(level.primary.dataPath);
  if (!bytes) {
    if (error) *error = "height map not found: " + level.primary.dataPath;
    return false;
  }

  const std::size_t samples = static_cast<std::size_t>(size) * size;
  const std::size_t bytesPerSample = level.primary.bitResolution == 16 ? 2 : 1;
  if (bytes->size() < samples * bytesPerSample) {
    if (error) {
      *error = "the height map is smaller than the declared size: " + std::to_string(bytes->size()) +
               " bytes instead of " + std::to_string(samples * bytesPerSample);
    }
    return false;
  }

  level.heights.resize(samples);
  const auto* raw = reinterpret_cast<const std::uint8_t*>(bytes->data());
  for (std::size_t i = 0; i < samples; ++i) {
    std::uint32_t value = 0;
    if (bytesPerSample == 2) {
      value = static_cast<std::uint32_t>(raw[i * 2]) |
              (static_cast<std::uint32_t>(raw[i * 2 + 1]) << 8);
    } else {
      value = raw[i];
    }
    level.heights[i] = static_cast<float>(value) * level.primary.scale.y;
  }
  return true;
}

}  // namespace

bool mountLevel(FileSystem& files, const std::filesystem::path& modDir, std::string_view levelName,
                std::string* error) {
  const std::string mountPoint = "Levels/" + std::string(levelName);
  const std::filesystem::path dir = modDir / "Levels" / std::string(levelName);

  int mounted = 0;
  for (const char* archive : {"server.zip", "client.zip"}) {
    std::string archiveError;
    if (files.mountArchive(dir / archive, mountPoint, &archiveError)) {
      ++mounted;
    } else if (error != nullptr && error->empty()) {
      *error = archiveError;
    }
  }
  return mounted > 0;
}

std::optional<Level> loadLevel(FileSystem& files, std::string_view levelName, std::string* error) {
  Level level;
  level.name = std::string(levelName);

  const std::string base = "Levels/" + std::string(levelName);
  LevelBuilder builder(level, files);
  con::Interpreter interpreter(files, [&](const con::Command& command) { builder(command); });

  // Init.con with no arguments is the game's own load, and it is the whole of
  // it: its `else` branch runs Heightdata, Terrain, Sky, CompiledRoads, the
  // overgrowth and its collision, the ambient objects and Water, in that order.
  // We used to run those files ourselves with a `BF2Editor` argument, which
  // took the editor's branch everywhere — the terrain out of the editor's loose
  // maps rather than the compiled blob, the light colours under their editor
  // names, and 350 `run` lines loading object templates we already have.
  interpreter.runFile(base + "/Init.con");

  // The one file the chain does not name. In the original it is reached through
  // `run tmp.con`, and `tmp.con` in the archives is zero bytes — the game writes
  // that list itself at load time, so ours is the file the editor saved.
  interpreter.runFile(base + "/StaticObjects.con");

  // The road templates: they hold the textures, and they live in the game's
  // objects rather than in the level.
  //
  // This is the one place the editor's argument is still passed, and it has to
  // be: every one of the eighty files under `Roads/Splines` is a single
  // `if v_arg1 == BF2Editor` around its whole body, so with no argument they
  // define nothing at all and the roads come out white. Where the game reads
  // them from instead is not established — a road's compiled geometry
  // (CompiledRoads.con) names the template and carries no texture.
  const std::vector<std::string> editorArgs{"BF2Editor"};
  for (const std::string& path : files.list("objects/roads/splines")) {
    if (assetExtension(path) == "con") interpreter.runFile(path, editorArgs);
  }

  // The roads' geometry is read separately: the .con holds only file paths.
  for (Road& road : level.roads) {
    const auto bytes = files.read(road.meshPath);
    if (!bytes) continue;
    if (auto geometry = loadRoadMesh(*bytes)) {
      road.geometry = std::move(*geometry);
      // The textures come by the template's name from CompiledRoads.con.
      // Both of them: the shader mixes the markings over the tiling surface.
      const auto texture = level.roadTextures.find(road.templateName);
      if (texture != level.roadTextures.end() && !road.geometry.ranges.empty()) {
        road.geometry.ranges[0].maps.push_back(texture->second.primary);
        road.geometry.ranges[0].maps.push_back(texture->second.secondary);
        road.blendFactor = texture->second.blendFactor;
      }
    }
  }

  if (!loadHeights(files, level, error)) return std::nullopt;
  return level;
}

std::optional<mesh::RenderMesh> loadRoadMesh(std::span<const std::byte> bytes,
                                             std::string* error) {
  auto fail = [error](const char* why) -> std::optional<mesh::RenderMesh> {
    if (error) *error = why;
    return std::nullopt;
  };

  constexpr std::size_t kHeaderBytes = 52;
  constexpr std::size_t kVertexStride = 32;
  if (bytes.size() < kHeaderBytes + 4) return fail("file too small for a road");

  auto readU32 = [&](std::size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
  };
  auto readFloat = [&](std::size_t at) {
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
  };

  const std::uint32_t vertexCount = readU32(48);
  const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * kVertexStride;
  if (vertexCount == 0 || kHeaderBytes + vertexBytes + 4 > bytes.size()) {
    return fail("the road's vertices are truncated");
  }

  // The positions in the file are relative to start; the offset is added by
  // whoever places the road into the world, together with absolutePosition.
  mesh::RenderMesh out;
  out.vertices.resize(vertexCount);
  for (std::uint32_t i = 0; i < vertexCount; ++i) {
    const std::size_t at = kHeaderBytes + static_cast<std::size_t>(i) * kVertexStride;
    mesh::Vertex& vertex = out.vertices[i];
    vertex.position = {readFloat(at), readFloat(at + 4), readFloat(at + 8)};
    // A road lies on the ground, so the normal points up — there is none in the file.
    vertex.normal = {0.0f, 1.0f, 0.0f};
    vertex.uv[0] = readFloat(at + 12);
    vertex.uv[1] = readFloat(at + 16);
    // The second UV set and the edge alpha, which we used to read past. The
    // tiling surface is sampled with the second set and the alpha is what
    // fades a road's edge into the terrain (`Road.fx:61`, `Road.fx:80`).
    vertex.uv2[0] = readFloat(at + 20);
    vertex.uv2[1] = readFloat(at + 24);
    vertex.alpha = readFloat(at + 28);
  }

  const std::size_t indexOffset = kHeaderBytes + vertexBytes;
  const std::uint32_t indexCount = readU32(indexOffset);
  if (indexOffset + 4 + static_cast<std::size_t>(indexCount) * 2 > bytes.size()) {
    return fail("the road's indices are truncated");
  }

  out.indices.resize(indexCount);
  for (std::uint32_t i = 0; i < indexCount; ++i) {
    std::uint16_t index = 0;
    std::memcpy(&index, bytes.data() + indexOffset + 4 + i * 2, sizeof(index));
    if (index >= vertexCount) return fail("a road index is outside the buffer");
    out.indices[i] = index;
  }

  mesh::Aabb bounds{};
  for (std::size_t i = 0; i < out.vertices.size(); ++i) {
    const auto& p = out.vertices[i].position;
    if (i == 0) {
      bounds.min = bounds.max = p;
    } else {
      bounds.min = {std::min(bounds.min.x, p.x), std::min(bounds.min.y, p.y),
                    std::min(bounds.min.z, p.z)};
      bounds.max = {std::max(bounds.max.x, p.x), std::max(bounds.max.y, p.y),
                    std::max(bounds.max.z, p.z)};
    }
  }
  out.bounds = bounds;

  mesh::DrawRange range;
  range.indexCount = indexCount;
  out.ranges.push_back(std::move(range));
  return out;
}

std::string lowDetailTexturePath(const Level& level, const FileSystem& files) {
  // The engine builds the name from the level's own directory and falls back to
  // one texture shipped with the game (`RendDX9.dll`, 0x1010b380):
  //
  //   GLGameLevelPath + "/lowDetailTexture.dds"
  //   "common/terrain/textures/Default.dds"    when the level has none
  const std::string own = "Levels/" + level.name + "/lowdetailtexture.dds";
  if (files.exists(own)) return own;
  const std::string fallback = "common/terrain/textures/Default.dds";
  if (files.exists(fallback)) return fallback;
  return {};
}

std::vector<TerrainPatch> buildTerrainPatches(const Level& level, const FileSystem& files) {
  std::vector<TerrainPatch> patches;
  const int size = level.primary.size;
  const int patchSize = level.terrain.patchSize > 0 ? level.terrain.patchSize : 128;
  if (size <= 1) return patches;

  // 1025 nodes = 1024 quads = 8 patches of 128. The last node is shared with the
  // neighbouring patch, otherwise gaps would be left between them.
  const int patchCount = (size - 1) / patchSize;

  for (int row = 0; row < patchCount; ++row) {
    for (int column = 0; column < patchCount; ++column) {
      TerrainPatch patch;
      patch.column = column;
      patch.row = row;

      char name[64];
      std::snprintf(name, sizeof(name), "%02dx%02d.dds", column, row);
      patch.colormap = level.terrain.colormapBase + name;

      // For patches under water the game simply has no colour map — it does not
      // draw them either. We skip them; the water plane covers the sea.
      if (!files.exists(patch.colormap)) continue;

      const int x0 = column * patchSize;
      const int z0 = row * patchSize;
      const int vertexCount = patchSize + 1;

      patch.geometry.vertices.reserve(static_cast<std::size_t>(vertexCount) * vertexCount);
      for (int z = 0; z < vertexCount; ++z) {
        for (int x = 0; x < vertexCount; ++x) {
          const int gx = x0 + x;
          const int gz = z0 + z;

          mesh::Vertex vertex;
          vertex.position = {level.worldX(gx), level.heightAt(gx, gz), level.worldZ(gz)};

          // The normal from the neighbouring nodes: a central difference on both axes.
          const float left = level.heightAt(gx - 1, gz);
          const float right = level.heightAt(gx + 1, gz);
          const float up = level.heightAt(gx, gz - 1);
          const float down = level.heightAt(gx, gz + 1);
          const Vec3f normal = normalize(Vec3f{left - right, 2.0f * level.primary.scale.x,
                                               up - down});
          vertex.normal = {normal.x, normal.y, normal.z};

          // The colour map is stretched over the whole patch.
          vertex.uv[0] = static_cast<float>(x) / static_cast<float>(patchSize);
          vertex.uv[1] = static_cast<float>(z) / static_cast<float>(patchSize);

          patch.geometry.vertices.push_back(vertex);
        }
      }

      patch.geometry.indices.reserve(static_cast<std::size_t>(patchSize) * patchSize * 6);
      for (int z = 0; z < patchSize; ++z) {
        for (int x = 0; x < patchSize; ++x) {
          const std::uint32_t topLeft = static_cast<std::uint32_t>(z * vertexCount + x);
          const std::uint32_t topRight = topLeft + 1;
          const std::uint32_t bottomLeft = topLeft + static_cast<std::uint32_t>(vertexCount);
          const std::uint32_t bottomRight = bottomLeft + 1;

          // Counter-clockwise winding — the same as in the game's meshes.
          patch.geometry.indices.push_back(topLeft);
          patch.geometry.indices.push_back(bottomLeft);
          patch.geometry.indices.push_back(topRight);

          patch.geometry.indices.push_back(topRight);
          patch.geometry.indices.push_back(bottomLeft);
          patch.geometry.indices.push_back(bottomRight);
        }
      }

      // The same patch's baked lighting — the second texture layer.
      if (!level.terrain.lightmapBase.empty()) {
        const std::string candidate = level.terrain.lightmapBase + name;
        if (files.exists(candidate)) patch.lightmap = candidate;
      }

      // How much of the level's low-detail texture shows through on this patch.
      // The colour map has only ~2 texels per metre, so without the tiling
      // texture the ground is a blur close up; this map says where it shows.
      if (!level.terrain.lowDetailmapBase.empty()) {
        const std::string candidate = level.terrain.lowDetailmapBase + name;
        if (files.exists(candidate)) patch.lowDetailmap = candidate;
      }

      // The chart maps: which of the level's six terrain materials owns which
      // texel of this patch. Three channels each, and the six add up to one.
      // A patch whose ground is one of the first three everywhere ships no
      // `_2` — four of Karkand's sixteen do not.
      if (!level.terrain.detailmapBase.empty()) {
        char detailName[64];
        std::snprintf(detailName, sizeof(detailName), "%02dx%02d_1.dds", column, row);
        std::string candidate = level.terrain.detailmapBase + detailName;
        if (files.exists(candidate)) patch.detailmap = candidate;
        std::snprintf(detailName, sizeof(detailName), "%02dx%02d_2.dds", column, row);
        candidate = level.terrain.detailmapBase + detailName;
        if (files.exists(candidate)) patch.detailmap2 = candidate;
      }

      // The terrain's slots are ours and not the game's material system, so
      // they are fixed rather than packed: colour, baked light, the patch's
      // `lowComponent`, and the two chart maps. A patch missing one of them
      // leaves an empty path in its place — shifting the rest up would put a
      // chart map where the renderer looks for the light.
      mesh::DrawRange range;
      range.indexStart = 0;
      range.indexCount = static_cast<std::uint32_t>(patch.geometry.indices.size());
      range.lightmapInSecondSlot = true;
      range.maps.push_back(patch.colormap);
      range.maps.push_back(patch.lightmap);
      range.maps.push_back(patch.lowDetailmap);
      range.maps.push_back(patch.detailmap);
      range.maps.push_back(patch.detailmap2);
      patch.geometry.ranges.push_back(std::move(range));

      patches.push_back(std::move(patch));
    }
  }
  return patches;
}

std::optional<mesh::RenderMesh> buildSkyDome(const Level& level, const FileSystem& files) {
  if (level.sky.domeTemplate.empty()) return std::nullopt;

  // Where the template lives is named by the level's own Sky.con:
  //
  //   run /Common/Sky/SkyDome/skydome.con
  //   Skydome.skyTemplate skydome
  //
  // and its geometry sits beside it in `Meshes/`, the way every other
  // ObjectTemplate's does.
  const std::string path =
      "common/sky/" + level.sky.domeTemplate + "/meshes/" + level.sky.domeTemplate + ".staticmesh";
  const auto bytes = files.read(path);
  if (!bytes) return std::nullopt;
  const auto parsed = mesh::load(*bytes, mesh::Kind::Static);
  if (!parsed) return std::nullopt;
  auto dome = mesh::extract(*parsed, 0, 0);
  if (!dome || dome->ranges.empty()) return std::nullopt;

  // The mesh carries a default sky (`skyclear01.dds`); the level replaces it
  // through `Skydome.skyTexture`. Slot 0 is the base colour of its
  // `BaseDetail` material, so that is the one to swap — leaving the detail
  // behind would multiply the sky by a cloud tile it was not meant to have.
  if (!level.sky.texture.empty()) {
    for (mesh::DrawRange& range : dome->ranges) {
      range.maps.assign(1, level.sky.texture);
    }
  }
  return dome;
}

mesh::RenderMesh buildWaterPlane(const Level& level) {
  const float extent = level.halfExtent() * level.primary.scale.x;
  const float y = level.terrain.seaLevel;

  mesh::RenderMesh water;
  water.vertices = {
      mesh::Vertex{{-extent, y, -extent}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      mesh::Vertex{{extent, y, -extent}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
      mesh::Vertex{{-extent, y, extent}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
      mesh::Vertex{{extent, y, extent}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
  };
  water.indices = {0, 2, 1, 1, 2, 3};
  water.bounds = mesh::Aabb{{-extent, y, -extent}, {extent, y, extent}};

  mesh::DrawRange range;
  range.indexCount = static_cast<std::uint32_t>(water.indices.size());
  range.maps.push_back(kWaterColorMap);
  water.ranges.push_back(std::move(range));
  return water;
}

}  // namespace obf2::level

namespace obf2::level {

float Level::groundHeightAt(const Vec3f& position) const {
  if (heights.empty()) return 0.0f;

  const float half = halfExtent();
  const float gx = position.x / primary.scale.x + half;
  const float gz = position.z / primary.scale.z + half;

  const int x0 = static_cast<int>(std::floor(gx));
  const int z0 = static_cast<int>(std::floor(gz));
  const float tx = gx - static_cast<float>(x0);
  const float tz = gz - static_cast<float>(z0);

  const float h00 = heightAt(x0, z0);
  const float h10 = heightAt(x0 + 1, z0);
  const float h01 = heightAt(x0, z0 + 1);
  const float h11 = heightAt(x0 + 1, z0 + 1);

  const float top = h00 + (h10 - h00) * tx;
  const float bottom = h01 + (h11 - h01) * tx;
  return top + (bottom - top) * tz;
}

}  // namespace obf2::level
