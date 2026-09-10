// openbf2 — the engine's entry point.
//
//   openbf2 [--mod <path>] [--mesh <path in the VFS>]  — one mesh
//           [--object <template name>]                 — an assembled vehicle
//           [--level <level name>]                     — a whole level
//           [--geom N] [--lod N] [--frames N] [--screenshot file.bmp]
//
// The game's data is read straight out of the archives, as fileManager does in Refractor 2.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "obf2/core/math.h"
#include "obf2/core/parallel.h"
#include "obf2/core/path.h"
#include "obf2/core/platform.h"
#include "obf2/engine/engine.h"
#include "obf2/font/text.h"
#include "obf2/hud/bottom_left.h"
#include "obf2/hud/ingame.h"
#include "obf2/hud/kit_list.h"
#include "obf2/hud/map_node.h"
#include "obf2/meme/graph.h"
#include "obf2/hud/render.h"
#if OBF2_HAVE_FLASH
#include "obf2/flash/movie.h"
#endif
#include "obf2/hud/spawn.h"
#include "obf2/hud/spawn_interface.h"
#include "obf2/hud/states.h"
#include "obf2/game/controls.h"
#include "obf2/game/scene.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"
#include "obf2/level/lightmap_atlas.h"
#include "obf2/server/game_client.h"
#include "obf2/server/physics.h"
#include "obf2/server/soldier_move.h"
#include <set>

#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_world.h"
#include "obf2/net/bf2_join.h"
#include "obf2/net/md5.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/udp.h"
#include "obf2/server/game_server.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/material.h"
#include "obf2/mesh/collision.h"
#include "obf2/mesh/primitives.h"
#include "obf2/mesh/skinning.h"
#include "obf2/server/collision_world.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

namespace {

struct Args {
  std::filesystem::path modDir = "Game Files/mods/bf2";
  std::string meshPath;  // empty -> an ordinary engine run
  std::string objectName;
  std::string levelName;
  int geometryIndex = -1;  // -1 = choose by the number of lods
  int lodIndex = 0;
  int frames = 0;  // 0 = run until the window is closed
  std::string screenshot;
  std::string screen;  // menu | loading — for deterministic screenshots
  bool hosted = false; // --hosted: the world comes from a local server
  std::optional<obf2::Vec3f> focus;  // where the camera looks
  float mouseX = -1.0f, mouseY = -1.0f;  // --mouse: place the cursor for a screenshot
  // --topdown: strictly from above, +X to the right, -Z up. Needed to check the
  // world's orientation against the level's own minimap.
  bool topDown = false;
  // A camera put where we say and pointed where we say, and a way to take the
  // interface off the picture. Both are for looking: comparing a frame of ours
  // against the original means standing in the same spot, and the HUD covers
  // most of what is being compared. `tools/bf2_run.sh` puts the original at the
  // same place.
  std::optional<obf2::Vec3f> camera;
  float cameraYaw = 0.0f;    // degrees; 0 looks along +Z, as the engine counts
  float cameraPitch = 0.0f;  // degrees; positive is up
  bool noHud = false;
  // --no-lightmaps: draw without the levels' baked light maps. For telling a
  // wrong light map apart from a dark one — the two look alike on screen and
  // only an A/B says which.
  bool noLightmaps = false;
  // --own-box: draw a placeholder at our own soldier's position too. It is not
  // needed in the game itself, but without it the other players' placeholder
  // cannot be checked on an empty server.
  bool showOwnBox = false;
  // --mouse-scale: how many axis units one mouse pixel gives. The link
  // "pixels -> axis" lives in the client's `ControlMap` and is **not reversed
  // yet**, so this number is NOT measured — it plays the role of sensitivity and
  // stays a setting. Everything after it (axis -> angle, axis -> wire) already
  // comes from the binary, so the camera and the server do not diverge whatever it is.
  float mouseScale = 0.02f;
  // --record <file>: save everything the server sent in the same format
  // `loadCapture` reads (u32 length, then the bytes). Captured traffic is the
  // only thing the protocol parsing can be checked against by a test.
  std::string recordTo;
  // --exec "<line>": run a console command as soon as the spawn screen is ready.
  // This screen's buttons have no logic of their own anyway — they run console
  // commands (`setButtonNodeConCmd`), so this is the same path a click takes,
  // only without pointing the mouse at a pixel. The flag may be repeated.
  //
  std::vector<std::string> execLines;
  // --flash <file.swf>: show the menu's movie instead of our screen.
  // The game's menu is Flash, played by Ruffle (docs/research/11-ruffle-menu.md).
  std::string flashSwf;
  std::string connectTo;      // --connect <host[:port]>: a real BF2 server
  // --probe: protocol parsing only, without a window. Without it --connect opens
  // the world, as a client should.
  bool probe = false;
  // --no-content: skip the content check. Needed to find out whether it is what
  // makes the server break the connection.
  bool skipContent = false;
  bool skipDatabase = false;
  bool startSimulation = false;
  bool blockReady = false;
  std::string connectPassword;
  std::string playerName = "OpenBF2";  // --name: the name we join under
  std::string calibrate;               // --calibrate <file>: match template numbers
  // --ordinal: the line number in the fingerprint files. The server picks it when
  // it loads the level; where a real client learns it has not been found yet, so
  // for now we set it by hand.
  // -1 = take it from the block the server sends.
  int ordinal = -1;
  int team = 1;                        // --team, --kit, --group: the spawn choice
  int kit = 0;
  int spawnGroup = 1;
  std::vector<std::string> animationPaths;  // --anim: several are allowed, they blend
  std::string skeletonPath;    // --skeleton: a .ske; the soldier's skeleton by default
  int frame = 0;               // --frame: which frame to show
  bool click = false;  // --click: one click at the --mouse position
  // --click-at <frame>:<x>:<y> — clicks on a schedule, several are allowed.
  // The menu leads the player through several steps (pick a profile -> log in),
  // and one click is not enough to check it.
  struct ScheduledClick {
    int frame;
    float x, y;
  };
  std::vector<ScheduledClick> clicks;
  // --hud-screen <group>: show a screen normally visible only while a key is
  // held. Needed for screenshots and for checking by eye.
  std::string hudScreenName;
  // --hud-rects: write out the rectangles of every node drawn. The format is the
  // same as in the original's frame dump, so they can be compared
  // (tools/hud_coverage.py).
  bool hudRects = false;
  float distance = 0.0f;             // 0 = choose it from the bounds
  // The game is made for 4:3, and for now we keep to that: 1600x1200 is exactly
  // twice the base 800x600, so the HUD lands with nothing left over.
  // A wide screen will be a separate job.
  int width = 1600;
  int height = 1200;
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--mod" && i + 1 < argc) args.modDir = argv[++i];
    else if (flag == "--mesh" && i + 1 < argc) args.meshPath = argv[++i];
    else if (flag == "--object" && i + 1 < argc) args.objectName = argv[++i];
    else if (flag == "--level" && i + 1 < argc) args.levelName = argv[++i];
    else if (flag == "--geom" && i + 1 < argc) args.geometryIndex = std::atoi(argv[++i]);
    else if (flag == "--lod" && i + 1 < argc) args.lodIndex = std::atoi(argv[++i]);
    else if (flag == "--frames" && i + 1 < argc) args.frames = std::atoi(argv[++i]);
    else if (flag == "--screenshot" && i + 1 < argc) args.screenshot = argv[++i];
    else if (flag == "--screen" && i + 1 < argc) args.screen = argv[++i];
    else if (flag == "--hosted") args.hosted = true;
    else if (flag == "--topdown") args.topDown = true;
    else if (flag == "--no-hud") args.noHud = true;
    else if (flag == "--no-lightmaps") args.noLightmaps = true;
    else if (flag == "--camera" && i + 1 < argc) {
      obf2::con::Command command;
      command.args.emplace_back(argv[++i]);
      if (const auto point = command.argVec3(0)) {
        args.camera = obf2::Vec3f{point->x, point->y, point->z};
      }
    }
    else if (flag == "--angles" && i + 2 < argc) {
      args.cameraYaw = static_cast<float>(std::atof(argv[++i]));
      args.cameraPitch = static_cast<float>(std::atof(argv[++i]));
    }
    else if (flag == "--own-box") args.showOwnBox = true;
    else if (flag == "--flash" && i + 1 < argc) args.flashSwf = argv[++i];
    else if (flag == "--connect" && i + 1 < argc) args.connectTo = argv[++i];
    else if (flag == "--probe") args.probe = true;
    else if (flag == "--no-content") args.skipContent = true;
    else if (flag == "--no-database") args.skipDatabase = true;
    else if (flag == "--start-sim") args.startSimulation = true;
    else if (flag == "--block-ready") args.blockReady = true;
    else if (flag == "--width" && i + 1 < argc) args.width = std::atoi(argv[++i]);
    else if (flag == "--height" && i + 1 < argc) args.height = std::atoi(argv[++i]);
    else if (flag == "--hud-screen" && i + 1 < argc) args.hudScreenName = argv[++i];
    else if (flag == "--hud-rects") args.hudRects = true;
    else if (flag == "--connect-password" && i + 1 < argc) args.connectPassword = argv[++i];
    else if (flag == "--name" && i + 1 < argc) args.playerName = argv[++i];
    else if (flag == "--calibrate" && i + 1 < argc) args.calibrate = argv[++i];
    else if (flag == "--ordinal" && i + 1 < argc) args.ordinal = std::atoi(argv[++i]);
    else if (flag == "--record" && i + 1 < argc) args.recordTo = argv[++i];
    else if (flag == "--exec" && i + 1 < argc) args.execLines.emplace_back(argv[++i]);
    else if (flag == "--mouse-scale" && i + 1 < argc) args.mouseScale = std::atof(argv[++i]);
    else if (flag == "--team" && i + 1 < argc) args.team = std::atoi(argv[++i]);
    else if (flag == "--kit" && i + 1 < argc) args.kit = std::atoi(argv[++i]);
    else if (flag == "--group" && i + 1 < argc) args.spawnGroup = std::atoi(argv[++i]);
    else if (flag == "--anim" && i + 1 < argc) args.animationPaths.emplace_back(argv[++i]);
    else if (flag == "--skeleton" && i + 1 < argc) args.skeletonPath = argv[++i];
    else if (flag == "--frame" && i + 1 < argc) args.frame = std::atoi(argv[++i]);
    else if (flag == "--click") args.click = true;
    else if (flag == "--click-at" && i + 1 < argc) {
      // "frame:x:y" — three numbers separated by colons.
      const std::string text = argv[++i];
      const std::size_t first = text.find(':');
      const std::size_t second = text.find(':', first == std::string::npos ? 0 : first + 1);
      if (first == std::string::npos || second == std::string::npos) {
        std::fprintf(stderr, "--click-at expects <frame>:<x>:<y>, not %s\n", text.c_str());
        continue;
      }
      args.clicks.push_back({std::atoi(text.substr(0, first).c_str()),
                             static_cast<float>(std::atof(text.substr(first + 1, second - first - 1).c_str())),
                             static_cast<float>(std::atof(text.substr(second + 1).c_str()))});
    }
    else if (flag == "--mouse" && i + 2 < argc) {
      args.mouseX = static_cast<float>(std::atof(argv[++i]));
      args.mouseY = static_cast<float>(std::atof(argv[++i]));
    }
    else if (flag == "--dist" && i + 1 < argc) args.distance = static_cast<float>(std::atof(argv[++i]));
    else if (flag == "--focus" && i + 1 < argc) {
      // The format is the game's: x/y/z
      obf2::con::Command command;
      command.args.emplace_back(argv[++i]);
      if (const auto point = command.argVec3(0)) {
        args.focus = obf2::Vec3f{point->x, point->y, point->z};
      }
    }
  }
  return args;
}

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// A vehicle's .bundledmesh holds several geoms: the cockpit view, the outside
// view and the wreckage. There is no explicit marker in the file, but there is a
// reliable sign: **the cockpit view has exactly one lod** — the player is always
// close, so it needs no detail chain, while the outside view has 3-4 levels.
std::size_t pickGeometry(const obf2::mesh::Mesh& mesh, std::size_t lodIndex) {
  std::size_t best = 0;
  std::size_t bestLods = 0;
  std::size_t bestTriangles = 0;

  for (std::size_t g = 0; g < mesh.geometries.size(); ++g) {
    const auto& lods = mesh.geometries[g].lods;
    if (lodIndex >= lods.size()) continue;

    std::size_t triangles = 0;
    for (const auto& material : lods[lodIndex].materials) triangles += material.indexCount / 3;

    if (lods.size() > bestLods || (lods.size() == bestLods && triangles > bestTriangles)) {
      bestLods = lods.size();
      bestTriangles = triangles;
      best = g;
    }
  }
  return best;
}

std::optional<obf2::mesh::RenderMesh> loadMesh(obf2::FileSystem& files, const std::string& path,
                                               int geometryOverride, int lodIndex, bool verbose) {
  const std::string normalized = obf2::normalizeAssetPath(path);
  const auto kind = obf2::mesh::kindFromExtension(obf2::assetExtension(normalized));
  if (!kind) return std::nullopt;

  const auto bytes = files.read(normalized);
  if (!bytes) {
    if (verbose) std::fprintf(stderr, "mesh not found in the VFS: %s\n", normalized.c_str());
    return std::nullopt;
  }

  std::string error;
  const auto parsed = obf2::mesh::load(*bytes, *kind, &error);
  if (!parsed) {
    if (verbose) std::fprintf(stderr, "could not parse %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  const std::size_t lod = static_cast<std::size_t>(std::max(0, lodIndex));
  const std::size_t geometry = geometryOverride >= 0 ? static_cast<std::size_t>(geometryOverride)
                                                     : pickGeometry(*parsed, lod);

  auto render = obf2::mesh::extract(*parsed, geometry, lod, &error);
  if (!render) {
    if (verbose) std::fprintf(stderr, "could not unpack %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  // Whether this mesh is vegetation is decided by its path, and by nothing
  // else — that is the engine's own test (`obf2::mesh::isVegetationPath`).
  obf2::mesh::markVegetationLeaves(*render, normalized);

  if (verbose) {
    std::printf("mesh: %s\n  version %u, geom %zu/%zu, lod %zu, vertices %zu, triangles %zu, "
                "materials %zu\n",
                normalized.c_str(), parsed->header.version, geometry, parsed->geometries.size(),
                lod, render->vertices.size(), render->indices.size() / 3, render->ranges.size());
  }
  return render;
}

// A geometry name from an ObjectTemplate is not a path. The file lies next to the
// .con, in a meshes subdirectory: objects/vehicles/land/apc_btr90/meshes/apc_btr90.bundledmesh.
std::string resolveGeometryPath(obf2::FileSystem& files, const std::string& templateFile,
                                const std::string& geometryName) {
  if (geometryName.empty()) return {};
  const std::string_view dir = obf2::assetParentDir(templateFile);
  for (const char* extension : {".staticmesh", ".bundledmesh", ".skinnedmesh"}) {
    for (const char* subdirectory : {"meshes/", ""}) {
      const std::string candidate =
          obf2::joinAssetPath(dir, std::string(subdirectory) + geometryName + extension);
      if (files.exists(candidate)) return candidate;
    }
  }
  return {};
}

// The collision mesh lies next to the visible one, in the same meshes subdirectory.
std::string resolveCollisionPath(obf2::FileSystem& files, const std::string& templateFile,
                                 const std::string& name) {
  if (name.empty()) return {};
  const std::string_view dir = obf2::assetParentDir(templateFile);
  for (const char* subdirectory : {"meshes/", ""}) {
    const std::string candidate =
        obf2::joinAssetPath(dir, std::string(subdirectory) + name + ".collisionmesh");
    if (files.exists(candidate)) return candidate;
  }
  return {};
}

// Runs every .con/.tweak of the game through the interpreter and collects the template registry.
obf2::game::Registry buildRegistry(obf2::FileSystem& files) {
  obf2::game::Registry registry;
  std::vector<std::string> configs;
  for (auto& path : files.list()) {
    const std::string_view extension = obf2::assetExtension(path);
    if (extension == "con" || extension == "tweak") configs.push_back(std::move(path));
  }
  std::sort(configs.begin(), configs.end());
  configs.erase(std::unique(configs.begin(), configs.end()), configs.end());

  // Reading the files is the expensive half — unpacking each out of its archive
  // and taking it apart — and every file is read on its own, so that half is
  // done on every core. Feeding the registry is not: a `.tweak` changes the
  // template a `.con` created, so the commands go in one at a time and in the
  // order the sorted list gives, whatever order the threads finished in.
  //
  // A chunk at a time rather than all at once: the whole corpus is millions of
  // commands and holding them would cost more memory than the level.
  constexpr std::size_t kChunk = 512;
  std::vector<std::vector<obf2::con::Command>> parsed(kChunk);
  for (std::size_t start = 0; start < configs.size(); start += kChunk) {
    const std::size_t count = std::min(kChunk, configs.size() - start);
    obf2::parallelFor(count, [&](std::size_t i) {
      std::vector<obf2::con::Command>& into = parsed[i];
      into.clear();
      obf2::con::Interpreter interpreter(
          files, [&into](const obf2::con::Command& command) { into.push_back(command); });
      interpreter.runFile(configs[start + i]);
    });
    for (std::size_t i = 0; i < count; ++i) {
      for (const obf2::con::Command& command : parsed[i]) registry.feed(command);
    }
  }
  return registry;
}

// Template -> assembled geometry: the child tree plus the placement of the
// BundledMesh's parts by geometryPart.
// Adds a mesh transformed by a matrix to the target. The indices are shifted by
// the vertices already present, the materials' ranges by the indices already present.
void appendMesh(obf2::mesh::RenderMesh& target, const obf2::mesh::RenderMesh& source,
                const obf2::Mat4& transform) {
  const auto vertexBase = static_cast<std::uint32_t>(target.vertices.size());
  const auto indexBase = static_cast<std::uint32_t>(target.indices.size());

  for (const auto& vertex : source.vertices) {
    obf2::mesh::Vertex moved = vertex;
    const obf2::Vec3f at = transformPoint(
        transform, obf2::Vec3f{vertex.position.x, vertex.position.y, vertex.position.z});
    moved.position = {at.x, at.y, at.z};
    // The normals are rotated without translation: these transforms have no
    // scale, so an ordinary multiplication by the upper 3x3 block is enough.
    const obf2::Vec3f n = transformDirection(
        transform, obf2::Vec3f{vertex.normal.x, vertex.normal.y, vertex.normal.z});
    moved.normal = {n.x, n.y, n.z};
    target.vertices.push_back(moved);
  }
  for (const auto index : source.indices) target.indices.push_back(index + vertexBase);
  for (auto range : source.ranges) {
    range.indexStart += indexBase;
    target.ranges.push_back(std::move(range));
  }
}

// Template -> assembled geometry: the child tree plus the placement of the
// BundledMesh's parts by geometryPart.
//
// When the root has no mesh of its own, the object is assembled from the
// children's meshes. Control points are built that way: the template itself has
// no geometry, and the flag arrives through `ObjectTemplate.addTemplate flagpole`.
std::optional<obf2::mesh::RenderMesh> buildObjectMesh(obf2::FileSystem& files,
                                                      const obf2::game::Registry& registry,
                                                      const std::string& templateName,
                                                      const Args& args, bool verbose) {
  const auto* root = registry.find(templateName);
  if (root == nullptr) return std::nullopt;

  const auto instance = obf2::game::flattenObject(registry, templateName);
  if (!instance) return std::nullopt;

  if (instance->geometryName.empty()) {
    obf2::mesh::RenderMesh merged;
    int added = 0;
    for (const auto& part : instance->parts) {
      if (part.geometryName.empty()) continue;
      const std::string path = resolveGeometryPath(files, part.file, part.geometryName);
      if (path.empty()) continue;
      const auto piece = loadMesh(files, path, args.geometryIndex, args.lodIndex, false);
      if (!piece) continue;
      appendMesh(merged, *piece, part.transform);
      ++added;
    }
    if (added == 0) return std::nullopt;
    // The bounds are computed by us: the meshes came from different files, and
    // each brought its own, in its own coordinates.
    if (!merged.vertices.empty()) {
      merged.bounds.min = merged.bounds.max = merged.vertices.front().position;
      for (const auto& vertex : merged.vertices) {
        merged.bounds.min.x = std::min(merged.bounds.min.x, vertex.position.x);
        merged.bounds.min.y = std::min(merged.bounds.min.y, vertex.position.y);
        merged.bounds.min.z = std::min(merged.bounds.min.z, vertex.position.z);
        merged.bounds.max.x = std::max(merged.bounds.max.x, vertex.position.x);
        merged.bounds.max.y = std::max(merged.bounds.max.y, vertex.position.y);
        merged.bounds.max.z = std::max(merged.bounds.max.z, vertex.position.z);
      }
    }
    if (verbose) {
      std::printf("  root without geometry: assembled from %d child meshes, vertices %zu\n", added,
                  merged.vertices.size());
    }
    return merged;
  }

  const std::string path = resolveGeometryPath(files, root->file, instance->geometryName);
  if (path.empty()) return std::nullopt;

  auto render = loadMesh(files, path, args.geometryIndex, args.lodIndex, verbose);
  if (!render) return std::nullopt;

  const auto transforms = obf2::game::partTransformMap(*instance);
  const std::size_t moved = obf2::game::applyPartTransforms(*render, transforms);
  if (verbose) {
    std::printf("  nodes in the tree: %zu, depth: %d, cycles: %d\n"
                "  parts with a transform: %zu, vertices moved: %zu of %zu\n",
                instance->parts.size(), instance->maxDepth, instance->cycles, transforms.size(),
                moved, render->vertices.size());
  }
  return render;
}

// A full-screen rectangle in NDC coordinates: with an identity matrix the vertex
// shader leaves them as they are.
//
// The normal is set exactly along the light source from the fragment shader —
// then the half-Lambert factor equals one and the picture comes out without
// darkening. A temporary trick: as soon as there is a separate pipeline for the
// interface it will become unnecessary.
obf2::mesh::RenderMesh buildScreenQuad(const std::string& imagePath) {
  const obf2::Vec3f light = obf2::normalize(obf2::Vec3f{0.4f, 0.9f, 0.35f});
  const obf2::mesh::Vec3 normal{light.x, light.y, light.z};

  obf2::mesh::RenderMesh quad;
  quad.vertices = {
      obf2::mesh::Vertex{{-1.0f, -1.0f, 0.0f}, normal, {0.0f, 1.0f}},
      obf2::mesh::Vertex{{1.0f, -1.0f, 0.0f}, normal, {1.0f, 1.0f}},
      obf2::mesh::Vertex{{-1.0f, 1.0f, 0.0f}, normal, {0.0f, 0.0f}},
      obf2::mesh::Vertex{{1.0f, 1.0f, 0.0f}, normal, {1.0f, 0.0f}},
  };
  quad.indices = {0, 1, 2, 2, 1, 3};

  obf2::mesh::DrawRange range;
  range.indexCount = static_cast<std::uint32_t>(quad.indices.size());
  if (!imagePath.empty()) range.maps.push_back(imagePath);
  quad.ranges.push_back(std::move(range));
  return quad;
}

// The font: a pair of .dif (metrics) + .dds (atlas) from Fonts_client.zip.
struct LoadedFont {
  obf2::font::Font font;
  std::string atlasPath;
  bool valid = false;
};

LoadedFont loadFont(obf2::FileSystem& files, const std::string& base) {
  LoadedFont out;
  const auto metrics = files.read(base + ".dif");
  if (!metrics) return out;

  const std::string text(reinterpret_cast<const char*>(metrics->data()), metrics->size());
  std::string error;
  auto parsed = obf2::font::parseDif(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "font %s: %s\n", base.c_str(), error.c_str());
    return out;
  }

  out.font = std::move(*parsed);
  out.atlasPath = base + ".dds";
  out.valid = files.exists(out.atlasPath);
  if (!out.valid) std::fprintf(stderr, "no font atlas: %s\n", out.atlasPath.c_str());
  return out;
}

// The main menu's background. In the game the menu is Flash, which the engine
// runs with its own player; its assets are ordinary PNGs, and those are what we take.
std::string findMenuBackground(obf2::FileSystem& files) {
  for (const char* candidate : {
           "menu/external/flashmenu/images/background/background_2.png",
           "menu/external/flashmenu/images/background/background_3.png",
           "menu/external/flashmenu/images/background/background_1.png",
       }) {
    if (files.exists(candidate)) return candidate;
  }
  return {};
}

}  // namespace

// One session: the menu or the game. Returns the exit code; if a level was chosen
// in the menu, its name lands in nextLevel and the main loop starts a session
// again — this time in the game.
// Everything we know about a level: a template's name and where it stands.
// The server sends objects without names, only numbers, so we recognise them by position.
// The soldier's movement constants from the game's data. Both paths need them:
// our own server and the movement prediction on a real one — otherwise our
// prediction would diverge from what the server computes.
obf2::server::PhysicsConstants loadPhysics(obf2::FileSystem& files) {
  obf2::server::PhysicsConstants constants;
  obf2::engine::Console console;
  constants.bind(console);
  obf2::con::Interpreter interpreter(files,
                                     [&](const obf2::con::Command& c) { console.execute(c); });
  interpreter.runFile("objects/soldiers/common/common.con");
  return constants;
}

// The collision world from the placed objects.
//
// Both paths need it: our own server, to move bodies, and the client on a real
// server, so that movement prediction does not fall through a building's floor.
// While it lived inside the `--hosted` branch, the client knew only the terrain.
std::unique_ptr<obf2::server::CollisionWorld> buildCollisionWorld(
    obf2::FileSystem& files, const obf2::game::Registry& registry,
    const std::vector<obf2::level::StaticObject>& objects) {
  auto world = std::make_unique<obf2::server::CollisionWorld>();
  int withCollision = 0, withoutCollision = 0;
  std::unordered_map<std::string, std::shared_ptr<obf2::mesh::CollisionMesh>> cache;

  for (const auto& object : objects) {
    auto cached = cache.find(object.templateName);
    if (cached == cache.end()) {
      std::shared_ptr<obf2::mesh::CollisionMesh> loaded;
      if (const auto* root = registry.find(object.templateName)) {
        // The collision mesh's name is a separate property of the template.
        const std::string_view name = root->text("collisionMesh");
        if (!name.empty()) {
          const std::string path = resolveCollisionPath(files, root->file, std::string(name));
          if (!path.empty()) {
            if (const auto bytes = files.read(path)) {
              if (auto mesh = obf2::mesh::loadCollisionMesh(*bytes)) {
                loaded = std::make_shared<obf2::mesh::CollisionMesh>(std::move(*mesh));
              }
            }
          }
        }
      }
      cached = cache.emplace(object.templateName, std::move(loaded)).first;
    }
    if (cached->second == nullptr) {
      ++withoutCollision;
      continue;
    }
    const auto* layer = cached->second->layer(obf2::mesh::ColType::Soldier);
    if (layer == nullptr) {
      ++withoutCollision;
      continue;
    }

    // Vegetation arrives as a ready matrix, the rest as a position with angles.
    obf2::Mat4 transform = object.transform;
    if (!object.hasTransform) {
      transform = obf2::translation(object.position);
      if (object.hasRotation) {
        transform = transform * obf2::rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                           object.rotation.z);
      }
    }
    world->addLayer(*layer, transform);
    ++withCollision;
  }

  std::printf("  collision: %d objects, %zu triangles in %zu cells (without geometry %d)\n",
              withCollision, world->triangleCount(), world->cellCount(), withoutCollision);
  return world;
}

struct KnownObject {
  std::string name;
  obf2::Vec3f position;
};

std::vector<KnownObject> buildKnownObjects(obf2::FileSystem& files, const std::string& levelName,
                                           std::string* error) {
  std::vector<KnownObject> known;
  if (const auto gameplay =
          obf2::level::loadGameplayObjects(files, levelName, "gpm_cq", 16, error)) {
    for (const auto& point : gameplay->controlPoints) {
      known.push_back({point.templateName, point.position});
    }
    for (const auto& spawner : gameplay->spawners) {
      known.push_back({spawner.templateName, spawner.position});
      // A spawner issues different vehicles depending on the team — every variant
      // stands in the same place.
      for (const auto& [team, name] : spawner.templateByTeam) {
        (void)team;
        known.push_back({name, spawner.position});
      }
    }
  }
  // The statics too: the server also sends destructible things such as barrels and tankers.
  if (const auto loaded = obf2::level::loadLevel(files, levelName, error)) {
    for (const auto& object : loaded->objects) {
      known.push_back({object.templateName, object.position});
    }
  }
  return known;
}

// Which known object stands at this position. The tolerance is deliberately
// narrow: both sides take the position from the same data, so the match has to be
// exact, and a wider tolerance would start inventing correspondences.
const KnownObject* nearestKnown(const std::vector<KnownObject>& known, const obf2::Vec3f& at,
                               float tolerance = 2.0f) {
  const KnownObject* best = nullptr;
  float bestDistance = 0.0f;
  for (const auto& candidate : known) {
    const float d = length(candidate.position - at);
    if (!best || d < bestDistance) {
      best = &candidate;
      bestDistance = d;
    }
  }
  return best && bestDistance < tolerance ? best : nullptr;
}

// At which step an object is lost on its way to the screen.
enum class DrawStage {
  Drawn,            // it arrived: the geometry is assembled
  NoTemplate,       // the template is not in the registry
  NoTree,           // the child tree did not assemble
  NoGeometryName,   // neither the root nor the children have geometry
  GeometryInChild,  // there is geometry, but in a child — we take only the root's
  NoGeometryFile,   // there is a name but the file was not found
  NoMesh,           // there is a file but the mesh did not parse
};

std::string_view drawStageName(DrawStage stage) {
  switch (stage) {
    case DrawStage::Drawn: return "drawn";
    case DrawStage::NoTemplate: return "no template";
    case DrawStage::NoTree: return "the tree did not assemble";
    case DrawStage::NoGeometryName: return "no geometry anywhere";
    case DrawStage::GeometryInChild: return "assembled from children";
    case DrawStage::NoGeometryFile: return "file not found";
    case DrawStage::NoMesh: return "the mesh did not parse";
  }
  return "?";
}

// Walks the same path as `buildObjectMesh` but says where exactly it stopped.
// Without that, "the object is not visible" explains nothing.
DrawStage checkDrawable(obf2::FileSystem& files, const obf2::game::Registry& registry,
                        const std::string& templateName, const Args& args) {
  const auto* root = registry.find(templateName);
  if (root == nullptr) return DrawStage::NoTemplate;

  const auto instance = obf2::game::flattenObject(registry, templateName);
  if (!instance) return DrawStage::NoTree;
  if (instance->geometryName.empty()) {
    // A tree may carry its geometry somewhere other than the root: a control point
    // has no mesh of its own, and the flag arrives from `addTemplate flagpole`.
    // Such objects are assembled from the children's meshes.
    // We do not merely look for the file but really assemble the mesh: otherwise
    // "assembled" would only mean "it looks as though it should assemble".
    const auto merged = buildObjectMesh(files, registry, templateName, args, false);
    if (merged && !merged->vertices.empty()) return DrawStage::GeometryInChild;
    return DrawStage::NoGeometryName;
  }

  const std::string path = resolveGeometryPath(files, root->file, instance->geometryName);
  if (path.empty()) return DrawStage::NoGeometryFile;

  if (!loadMesh(files, path, args.geometryIndex, args.lodIndex, false)) return DrawStage::NoMesh;
  return DrawStage::Drawn;
}

// Matches template numbers to names.
//
// The server sends objects by template number, and the number is the order of
// creation (`ObjectTemplateManager::createTemplate`), so the name cannot be got
// from the number itself. But the positions match: we read the level ourselves and
// know what stands where, and the server gives numbers for those same places.
//
// The packet file is made by `tools/linuxded/capture.py --stage world --out`.
int runCalibrate(const Args& args, obf2::FileSystem& files) {
  if (args.levelName.empty()) {
    std::fprintf(stderr, "give a level: --level <name>\n");
    return 1;
  }
  std::string error;
  if (!obf2::level::mountLevel(files, args.modDir, args.levelName, &error)) {
    std::fprintf(stderr, "the level was not mounted: %s\n", error.c_str());
    return 1;
  }

  auto known = buildKnownObjects(files, args.levelName, &error);
  std::printf("level %s: known objects %zu\n", args.levelName.c_str(), known.size());

  const auto packets = obf2::net::bf2::loadCapture(args.calibrate);
  if (packets.empty()) {
    std::fprintf(stderr, "%s holds no packets\n", args.calibrate.c_str());
    return 1;
  }

  std::map<std::uint32_t, std::string> mapping;
  std::map<std::uint32_t, obf2::Vec3f> unmatched;
  int fromServer = 0, matched = 0;
  for (const auto& packet : packets) {
    for (const auto& event : obf2::net::bf2::readEvents(packet)) {
      if (!event.object || !event.object->position) continue;
      ++fromServer;
      const auto& at = *event.object->position;

      const KnownObject* best = nearestKnown(known, at);
      if (best) {
        ++matched;
        mapping[event.object->templateId] = best->name;
      } else {
        unmatched[event.object->templateId] = at;
      }
    }
  }

  std::printf("objects from the server: %d, matched: %d\n", fromServer, matched);
  for (const auto& [id, name] : mapping) {
    std::printf("  %6u  %s\n", id, name.c_str());
  }

  // A check of the guess about where the numbers come from.
  //
  // The server sends a template number rather than a name, and passes no list
  // block at all (the conversation holds only three: 0, 2 and 5). The engine takes
  // the template from `ObjectTemplateManager::getTemplate(unsigned)` — an ordinary
  // `std::map` by number (Linux server, 0x6a1110), so the numbers are handed out
  // somewhere during loading and have to match on both sides.
  //
  // The simplest assumption: the number is the order of a template's creation. It
  // is checked rather than taken on trust: we have "number -> name" pairs won by
  // matching on position, and our own list in creation order. If the assumption is
  // wrong, that is visible right here.
  {
    obf2::game::Registry registry = buildRegistry(files);
    const std::vector<const obf2::game::ObjectTemplate*> ordered = registry.all();
    int hit = 0, miss = 0;
    for (const auto& [id, name] : mapping) {
      if (id >= ordered.size()) { ++miss; continue; }
      if (ordered[id]->name == name) ++hit; else ++miss;
    }
    std::printf("number = creation order? matched %d, not %d (our templates %zu)\n",
                hit, miss, ordered.size());

    // If the numbers do not match, the question is whether at least the **order**
    // matches: then the difference is only that we count something extra or fail to
    // load something, while the idea "the number grows with creation order" is
    // right.
    std::map<std::string, std::size_t> indexByName;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
      indexByName.emplace(ordered[i]->name, i);
    }
    std::printf("order: the server's number -> ours\n");
    long long previous = -1;
    int rising = 0, falling = 0, absent = 0;
    for (const auto& [id, name] : mapping) {
      const auto found = indexByName.find(name);
      if (found == indexByName.end()) {
        std::printf("  %6u  -> not in our list  %s\n", id, name.c_str());
        ++absent;
        continue;
      }
      const auto ours = static_cast<long long>(found->second);
      std::printf("  %6u  -> %6lld  %s\n", id, ours, name.c_str());
      if (previous >= 0) (ours > previous ? rising : falling)++;
      previous = ours;
    }
    std::printf("the order matches: %d times, violated: %d, absent here: %d\n", rising, falling,
                absent);
  }
  if (!unmatched.empty()) {
    std::printf("%zu numbers not recognised:\n", unmatched.size());
    for (const auto& [id, at] : unmatched) {
      std::printf("  %6u  @ %.1f %.1f %.1f\n", id, at.x, at.y, at.z);
    }
  }
  return 0;
}

// The three hashes for the content check.
//
// The first is the one the server computes itself at startup; we cannot compute it
// yet, so it is passed with the `--misc-hash` flag. The second and third are read
// from the fingerprint files: the mod's archives and the level itself.
struct ContentHashes {
  std::array<std::byte, 16> misc{};
  std::array<std::byte, 16> archives{};
  std::array<std::byte, 16> level{};
};

std::optional<ContentHashes> contentHashes(obf2::FileSystem& files, const std::string& levelName,
                                           int ordinal) {
  ContentHashes out;


  // The first hash: an MD5 over four of the mod's files, in exactly this order.
  // The names come from `ChecksumContext::runMiscChecksum`.
  obf2::net::Md5 misc;
  for (const char* name : {"ClientArchives.con", "ServerArchives.con", "Init.con",
                           "GameLogicInit.con"}) {
    const auto data = files.read(name);
    if (!data) return std::nullopt;
    misc.update(*data);
  }
  out.misc = misc.finish();

  const auto readText = [&files](const std::string& path) -> std::string {
    const auto data = files.read(path);
    if (!data) return {};
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
  };

  const std::string archivesText = readText("std_archive.md5");
  const std::string levelText = readText("levels/" + levelName + "/archive.md5");
  const auto archives = obf2::net::bf2::readFingerprint(archivesText, ordinal);
  const auto level = obf2::net::bf2::readFingerprint(levelText, ordinal);
  if (!archives || !level) return std::nullopt;
  out.archives = *archives;
  out.level = *level;
  return out;
}

// The link to a real BF2 server, living together with the window: one and the same
// connection first carries the handshake through and then runs in the frame loop.
// That is what the original does too — a session does not end at the server
// accepting us.
struct RemoteWorld {
  RemoteWorld(const Args& a, obf2::FileSystem& f) : args(a), files(f) {
    world.setOwnName(args.playerName);
    if (!args.recordTo.empty()) {
      recording = std::fopen(args.recordTo.c_str(), "wb");
      if (recording == nullptr) {
        std::printf("  could not write the capture into %s\n", args.recordTo.c_str());
      }
    }
  }
  ~RemoteWorld() {
    if (recording != nullptr) std::fclose(recording);
  }
  RemoteWorld(const RemoteWorld&) = delete;
  RemoteWorld& operator=(const RemoteWorld&) = delete;

  // Where to write the captured traffic (--record). Empty means we do not write.
  std::FILE* recording = nullptr;

  const Args& args;
  obf2::FileSystem& files;
  std::unique_ptr<obf2::net::UdpSocket> socket;
  std::uint8_t id = 0;


  // After that we keep the link up: the server sends pings, and without an answer
  // it will disconnect us. At the same time we count what actually arrives.

  // The level comes from the server, as in the original: it sends it as a data
  // block of type 5 right after registration. Then we watch which of the received
  // objects reach the screen — the server gives a number and a position, the
  // position gives the template's name, and then the same path as in the game.
  std::vector<KnownObject> known;
  std::map<std::uint16_t, obf2::Vec3f> objects;  // id -> where it stands
  obf2::game::Registry registry;
  std::map<std::string, DrawStage> checked;
  std::map<DrawStage, int> stageCounts;
  obf2::net::bf2::DataBlockAssembler blocks;
  bool levelReady = false;
  std::chrono::steady_clock::time_point lastKeepAlive = std::chrono::steady_clock::now();

  // The join sequence lives separately and has a test of its own: every rule about
  // pauses, waiting for the load and stopping at the spawn screen is there
  // (`obf2/net/bf2_join.h`). Only assembling the packets is left here.
  obf2::net::bf2::JoinSequence join;
  std::string levelName;
  int blockOrdinal = 0;
  int pings = 0, dataPackets = 0, other = 0, challenges = 0;
  int eventCount = 0, objectCount = 0;
  std::vector<obf2::net::bf2::CreateSpawnGroup> spawnGroups;
  std::uint8_t lastServerSequence = 0;
  int ghostPackets = 0, ghostRecords = 0;
  int ghostFlagSet = 0, ghostFlagClear = 0, ghostUnparsed = 0;
  int ghostControlled = 0;
  int controlStates = 0;
  std::set<std::uint16_t> ghostObjects;
  std::set<std::uint32_t> seenBlocks;
  std::size_t dataBytes = 0;
  std::uint8_t sequence = 0;
  std::uint8_t batch = 0;
  bool answered = false;


  // The player pressed DONE and pointed at a flag. We deliberately do not know the
  // group's number yet: the groups arrive as events after the level loads, while
  // the button can be pressed earlier. So we remember the **position** and pick the
  // number at the moment of sending — when the list certainly exists.
  void askSpawn(int team, int kit, float worldX, float worldZ, float worldSize) {
    chosenX = worldX;
    chosenZ = worldZ;
    chosenWorld = worldSize;
    havePoint = true;
    join.ask(obf2::net::bf2::JoinChoice{team, kit, 0});
  }

  // The same, but with the group's number given directly. This is for a headless
  // run: there is no spawn screen there, and the number comes from the command line.
  void askSpawnGroup(int team, int kit, int group) {
    havePoint = false;
    directGroup = group;
    join.ask(obf2::net::bf2::JoinChoice{team, kit, group});
  }

  // The group's number for the chosen position. Zero means no position was chosen
  // or the server has not yet told us about its groups.
  std::uint16_t chosenGroupId(float* away = nullptr) const {
    if (!havePoint) return static_cast<std::uint16_t>(directGroup);
    // Our own groups only: another team's the server simply ignores, and the player does not spawn.
    return obf2::net::bf2::nearestSpawnGroup(spawnGroups, chosenX, chosenZ, chosenWorld,
                                             away, world.ownTeam());
  }

  int directGroup = 0;

  bool havePoint = false;
  float chosenX = 0.0f, chosenZ = 0.0f, chosenWorld = 2048.0f;
  // The input we send to the server. We keep it here because the frame loop sends
  // it while whoever reads the keyboard and mouse assembles it.
  obf2::net::bf2::PlayerAction action;
  std::uint32_t actionTick = 0;
  std::chrono::steady_clock::time_point lastAction = std::chrono::steady_clock::now();

  // Prediction of our own movement.
  //
  // The server does not send us our own soldier's position every tick — it only
  // corrects it occasionally (`PlayerControlObjectNetworkable::predict`). Waiting
  // for those corrections makes the movement look like jerks a few tenths of a
  // second apart, with the soldier standing in mid-air between them where the
  // previous one left him. So we compute the movement ourselves — with the same
  // physics as our server — and take the server's corrections as the truth.
  obf2::server::BodyState body;
  obf2::server::SwimState swim;
  obf2::server::TickAccumulator tick;
  bool bodyReady = false;
  int corrections = 0;
  float correctionSum = 0.0f;
  float correctionMax = 0.0f;
  obf2::Vec3f correctionAxis{};
  const obf2::level::Level* terrain = nullptr;
  const obf2::server::CollisionWorld* collision = nullptr;
  obf2::server::PhysicsConstants physics;
  float maxSpeed = 3.9f;  // phy-soldier-run-speed; filled in from the game's data

  // A correction from the server: we put the body where the server sees it.
  void correct(const obf2::Vec3f& position) {
    // How far we diverged from the server. That is a measure of the prediction's
    // quality: while the divergence is small, no abrupt position substitution is
    // visible and there is nothing to smooth.
    if (bodyReady) {
      const obf2::Vec3f delta = position - body.position;
      const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
      correctionSum += distance;
      correctionMax = std::max(correctionMax, distance);
      // Per axis separately: an even divergence on one axis is not a prediction
      // error but a shift of the origin. Mixing them into one length means not
      // seeing what exactly diverged.
      correctionAxis.x += std::abs(delta.x);
      correctionAxis.y += delta.y;  // signed: it matters whether we are above or below
      correctionAxis.z += std::abs(delta.z);
      ++corrections;
      if (corrections <= 8 && terrain != nullptr) {
        std::printf("  correction: server %.2f %.2f %.2f, us %.2f %.2f %.2f, "
                    "ground %.2f (dy %.2f)\n",
                    position.x, position.y, position.z, body.position.x, body.position.y,
                    body.position.z, terrain->groundHeightAt(position), delta.y);
      }
    }
    body.position = position;
    if (!bodyReady) body.velocity = obf2::Vec3f{};
    bodyReady = true;
  }

  // One prediction step. `yaw` is where the player is looking.
  void predict(float step, float yawDegrees) {
    if (!bodyReady || terrain == nullptr) return;
    constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
    const float yaw = yawDegrees * kToRadians;

    // The axes in the action stream are ±99; we need a direction of 0..1. A
    // soldier strafes with the yaw axis: the engine has no separate strafe axis.
    const float scale = 1.0f / static_cast<float>(obf2::net::bf2::kAxisFull);
    const float forward = static_cast<float>(action.axes[obf2::net::bf2::kAxisThrottle]) * scale;
    const float strafe = static_cast<float>(action.axes[obf2::net::bf2::kAxisYaw]) * scale;
    // A zero angle looks along +Z — the same as the server computes, so "right" is
    // the angle plus 90 degrees.
    obf2::Vec3f wish{std::sin(yaw) * forward + std::cos(yaw) * strafe, 0.0f,
                     std::cos(yaw) * forward - std::sin(yaw) * strafe};
    const float magnitude = std::sqrt(wish.x * wish.x + wish.z * wish.z);
    if (magnitude > 1.0f) wish = wish * (1.0f / magnitude);

    const bool sprint = (action.buttons & obf2::net::bf2::kButtonSprint) != 0;
    const bool jump = (action.buttons & obf2::net::bf2::kButtonAction) != 0;
    const float speed = sprint ? physics.sprintSpeed : maxSpeed;

    // The movement goes through the same function as on the server: ground, water,
    // walls. While the client had its own shortened copy, it knew only the ground's
    // height — and the soldier walked through objects.
    //
    // And in whole 1/30 s ticks only (`WorldPref::mTickTime`). A step as long as a
    // frame made movement and jumping differ at 60 and at 120 frames.
    const int ticks = tick.take(step);
    for (int i = 0; i < ticks; ++i) {
      obf2::server::moveSoldier(body, swim, wish, speed, jump, physics, terrain, collision,
                                obf2::server::kTickTime);
    }
  }

  // Send the current input. The original does this thirty times a second and puts
  // the last three sets into the packet — in case of loss.
  void sendActions() {
    if (socket == nullptr || ourSoldier == 0) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastAction < std::chrono::milliseconds(33)) return;
    lastAction = now;

    obf2::net::bf2::ExtendedHeader header;
    header.sequence = sequence++ & 0x3F;
    header.ack = lastServerSequence;
    header.ackBits = 0xFFFFFFFFu;
    // Three identical sets — as the original does: a packet may be lost, and the
    // next one carries the same thing.
    obf2::net::bf2::PlayerActions stream;
    stream.tick = static_cast<std::int32_t>(actionTick++);
    stream.actions.assign(3, action);
    socket->send(obf2::net::bf2::writePlayerActions(id, header, stream));
  }

  // Our own player number and team — from `CreatePlayerEvent` by name.
  int ourPlayer = -1;
  int ourTeam = 0;
  // Objects the server created while in the game: other players' soldiers and
  // vehicles. The position here is the one the object was **created** with. They
  // move by the ghost stream's records, which we do not parse yet, so the
  // placeholder will stand where the object appeared. That is debt, and it is visible on screen.
  // The world's state from the packets: players, objects, their positions and
  // `obf2::net::bf2::WorldView` (`src/net/src/bf2_world.cpp`).
  obf2::net::bf2::WorldView world;
  // The reference point for the compressed vectors: it comes from the
  // controlled-object state, and every object's position in the ghost stream is
  obf2::Vec3f compressionReference;
  int positionUpdates = 0;
  // Another soldier's track: how many updates, how far it travelled from the first
  // position and how high above the ground. Whether he stands or walks, above the
  // ground he has to stay at zero — that is the measure of the parsing's correctness.

  // Every player's team (`CreatePlayerEvent`) and the object the player occupied
  // (`EnterVehicleEvent`). Together they say whose soldier stands where — and say
  // it for certain, without guesswork.
  std::map<std::uint32_t, int> playerTeam;
  std::map<std::uint16_t, std::uint32_t> objectOwner;

  int teamOf(std::uint32_t player) const {
    const auto found = playerTeam.find(player);
    return found == playerTeam.end() ? 0 : found->second;
  }

  // An object's team: 0 means it is not somebody's soldier (a vehicle, level property).
  int objectTeam(std::uint16_t object) const {
    const auto owner = objectOwner.find(object);
    return owner == objectOwner.end() ? 0 : teamOf(owner->second);
  }

  // The object we control. `EnterVehicleEvent` names it.
  std::uint16_t ourSoldier = 0;
  // The object the server sends the controlled-object state about. Before spawning
  // that is not a soldier but the spawn screen's camera.
  std::uint16_t controlObject = 0;
  // The server said `NEPlayerSpawned`. After that the controlled object is a
  // soldier: before spawning there is no soldier, and the engine's `getSoldier`
  // returns nothing (0x445e01).
  bool playerSpawned = false;

  // Where our soldier is now. Empty means we have not spawned yet.
  std::optional<obf2::Vec3f> soldierPosition() const {
    if (ourSoldier == 0) return std::nullopt;
    // We show the predicted position rather than the last correction: tenths of a
    // second pass between corrections, and without prediction the movement would
    // look like jerks.
    if (bodyReady) return body.position;
    const auto found = objects.find(ourSoldier);
    if (found == objects.end()) return std::nullopt;
    return found->second;
  }

  // The handshake: the request, the server's reply, the acknowledgement.
  bool connect() {
    std::string host = args.connectTo;
    std::uint16_t port = 16567;  // BF2's usual game port
    if (const std::size_t colon = host.rfind(':'); colon != std::string::npos) {
      port = static_cast<std::uint16_t>(std::atoi(host.c_str() + colon + 1));
      host = host.substr(0, colon);
    }

    std::string error;
    socket = obf2::net::UdpSocket::connect(host, port, &error);
    if (!socket) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return false;
    }
    std::printf("connecting: %s\n", socket->describe().c_str());

    obf2::net::bf2::ConnectRequest request;
    request.password = args.connectPassword;
    // The server compares the mod's directory against its own (`GSModDirectory`),
    // and on a mismatch sends its own back — so the error is visible at once.
    request.modDirectory = "mods/bf2";

    if (!socket->send(obf2::net::bf2::writeConnectRequest(request))) {
      std::fprintf(stderr, "could not send the request\n");
      return false;
    }
    std::printf("  request sent: protocol %#x, version %#x\n", request.magic, request.version);

    const auto reply = socket->receive(2000);
    if (!reply) {
      std::fprintf(stderr, "  the server is silent\n");
      return false;
    }

    const auto packet = obf2::net::bf2::readPacket(*reply);
    if (!packet) {
      std::fprintf(stderr, "  %zu bytes arrived, but this is not a packet\n", reply->size());
      return false;
    }

    if (packet->denied) {
      std::printf("  denied: %s\n",
                  std::string(obf2::net::bf2::denyReasonName(packet->denied->reason)).c_str());
      if (!packet->denied->modDirectory.empty()) {
        std::printf("  the server wants the directory %s\n", packet->denied->modDirectory.c_str());
      }
      return false;
    }

    if (!packet->accept) {
      std::printf("  unexpected packet of type %d\n", static_cast<int>(packet->kind));
      return false;
    }

    std::printf("  ACCEPTED: connection %d, server time %u ms, PunkBuster %s\n",
                packet->accept->connectionId, packet->accept->serverTime,
                packet->accept->punkBuster ? "on" : "off");

    // The engine waits for an acknowledgement — only after it does the connection
    // become working (in `NetServer::_update` state 1 -> 2).
    socket->send(obf2::net::bf2::writeShortPacket(obf2::net::bf2::PacketKind::ConnectAcceptAck,
                                                  packet->accept->connectionId));
    std::printf("  acknowledgement sent\n");
    id = packet->accept->connectionId;
    return true;
  }

  // Keep the conversation up while we are busy with something long. Loading a
  // level takes about eleven seconds, and all that time we did not answer pings —
  // the server managed to disconnect us before we even said `NELoadComplete`. We
  // deliberately do not advance the sequence's steps here: only the pings are needed.
  // Call it no more often than twice a second: the socket is non-blocking, but the
  //
  // call still costs something, and loading is slow as it is.
  // One turn: advance the spawn sequence and read what arrived. Waiting long is
  void keepAlive() {
    if (socket == nullptr) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastKeepAlive < std::chrono::milliseconds(500)) return;
    lastKeepAlive = now;
    for (int i = 0; i < 8; ++i) {
      const auto more = socket->receive(0);
      if (!more) break;
      const auto parsed = obf2::net::bf2::readPacket(*more);
      if (!parsed) continue;
      if (parsed->extended) lastServerSequence = parsed->extended->sequence;
      if (parsed->kind != obf2::net::bf2::PacketKind::PingRequest) continue;
      obf2::net::bf2::ExtendedHeader header;
      header.sequence = sequence++ & 0x3F;
      header.ack = lastServerSequence;
      header.ackBits = 0xFFFFFFFFu;
      socket->send(obf2::net::bf2::writePingResponse(id, header,
                                                     parsed->pingTime.value_or(0)));
      ++pings;
    }
  }

  // only allowed outside a frame — inside one it would be a freeze.
  // One turn of the conversation: send what is due and **parse one packet**.
  // Returns whether there was a packet — because this has to be called until the
  // queue is empty. The socket's queue does not clear itself: taking one packet
  // per frame while the server sends more makes it grow, and we look at the world
  // as it was several seconds ago.
      // When to send the next step is decided by JoinSequence — every rule about
  bool pump(int timeoutMs) {
      // pauses and waiting is there, together with its test. Assembling the packet
      // and reporting that we sent it is what is left here.
            std::printf("  step: the level is loaded\n");
      const auto now = std::chrono::steady_clock::now();
      if (const auto todo = join.next(now)) {
        obf2::net::bf2::ExtendedHeader next;
        next.sequence = sequence++ & 0x3F;
        next.ack = lastServerSequence;
        next.ackBits = 0xFFFFFFFFu;

        const auto event = [&](std::uint32_t number) {
          socket->send(obf2::net::bf2::writePostRemoteEvent(
              id, next, batch++, obf2::net::bf2::kNetworkCategory, number));
        };
        const auto eventWith = [&](std::uint32_t number, std::uint32_t value) {
          socket->send(obf2::net::bf2::writePostRemoteEvent(
              id, next, batch++, obf2::net::bf2::kNetworkCategory, number, value));
        };
        const auto& choice = join.choice();
        bool sent = true;

        switch (*todo) {
          case obf2::net::bf2::JoinStep::Level:
            event(obf2::net::bf2::kNetLoadComplete);
            std::printf("  step: the level is loaded\n");
            break;
          case obf2::net::bf2::JoinStep::Content: {
            const int ordinal = args.ordinal < 0 ? blockOrdinal : args.ordinal;
            const auto hashes = contentHashes(files, levelName, ordinal);
            if (!hashes) {
              std::printf("  the content check was skipped: no fingerprints\n");
              break;
            }
            socket->send(obf2::net::bf2::writeContentCheckEvent(
                id, next, batch++, hashes->misc, hashes->archives, hashes->level));
            const auto show = [](const std::array<std::byte, 16>& hash) {
              std::string out;
              for (const auto byte : hash) {
                char pair[3];
                std::snprintf(pair, sizeof(pair), "%02x", std::to_integer<int>(byte));
                out += pair;
              }
              return out;
            };
            std::printf("  step: the content check, challenge number %d\n    %s\n    %s\n    %s\n",
                        ordinal, show(hashes->misc).c_str(), show(hashes->archives).c_str(),
                        show(hashes->level).c_str());
            break;
          }
          case obf2::net::bf2::JoinStep::Database:
            event(obf2::net::bf2::kNetDatabaseComplete);
            std::printf("  step: the player base was received\n");
            break;
          case obf2::net::bf2::JoinStep::Simulation:
            event(obf2::net::bf2::kNetStartSimulation);
            std::printf("  step: start counting\n");
            break;
          case obf2::net::bf2::JoinStep::Team:
            eventWith(obf2::net::bf2::kNetSelectTeam, static_cast<std::uint32_t>(choice.team));
            std::printf("  step: team %d\n", choice.team);
            break;
          case obf2::net::bf2::JoinStep::Kit:
            eventWith(obf2::net::bf2::kNetSelectKit, static_cast<std::uint32_t>(choice.kit));
            std::printf("  step: kit %d\n", choice.kit);
            break;
          case obf2::net::bf2::JoinStep::Group: {
            float away = 0.0f;
            const std::uint16_t wire = chosenGroupId(&away);
            eventWith(obf2::net::bf2::kNetSelectSpawnGroup, wire);
            std::printf("  step: spawn point %u (at %.0f m, groups in the list %zu, our team %d)\n",
                        wire, away, spawnGroups.size(), world.ownTeam());
            break;
          }
          case obf2::net::bf2::JoinStep::Ready:
          case obf2::net::bf2::JoinStep::Done:
            sent = false;
            break;
        }
        if (sent) join.commit(now);
      }

      const auto more = socket->receive(timeoutMs);
      if (!more) return false;
      if (recording != nullptr) {
        const auto length = static_cast<std::uint32_t>(more->size());
        std::fwrite(&length, sizeof(length), 1, recording);
        std::fwrite(more->data(), 1, more->size(), recording);
      }
      const auto parsed = obf2::net::bf2::readPacket(*more);
      if (!parsed) return true;
      if (parsed->extended) lastServerSequence = parsed->extended->sequence;

      switch (parsed->kind) {
        case obf2::net::bf2::PacketKind::PingRequest: {
          ++pings;
          obf2::net::bf2::ExtendedHeader header;
          header.sequence = sequence++ & 0x3F;
          if (parsed->extended) header.ack = parsed->extended->sequence;
          header.ackBits = 0xFFFFFFFFu;
          socket->send(obf2::net::bf2::writePingResponse(id, header,
                                                         parsed->pingTime.value_or(0)));
          break;
        }
        case obf2::net::bf2::PacketKind::Data: {
          ++dataPackets;
          dataBytes += more->size();

          if (const auto flag = obf2::net::bf2::ghostFlag(*more)) {
            if (*flag) ++ghostFlagSet; else ++ghostFlagClear;
          } else {
            ++ghostUnparsed;
          }
          // The controlled-object state. From it we take **only** the object's
          // number for now: the triple of numbers in it is the compression
          // reference point, not a position (see bf2_events.h).
          if (const auto state = obf2::net::bf2::readControlObjectState(*more)) {
            if (controlStates < 3) {
              std::printf("  controlled state: reference %.1f %.1f %.1f (counter %d, object %u)\n",
                          state->compressionReference.x, state->compressionReference.y,
                          state->compressionReference.z, state->counter, state->networkId);
            }
            ++controlStates;
            // The compression reference point for the whole stream that follows.
            compressionReference = state->compressionReference;
            // The server states the controlled object's number directly. But the
            // controlled object is not always a soldier: before spawning it is the
            // spawn screen's camera (number 257 on Dalian, with the position from
            // `setBeforeSpawnCamera`). The engine tells them apart by calling
            // `getSoldier` right after `getObject`, and we cannot do that yet — so
            // for now we only check the number against the enter event rather than
            // replacing it.
            if (state->networkId != controlObject) {
              controlObject = state->networkId;
              std::printf("  controlled object: %u%s\n", controlObject,
                          (ourSoldier != 0 && controlObject != ourSoldier) ? " (not our soldier!)"
                                                                          : "");
            }
            // After spawning the controlled object is our soldier. The enter event
            // says the same, but it comes once per game and may not arrive; and
            // without the number we send no action stream — and then the server
            // stops sending us state, because it has nothing to answer.
            if (playerSpawned && controlObject != 0 && ourSoldier != controlObject) {
              ourSoldier = controlObject;
              std::printf("  our soldier by the controlled-object state: %u\n", ourSoldier);
            }
            // There is nothing here to correct the position with: the real one
            // travels later, in the object's own state, and we do not parse that
            // yet. Until we do, only the prediction computes the movement — from
            // the point the server named when it created the soldier. Debt, not a decision.
            // Only after `NEPlayerSpawned`: before spawning we have no soldier,
            // and the enter event happens for other players too.
            if (!bodyReady && playerSpawned && ourSoldier != 0) {
              const auto born = objects.find(ourSoldier);
              if (born != objects.end()) {
                correct(born->second);
                std::printf("  the body was placed at %.1f %.1f %.1f (the soldier's creation position)\n",
                            born->second.x, born->second.y, born->second.z);
              }
            }
          }
          if (const auto ghost = obf2::net::bf2::readGhostHeader(*more)) {
            ++ghostPackets;
            // The "there is a controlled-object state" flag is the most direct
            // sign that the server gave us a soldier: it means the packet carries
            // the state of the very object we control.
            if (ghost->controlObjectState) ++ghostControlled;
            if (ghostPackets <= 3) {
              std::printf("  ghosts: time %u, records %u%s\n", ghost->time, ghost->records,
                          ghost->controlObjectState ? ", there is a controlled-object state" : "");
            }
          }

          // The world's state comes from **every** data packet, not only from those
          // with ghosts: players and objects arrive as events long before the first
          // record of the stream. The parsing lives in
          // `obf2::net::bf2::WorldView` (`src/net/src/bf2_world.cpp`).
          {
            const int before = world.positionUpdates();
            world.feed(*more);
            positionUpdates += world.positionUpdates() - before;
          }

          // We parse every event in the packet: by the size table each can be
          // skipped by exactly its length, so unfamiliar types do not throw off the
          // parsing of the ones after them.
          for (const auto& event : obf2::net::bf2::readEvents(*more)) {
            ++eventCount;
            if (event.block) {
              const auto done = blocks.feed(*event.block);
              if (done && seenBlocks.insert(done->first).second) {
                // What the server sends in blocks at all: among them we look for the
                // one that matches template numbers to names.
                std::string head;
                for (std::size_t k = 0; k < done->second.size() && k < 24; ++k) {
                  const int byte = std::to_integer<int>(done->second[k]);
                  head += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
                }
                std::printf("  block %u: %zu bytes  %s\n", done->first, done->second.size(),
                            head.c_str());
              }
              // An experiment: acknowledge an assembled block with a NEDataBlockReady
              // event. The real client apparently does this — the server keeps its own
              // record of what the client has already received.
              if (done && args.blockReady) {
                obf2::net::bf2::ExtendedHeader ack;
                ack.sequence = sequence++ & 0x3F;
                ack.ack = lastServerSequence;
                ack.ackBits = 0xFFFFFFFFu;
                socket->send(obf2::net::bf2::writePostRemoteEvent(
                    id, ack, batch++, obf2::net::bf2::kNetworkCategory,
                    obf2::net::bf2::kNetDataBlockReady,
                    static_cast<std::int32_t>(done->first)));
                std::printf("  block %u assembled, acknowledged\n", done->first);
              }
              // Block 2 is the real MapInfo. From it we take the challenge number:
              // the server picks it when it loads the level, and it is against that
              // line of the fingerprints that it checks our content check.
              if (done && done->first == obf2::net::bf2::kMapInfoNetBuffer) {
                if (const auto net = obf2::net::bf2::parseMapInfoNetBuffer(done->second)) {
                  std::printf("  server: slots %d, commander %s, challenge number %d\n",
                              net->maxPlayers, net->commanderEnabled ? "yes" : "no",
                              net->challengeOrdinal);
                  if (args.ordinal < 0) blockOrdinal = net->challengeOrdinal;
                }
              }
              if (done && done->first == obf2::net::bf2::kMapInfoBlock && !levelReady) {
                if (const auto info = obf2::net::bf2::parseMapInfo(done->second)) {
                  std::printf("  the server plays %s, mode %s, size %d, first number %u\n",
                              info->levelName.c_str(), info->gameMode.c_str(), info->size,
                              info->first);
                  // The challenge number no longer goes here: it is in block 2, while
                  // block 5's first number is something else.
                  std::string levelError;
                  if (!obf2::level::mountLevel(files, args.modDir, info->levelName, &levelError)) {
                    std::printf("  the level was not mounted: %s\n", levelError.c_str());
                  } else {
                    known = buildKnownObjects(files, info->levelName, &levelError);
                    registry = buildRegistry(files);
                    std::printf("  the level was read: known objects %zu\n", known.size());
                  }
                  levelReady = true;
                  join.setLevelReady();
                  join.setSkipContent(args.skipContent);
                  join.setSkipDatabase(args.skipDatabase);
                  join.setSkipSimulation(!args.startSimulation);
                  levelName = info->levelName;
                }
              }
              continue;
            }
            if (event.remote) {
              const auto& remote = *event.remote;
              if (remote.category == obf2::net::bf2::kNetworkCategory) {
                std::printf("  server: event %u%s\n", remote.number,
                            remote.value ? (" = " + std::to_string(*remote.value)).c_str() : "");
                if (remote.number == obf2::net::bf2::kNetPlayerSpawned) {
                  playerSpawned = true;
                  std::printf("  THE PLAYER SPAWNED\n");
                }
              }
              continue;
            }
            if (event.spawnGroup) {
              const auto& group = *event.spawnGroup;
              spawnGroups.push_back(group);
              // The world's size comes from the level, because that is what the server
              // packs the position with (GLSWorldSizeX/Z).
              const float worldSize = 2048.0f;
              std::printf(
                  "  spawn group: number %u, team %u, network %u, flags %d%d%d, "
                  "position %.0f %.0f\n",
                  group.id, group.team, group.networkId, group.flag1 ? 1 : 0, group.flag2 ? 1 : 0,
                  group.flag3 ? 1 : 0,
                  obf2::net::bf2::spawnGroupWorldPos(group.worldX, worldSize),
                  obf2::net::bf2::spawnGroupWorldPos(group.worldZ, worldSize));
              continue;
            }
            if (event.object) {
              ++objectCount;
              if (!event.object->position) continue;
              const auto& at = *event.object->position;
              // The positions the server named for us. From them we take where to look
              // from: we do not know our own soldier yet, while the flags the server
              // sends at once — and it is next to them that the player appears.
              objects[event.object->networkId] = at;
              if (known.empty()) {
                if (objectCount <= 3) {
                  std::printf("  object: template %u, id %u, position %.1f %.1f %.1f\n",
                              event.object->templateId, event.object->networkId, at.x, at.y, at.z);
                }
                continue;
              }
              const KnownObject* match = nearestKnown(known, at);
              if (match == nullptr) {
                // An object that is not in the level's placement is something alive: another
                // player's soldier or a vehicle the server created while in the game.
                // What exactly it is we do not know yet: matching a template number to a
                // name is not worked out. So we remember the position and show a
                // placeholder.
                std::printf("  not recognised: template %u @ %.1f %.1f %.1f\n",
                            event.object->templateId, at.x, at.y, at.z);
                continue;
              }

              auto found = checked.find(match->name);
              if (found == checked.end()) {
                const DrawStage stage = checkDrawable(files, registry, match->name, args);
                found = checked.emplace(match->name, stage).first;
                ++stageCounts[stage];
              }
              if (found->second != DrawStage::Drawn) {
                std::printf("  NOT VISIBLE: %-44s %s\n", match->name.c_str(),
                            std::string(drawStageName(found->second)).c_str());
              }
            }
            if (event.player) {
              playerTeam[event.player->id] = static_cast<int>(event.player->team);
              std::printf("  player: %s (id %u, team %u)\n",
                          event.player->name.c_str(), event.player->id, event.player->team);
              // We learn our own player number by name: the server assembles it as
              // "clan tag + space + name", so we compare by the tail rather than by the
              // whole string.
              const std::string& name = event.player->name;
              const std::string& want = args.playerName;
              if (ourPlayer < 0 && !want.empty() && name.size() >= want.size() &&
                  name.compare(name.size() - want.size(), want.size(), want) == 0) {
                ourPlayer = static_cast<int>(event.player->id);
                ourTeam = static_cast<int>(event.player->team);
                std::printf("  that is us: id %d, team %d\n", ourPlayer, ourTeam);
              }
            }
            // Who controls what. A soldier in BF2 is "occupied" like a vehicle, and it
            // is with this event that the server says which object is ours.
            if (event.enter) {
              // Whose object is whose the server says itself, without any guesswork:
              // `CreatePlayerEvent` gives the player's team, and this event the object
              // the player occupied. So a placeholder at this position is no longer
              // "something alive" but a particular player of a particular team.
              // particular team.
              objectOwner[event.enter->object] = event.enter->player;
              if (ourPlayer >= 0 && static_cast<int>(event.enter->player) == ourPlayer) {
                ourSoldier = event.enter->object;
                std::printf("  our object: %u\n", ourSoldier);
              } else {
                std::printf("  player %u occupied object %u (team %d)\n", event.enter->player,
                            event.enter->object, teamOf(event.enter->player));
              }
            }
            if (event.exitPlayer && ourPlayer >= 0 &&
                static_cast<int>(*event.exitPlayer) == ourPlayer) {
              ourSoldier = 0;
              std::printf("  we left the object\n");
            }
          }
          if (parsed->challenge) {
            ++challenges;
            if (!answered) {
              std::printf("  challenge event: %s, mod %s\n", parsed->challenge->challenge.c_str(),
                          parsed->challenge->modDirectory.c_str());

              obf2::net::bf2::ExtendedHeader header;
              header.sequence = sequence++ & 0x3F;
              if (parsed->extended) header.ack = parsed->extended->sequence;
              // Ones in the mask mean "everything previous arrived". Without that the
              // server considers the event unacknowledged and sends it again and again.
              header.ackBits = 0xFFFFFFFFu;
              socket->send(obf2::net::bf2::writeChallengeResponse(id, header, batch++));
              std::printf("  challenge reply sent\n");
              answered = true;

              // Next the engine waits for the block with the client's details: without
              // it the player does not exist. The block travels as events — first the
              // header with the type and the size, then the chunks.
              obf2::net::bf2::ClientInfo info;
              info.name = args.playerName;
              info.nameHash = obf2::net::bf2::clientInfoNameHash(info.name);
              const auto blob = obf2::net::bf2::buildClientInfo(info);

              const auto nextHeader = [&]() {
                obf2::net::bf2::ExtendedHeader next;
                next.sequence = sequence++ & 0x3F;
                if (parsed->extended) next.ack = parsed->extended->sequence;
                next.ackBits = 0xFFFFFFFFu;
                return next;
              };

              socket->send(obf2::net::bf2::writeDataBlockHeader(
                  id, nextHeader(), batch++, obf2::net::bf2::kClientInfoBlock,
                  static_cast<std::uint32_t>(blob.size())));
              for (std::size_t at = 0; at < blob.size(); at += 200) {
                const auto count = std::min<std::size_t>(200, blob.size() - at);
                socket->send(obf2::net::bf2::writeDataBlockChunk(
                    id, nextHeader(), batch++,
                    std::span<const std::byte>(blob.data() + at, count)));
              }
              std::printf("  ClientInfo sent: name %s, %zu bytes\n",
                          info.name.c_str(), blob.size());

            }
          }
          break;
        }
        default:
          ++other;
          if (other <= 4) {
            std::printf("  another packet: type %d%s\n", static_cast<int>(parsed->kind),
                        parsed->kind == obf2::net::bf2::PacketKind::Disconnect
                            ? " (Disconnect!)" : "");
            // A break is not "another packet" but the server's answer. We show the
            // bytes in full: the reason, if it is there at all, lies in them.
            if (parsed->kind == obf2::net::bf2::PacketKind::Disconnect) {
              std::string hex;
              std::string text;
              for (std::size_t k = 0; k < more->size() && k < 64; ++k) {
                char pair[4];
                const int byte = std::to_integer<int>((*more)[k]);
                std::snprintf(pair, sizeof(pair), "%02x ", byte);
                hex += pair;
                text += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
              }
              std::printf("    the step at this moment: %s, bytes %zu\n    %s\n    %s\n",
                          obf2::net::bf2::joinStepName(join.step()), more->size(), hex.c_str(),
                          text.c_str());
            }
          }
          break;
      }
      return true;
  }

  void report() {

    std::printf("  over 30 seconds: pings %d (all answered), data packets %d (%zu bytes), "
                "other %d\n",
                pings, dataPackets, dataBytes, other);
    // If the challenge came once, the server accepted our reply. While the reply
    // does not satisfy it, it sends the challenge again and again.
    std::printf("  events parsed: %d, of them world objects: %d\n", eventCount, objectCount);
    std::printf("  packets with a ghost stream: %d, state updates: %d, distinct objects: %zu\n",
                ghostPackets, ghostRecords, ghostObjects.size());
    std::printf("  the ghost flag: set %d, cleared %d, not read %d\n", ghostFlagSet,
                ghostFlagClear, ghostUnparsed);
    std::printf("  packets with a controlled-object state: %d, parsed %d\n",
                ghostControlled, controlStates);
    if (corrections > 0) {
      const float n = static_cast<float>(corrections);
      std::printf("  corrections from the server: %d, divergence on average %.2f m, at most %.2f m\n",
                  corrections, correctionSum / n, correctionMax);
      std::printf("    per axis: x %.2f, y %.2f (signed), z %.2f\n", correctionAxis.x / n,
                  correctionAxis.y / n, correctionAxis.z / n);
    }
    std::printf("  objects created in the game (other soldiers and vehicles): %zu, "
                "position updates from the ghost stream: %d\n",
                world.objects().size(), positionUpdates);
    if (world.rejected() > 0) {
      std::printf("  positions rejected as unreadable: %d\n", world.rejected());
    }
    for (const auto& [id, object] : world.objects()) {
      if (object.team == 0 || object.updates == 0) continue;
      std::printf("  soldier %u's track: updates %d, travelled %.1f m, above the ground on average "
                  "%.2f m\n",
                  id, object.updates, object.travelled,
                  object.aboveGround / static_cast<float>(object.updates));
    }
    for (const auto& [object, player] : objectOwner) {
      std::printf("  player %u -> object %u: in the ghost records %s\n", player, object,
                  ghostObjects.count(object) ? "YES" : "NO");
    }
    for (const auto& [id, object] : world.objects()) {
      const obf2::Vec3f& at = object.position;
      std::printf("    object %5u  %8.1f %7.1f %8.1f  %s%s\n", id, at.x, at.y, at.z,
                  object.team == 0 ? "not a player"
                                   : (object.team == world.ownTeam() ? "our soldier"
                                                                     : "AN ENEMY SOLDIER"),
                  object.fromGhostStream ? " (from the stream)" : "");
    }
    if (ourSoldier != 0) {
      std::printf("  our object %u in the ghost records: %s\n", ourSoldier,
                  ghostObjects.count(ourSoldier) ? "yes" : "no");
    }
    if (!checked.empty()) {
      std::printf("  distinct templates: %zu\n", checked.size());
      for (const auto& [stage, count] : stageCounts) {
        std::printf("    %-24s %d\n", std::string(drawStageName(stage)).c_str(), count);
      }
    }
    std::printf("  challenges received: %d %s\n", challenges,
                challenges == 1 ? "(the reply was accepted)" : "(the reply was not accepted)");

  }

  void disconnect() {
    if (!socket) return;
    socket->send(obf2::net::bf2::writeShortPacket(
        obf2::net::bf2::PacketKind::Disconnect, id));
  }
};

// Text mode: connect, spin for a while and report what arrived.
// There is deliberately no window here — this is an instrument for taking the protocol apart.
int runProbe(const Args& args, obf2::FileSystem& files) {
  RemoteWorld remote(args, files);
  if (!remote.connect()) return 1;
  // In a probe there is nothing to load: no window, no scene.
  remote.join.setClientLoaded();
  // There is no spawn screen either, so the choice comes from the command line, and
  // we make it at once.
  remote.askSpawnGroup(args.team, args.kit, args.spawnGroup);
  // --frames here says how many turns to listen for: thirty is enough for short
  // experiments (whether the server breaks us off on the content check).
  const int loops = args.frames > 0 ? args.frames : 90;
  for (int i = 0; i < loops; ++i) remote.pump(500);
  remote.report();
  remote.disconnect();
  return 0;
}

int runSession(const Args& args, obf2::FileSystem& files, std::string* nextLevel,
               RemoteWorld* remote = nullptr) {
  // --- preparing the scene ---------------------------------------------

  // Unique geometry is kept apart from the placement: on a level the same building
  // occurs dozens of times, and uploading it to the GPU each time makes no sense.
  struct Scene {
    // One placement of a piece of geometry. `road` says the renderer must draw
    // it as a skin on the terrain rather than as geometry of its own — what that
    // means is `obf2::gfx`'s business, not ours.
    struct Instance {
      int mesh = -1;
      obf2::Mat4 transform;
      bool road = false;
      float roadBlendFactor = 1.0f;
      // Which page of the level's light map atlas this placement is baked into,
      // and its window in it. -1 means the object has no baked light map.
      int lightmapAtlas = -1;
      float lightmapOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    std::vector<obf2::mesh::RenderMesh> meshes;
    std::vector<Instance> instances;
    obf2::Vec3f center;
    float radius = 1.0f;

    void add(obf2::mesh::RenderMesh&& geometry, const obf2::Mat4& transform, bool road = false) {
      meshes.push_back(std::move(geometry));
      Instance instance;
      instance.mesh = static_cast<int>(meshes.size()) - 1;
      instance.transform = transform;
      instance.road = road;
      instances.push_back(instance);
    }
  } scene;

  // The sky dome, kept apart from the scene: everything in the scene has a
  // fixed place, and the dome's place is wherever the camera is.
  std::optional<obf2::mesh::RenderMesh> skyDome;

  // The level's baked object light maps, keyed by template name and position.
  obf2::level::ObjectLightmaps objectLightmaps;
  // One of the terrain's chart maps, kept for its size alone: the near detail's
  // half-texel correction needs it, and the patches themselves are handed to
  // the scene as they are built.
  std::string firstChartMap;

  std::optional<obf2::level::Level> level;
  obf2::game::Registry registry;
  // The server and the client live all the time, not only during loading: in a
  // single-player game it is they that move the world.
  std::unique_ptr<obf2::server::GameServer> hostedServer;
  // The collision geometry for movement prediction on a real server.
  // Our own server owns its own; here it is ours.
  std::unique_ptr<obf2::server::CollisionWorld> remoteCollision;
  std::unique_ptr<obf2::server::GameClient> hostedClient;
  std::uint32_t localSoldierId = 0;

  if (!args.levelName.empty()) {
    std::string error;
    if (!obf2::level::mountLevel(files, args.modDir, args.levelName, &error)) {
      std::fprintf(stderr, "level %s not mounted: %s\n", args.levelName.c_str(), error.c_str());
      return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    level = obf2::level::loadLevel(files, args.levelName, &error);
    if (!level) {
      std::fprintf(stderr, "the level was not loaded: %s\n", error.c_str());
      return 1;
    }
    std::printf("level: %s\n  height map %dx%d, scale %.4g/%.6g/%.4g, sea level %.1f\n",
                level->name.c_str(), level->primary.size, level->primary.size,
                level->primary.scale.x, level->primary.scale.y, level->primary.scale.z,
                level->terrain.seaLevel);
    std::printf("  static objects: %zu, roads: %zu\n", level->objects.size(),
                level->roads.size());
    // The level's baked light maps for placed objects. Read once here; every
    // placement below asks it for its own window.
    objectLightmaps = obf2::level::ObjectLightmaps::load(files, args.levelName);

    auto patches = obf2::level::buildTerrainPatches(*level, files);
    std::printf("  terrain patches: %zu of %d (the rest under water, no colour map)\n", patches.size(),
                ((level->primary.size - 1) / level->terrain.patchSize) *
                    ((level->primary.size - 1) / level->terrain.patchSize));
    for (auto& patch : patches) {
      if (firstChartMap.empty()) firstChartMap = patch.detailmap;
      scene.add(std::move(patch.geometry), obf2::Mat4::identity());
    }

    // Roads: their vertices lie relative to the start point, so we place them by
    // the absolute position from the .con.
    int roadsPlaced = 0;
    for (auto& road : level->roads) {
      if (road.geometry.indices.empty()) continue;
      scene.add(std::move(road.geometry), obf2::translation(road.position), true);
      scene.instances.back().roadBlendFactor = road.blendFactor;
      ++roadsPlaced;
    }
    std::printf("  roads in the scene: %d\n", roadsPlaced);
    scene.add(obf2::level::buildWaterPlane(*level), obf2::Mat4::identity());

    // The sky dome. Its place is the camera's, so it is not a scene instance:
    // it is uploaded on its own and put into the draw list every frame.
    if (auto dome = obf2::level::buildSkyDome(*level, files)) {
      skyDome = std::move(*dome);
      std::printf("  sky: %s, texture %s, rotation %.0f\n", level->sky.domeTemplate.c_str(),
                  level->sky.texture.c_str(), static_cast<double>(level->sky.domeRotation));
    } else if (!level->sky.domeTemplate.empty()) {
      std::printf("  sky: the dome mesh for `%s` was not found\n",
                  level->sky.domeTemplate.c_str());
    }

    if (remote != nullptr) remote->keepAlive();
    std::printf("  loading: the level's own files took %.2f s\n", secondsSince(started));
    const auto registryStarted = std::chrono::steady_clock::now();
    registry = buildRegistry(files);
    std::printf("  registry: %zu templates (%.2f s)\n", registry.size(),
                secondsSince(registryStarted));
    if (remote != nullptr) remote->keepAlive();

    // --- a single-player game = a local server plus a client ---
    //
    // In BF2 the server owns the world even offline, so with --hosted the placement
    // comes not from the level's file but from the packets that arrived from the
    // server through the in-memory loop. The renderer draws what came over the network.
    std::vector<obf2::level::StaticObject> placement = level->objects;

    if (args.hosted) {
      obf2::server::ServerSettings serverSettings;
      serverSettings.levelName = level->name;
      // The spawn is above the map's centre, slightly above sea level, so as not to
      // end up inside a hill.
      serverSettings.spawnPosition = obf2::Vec3f{-40.0f, level->terrain.seaLevel + 40.0f, -200.0f};

      // The movement constants come from the game's data rather than from our heads:
      // the same file the original reads.
      obf2::engine::Console physicsConsole;
      serverSettings.physics.bind(physicsConsole);
      obf2::con::Interpreter physicsInterpreter(
          files, [&](const obf2::con::Command& c) { physicsConsole.execute(c); });
      physicsInterpreter.runFile("objects/soldiers/common/common.con");
      std::printf("  physics: acceleration %.2f, deceleration %.2f, air control %.2f\n",
                  serverSettings.physics.acceleration, serverSettings.physics.deceleration,
                  serverSettings.physics.airMovementFactor);
      // The tickets and the round's settings come from the same files the game reads:
      // GameLogicInit.con (the starting tickets) and Settings/ServerSettings.con.
      {
        obf2::engine::Console settingsConsole;
        settingsConsole.bind("gameLogic.setDefaultNumberOfTickets",
                             [&](const obf2::con::Command& command) {
                               const auto team = command.argInt(0);
                               const auto count = command.argInt(1);
                               if (team && count && *team >= 1 && *team <= 2) {
                                 serverSettings.defaultTickets[*team] = *count;
                               }
                             });
        settingsConsole.bind("sv.ticketRatio", [&](const obf2::con::Command& command) {
          serverSettings.ticketRatio = command.argFloat(0).value_or(serverSettings.ticketRatio);
        });
        settingsConsole.bind("sv.spawnTime", [&](const obf2::con::Command& command) {
          serverSettings.respawnDelay = command.argFloat(0).value_or(serverSettings.respawnDelay);
        });
        settingsConsole.bind("sv.numPlayersNeededToStart", [&](const obf2::con::Command& command) {
          serverSettings.playersNeededToStart =
              command.argInt(0).value_or(serverSettings.playersNeededToStart);
        });
        // We have a spawn screen, so the workaround for headless runs is not needed
        // here: the soldier will spawn only after DONE, as in the engine.
        serverSettings.spawnOnJoin = false;
        obf2::con::Interpreter settingsInterpreter(
            files, [&](const obf2::con::Command& c) { settingsConsole.execute(c); });
        settingsInterpreter.runFile("GameLogicInit.con");
        settingsInterpreter.runFile("Settings/ServerSettings.con");
        std::printf("  tickets: %d against %d (ticketRatio %.0f%%), spawn in %.0f s\n",
                    serverSettings.defaultTickets[1], serverSettings.defaultTickets[2],
                    serverSettings.ticketRatio, serverSettings.respawnDelay);
      }

      hostedServer = std::make_unique<obf2::server::GameServer>(serverSettings);
      obf2::server::GameServer& gameServer = *hostedServer;

      // The level's statics first, then the game logic — exactly the order the engine
      // does it in. The other way round is not allowed: `loadWorld` starts with a
      // clean object list and would sweep away the flags `setGameplay` places.
      gameServer.loadWorld(*level);

      // The mode's game logic: control points and vehicle spawners.
      std::string gameplayError;
      if (auto gameplay = obf2::level::loadGameplayObjects(files, level->name, "gpm_cq", 16,
                                                           &gameplayError)) {
        std::printf("  game logic: %zu control points, %zu vehicle spawners, "
                    "%zu spawn points\n",
                    gameplay->controlPoints.size(), gameplay->spawners.size(),
                    gameplay->spawnPoints.size());
        for (const auto& point : gameplay->controlPoints) {
          std::printf("    point %d \"%s\" radius %.0f @ %.0f/%.0f/%.0f\n", point.id,
                      point.nameKey.c_str(), point.radius, point.position.x, point.position.y,
                      point.position.z);
        }
        gameServer.setGameplay(std::move(*gameplay));
      } else {
        std::printf("  game logic: %s\n", gameplayError.c_str());
      }

      // The terrain, for collision with the ground: without it a soldier falls forever.
      gameServer.setTerrain(&*level);

      // Vehicles have to stop a soldier too: they stand in the server's world rather
      // than in the level's statics, so they are added separately.
      std::vector<obf2::level::StaticObject> collisionObjects = level->objects;
      for (const auto& object : gameServer.objects()) {
        if (object.spawnerIndex < 0) continue;
        obf2::level::StaticObject vehicle;
        vehicle.templateName = object.templateName;
        vehicle.position = object.position;
        vehicle.rotation = object.rotation;
        vehicle.hasRotation = true;
        collisionObjects.push_back(std::move(vehicle));
      }

      gameServer.setCollision(buildCollisionWorld(files, registry, collisionObjects));

      auto [clientSide, serverSide] = obf2::net::LoopbackConnection::createPair();
      gameServer.accept(std::move(serverSide));

      hostedClient = std::make_unique<obf2::server::GameClient>(std::move(clientSide),
                                                               std::string("player"));
      obf2::server::GameClient& client = *hostedClient;
      client.connect();

      // The world is large and is cut into packets, so we spin while new ones arrive.
      std::size_t previous = 0;
      for (int step = 0; step < 4096; ++step) {
        gameServer.tick(1.0f / 60.0f);
        client.tick(1.0f / 60.0f);
        if (client.objects().size() == previous && step > 8) break;
        previous = client.objects().size();
      }

      std::printf("  local server: %s, players %zu, packets %lld/%lld\n",
                  std::string(obf2::server::clientStateName(client.state())).c_str(),
                  gameServer.playerCount(), gameServer.packetsSent(),
                  gameServer.packetsReceived());
      int flags = 0;
      for (const auto& [id, object] : client.objects()) {
        (void)id;
        if (object.templateName.rfind("CPNAME", 0) == 0) ++flags;
      }
      std::printf("  control points that reached the client: %d\n", flags);
      std::printf("  the client received objects: %zu of %zu\n", client.objects().size(),
                  level->objects.size());

      // We do not put the player's soldier into the scene: we play as him, not watch him.
      for (const auto& player : gameServer.players()) localSoldierId = player.soldierId;

      placement.clear();
      placement.reserve(client.objects().size());
      for (const auto& [id, object] : client.objects()) {
        if (id == localSoldierId) continue;
        obf2::level::StaticObject staticObject;
        staticObject.templateName = object.templateName;
        staticObject.position = object.position;
        staticObject.rotation = object.rotation;
        staticObject.hasRotation = true;
        placement.push_back(std::move(staticObject));
      }

      // Vegetation does not travel over the network — the client draws it from the
      // level's data, as the original does. The matrix from the .con carries both
      int overgrowth = 0;
      for (const obf2::level::StaticObject& object : level->objects) {
        if (!object.isOvergrowth) continue;
        placement.push_back(object);
        ++overgrowth;
      }
      std::printf("  vegetation from the level's data: %d instances\n", overgrowth);
    }

    std::unordered_map<std::string, int> meshIndexByTemplate;
    std::map<std::string, int> missing;
    int placed = 0;
    int lightmapped = 0;
    const auto placementStarted = std::chrono::steady_clock::now();

    // The geometry first, and on every core. A level places two thousand
    // objects out of three hundred templates, and assembling one — unpacking
    // its meshes from the archives, flattening its tree, moving its parts — is
    // the same work whichever thread does it and looks at nothing another
    // thread writes. The order of the built meshes is the order the names come
    // in, so the scene is the same as when this was a loop.
    {
      std::vector<std::string> names;
      for (const auto& object : placement) {
        if (meshIndexByTemplate.emplace(object.templateName, -1).second) {
          names.push_back(object.templateName);
        }
      }
      std::vector<std::optional<obf2::mesh::RenderMesh>> built(names.size());
      obf2::parallelFor(names.size(), [&](std::size_t i) {
        built[i] = buildObjectMesh(files, registry, names[i], args, false);
      });
      for (std::size_t i = 0; i < names.size(); ++i) {
        if (!built[i]) continue;
        scene.meshes.push_back(std::move(*built[i]));
        meshIndexByTemplate[names[i]] = static_cast<int>(scene.meshes.size()) - 1;
      }
      if (remote != nullptr) remote->keepAlive();
    }

    for (const auto& object : placement) {
      // The placement is the longest part of loading. While it goes on, the server
      // has to hear that we are alive.
      if (remote != nullptr) remote->keepAlive();
      const auto found = meshIndexByTemplate.find(object.templateName);
      if (found == meshIndexByTemplate.end()) continue;
      if (found->second < 0) {
        ++missing[object.templateName];
        continue;
      }

      obf2::Mat4 transform = object.transform;
      if (!object.hasTransform) {
        transform = obf2::translation(object.position);
        if (object.hasRotation) {
          transform = transform * obf2::rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                             object.rotation.z);
        }
      }
      Scene::Instance instance;
      instance.mesh = found->second;
      instance.transform = transform;
      // The baked light map is keyed by the template's name and the placement's
      // own position, so it is looked up here rather than with the geometry:
      // the mesh is shared between copies and the light map is not.
      const auto* baked =
          args.noLightmaps ? nullptr
                           : objectLightmaps.find(object.templateName, object.position);
      if (baked != nullptr) {
        instance.lightmapAtlas = baked->atlas;
        instance.lightmapOffset[0] = baked->scaleU;
        instance.lightmapOffset[1] = baked->scaleV;
        instance.lightmapOffset[2] = baked->offsetU;
        instance.lightmapOffset[3] = baked->offsetV;
        ++lightmapped;
      }
      scene.instances.push_back(instance);
      ++placed;
    }

    std::printf("  unique geometry: %zu, placed: %d, without geometry: %zu templates (%.2f s)\n",
                scene.meshes.size() - patches.size(), placed, missing.size(),
                secondsSince(placementStarted));
    std::printf("  baked light maps: %d of %d objects, %d atlas pages\n", lightmapped, placed,
                objectLightmaps.atlasCount());
    int shown = 0;
    for (const auto& [name, count] : missing) {
      if (shown++ >= 5) break;
      std::printf("    without geometry: %s (x%d)\n", name.c_str(), count);
    }

    std::printf("  terrain lighting: sun %.2f/%.2f/%.2f, sky %.2f/%.2f/%.2f\n",
                level->terrain.terrainSunColor.x, level->terrain.terrainSunColor.y,
                level->terrain.terrainSunColor.z, level->terrain.terrainSkyColor.x,
                level->terrain.terrainSkyColor.y, level->terrain.terrainSkyColor.z);
    std::printf("  view distance: %.0f (the level's own)\n",
                static_cast<double>(level->maximumViewDistance));
    std::printf("  fog: %.0f..%.0f, base %.2f, floor %.2f, colour %.2f/%.2f/%.2f\n",
                level->terrain.fogStart, level->terrain.fogEnd, level->terrain.fogBase,
                level->terrain.fogFloor, level->terrain.fogColor.x, level->terrain.fogColor.y,
                level->terrain.fogColor.z);

    const float extent = level->halfExtent() * level->primary.scale.x;
    scene.center = obf2::Vec3f{0.0f, level->terrain.seaLevel, 0.0f};
    scene.radius = extent;
  }

  // --- the remaining modes ---------------------------------------------

  obf2::engine::Engine engine;
  const bool bootMode = args.levelName.empty() && args.objectName.empty() && args.meshPath.empty();
  std::string requestedLevel;
  bool menuQuit = false;
  int introQuad = -1;
  int loadingQuad = -1;
  std::vector<int> loadingTextQuads;

  if (bootMode) {
    engine.boot(files, args.modDir);
    const auto& settings = engine.settings();

    std::printf("engine start\n  read: ");
    for (const auto& file : engine.bootFiles()) std::printf("%s ", file.c_str());
    std::printf("\n  player: \"%s\", fullscreen: %d, field of view: %.2f\n",
                settings.general.playerName.c_str(), settings.video.fullScreen ? 1 : 0,
                settings.video.fieldOfView);
    // The ten the options screen has. Printed because they were bound to a
    // name the game never writes and nobody noticed for months
    // (docs/TODO-graphics.md).
    const obf2::engine::VideoSettings& v = settings.video;
    std::printf("  quality: terrain %d, effects %d, geometry %d, texture %d, lighting %d\n"
                "           dynamic shadows %d, dynamic lights %d, antialiasing %d,"
                " filtering %d, view distance %.2f\n",
                v.terrainQuality, v.effectsQuality, v.geometryQuality, v.textureQuality,
                v.lightingQuality, v.dynamicShadowsQuality, v.dynamicLightingQuality,
                v.antialiasing, v.textureFilteringQuality,
                static_cast<double>(v.viewDistanceScale));
    std::printf("  show the intro: %d, movies found: %zu\n",
                settings.general.viewIntroMovie ? 1 : 0, engine.movies().size());
    for (const auto& movie : engine.movies()) {
      std::printf("    %s (%.1f MB)\n", movie.path.c_str(),
                  static_cast<double>(movie.sizeBytes) / (1024.0 * 1024.0));
    }
    // We do not decode Bink, so each movie is a black screen for a second and a
    // half. Said out loud, because otherwise the first thing the program does
    // is show six seconds of nothing and look hung.
    if (settings.general.viewIntroMovie && !engine.movies().empty()) {
      std::printf("    the movies are not decoded — %.1f s of black, Space skips one\n",
                  1.5 * static_cast<double>(engine.movies().size()));
    }

    const auto& console = engine.console();
    std::printf("  console: handlers %zu, aliases %zu, commands run %lld, "
                "unknown %lld\n",
                console.handlerCount(), console.aliasCount(), console.executedCount(),
                console.unknownCount());
    int shown = 0;
    for (const auto& [name, count] : console.unknownCommands()) {
      if (shown++ >= 24) break;
      std::printf("    without a handler: %s (x%d)\n", name.c_str(), count);
    }

    // The commands the menu's buttons run. The interface drives the game through the
    // console — the same as in the original.
    engine.console().bind("openbf2.startLevel", [&](const obf2::con::Command& command) {
      const std::string_view level = command.argStr(0);
      requestedLevel = level.empty() && !engine.levels().empty()
                           ? engine.levels().front().directory
                           : std::string(level);
      std::printf("menu: launching level %s\n", requestedLevel.c_str());
    });
    engine.console().bind("openbf2.quit", [&](const obf2::con::Command&) {
      std::printf("menu: quit\n");
      menuQuit = true;
    });
    const std::string background = findMenuBackground(files);
    std::printf("  state: %s, the menu's background: %s\n",
                std::string(obf2::engine::stateName(engine.state())).c_str(),
                background.empty() ? "(none)" : background.c_str());

    scene.meshes.push_back(buildScreenQuad("#000000"));
    introQuad = static_cast<int>(scene.meshes.size()) - 1;
    // The menu is `mainMenu.swf` played by `obf2::flash`, the same movie
    // the original opens; the engine only supplies the background it is
    // drawn over (docs/research/11-ruffle-menu.md).
    (void)background;

    std::printf("  localisation: %zu strings, levels: %zu\n", engine.lexicon().size(),
                engine.levels().size());

    // --- loading screen ---
    //
    // Not part of the menu: the original draws it with the engine too,
    // out of the level's own `Info/<level>.desc` and `loadmap.png`.
    const LoadedFont screenFont = loadFont(files, "Fonts/800/dynamicText_13");
    const auto* first = engine.levels().empty() ? nullptr : &engine.levels().front();
    if (screenFont.valid && first != nullptr) {
      obf2::font::TextLayout layout;
      layout.screenWidth = 1280;
      layout.screenHeight = 720;
      auto addText = [&](std::string_view text, float x, float y, float scale) {
        layout.x = x;
        layout.y = y;
        layout.scale = scale;
        auto geometry = obf2::font::buildText(screenFont.font, text, layout, screenFont.atlasPath);
        if (geometry.indices.empty()) return -1;
        scene.meshes.push_back(std::move(geometry));
        return static_cast<int>(scene.meshes.size()) - 1;
      };

      const std::string image = first->loadImage.empty() ? std::string("#101418") : first->loadImage;
      scene.meshes.push_back(buildScreenQuad(image));
      loadingQuad = static_cast<int>(scene.meshes.size()) - 1;

      loadingTextQuads.push_back(addText(first->displayName, 64.0f, 520.0f, 2.0f));

      // The map's description is the same lexicon string the game shows
      // (the locid from <briefing> in the .desc).
      if (!first->briefingKey.empty()) {
        const std::string_view briefing = engine.lexicon().text(first->briefingKey);
        float y = 570.0f;
        for (const auto& line : obf2::font::wrapText(screenFont.font, briefing, 900.0f, 1.1f)) {
          if (y > 690.0f) break;
          loadingTextQuads.push_back(addText(line, 64.0f, y, 1.1f));
          y += 18.0f;
        }
      }
      loadingTextQuads.erase(std::remove(loadingTextQuads.begin(), loadingTextQuads.end(), -1),
                             loadingTextQuads.end());
    }

    if (args.screen == "menu") engine.skipAllMovies();
    if (args.screen == "loading" && !engine.levels().empty()) {
      engine.skipAllMovies();
      engine.startLoading(engine.levels().front().directory);
    }
  } else if (args.levelName.empty()) {
    std::optional<obf2::mesh::RenderMesh> single;
    if (!args.objectName.empty()) {
      registry = buildRegistry(files);
      std::printf("registry: %zu templates\n", registry.size());
      single = buildObjectMesh(files, registry, args.objectName, args, true);
      if (!single) {
        std::fprintf(stderr, "could not assemble the object %s\n", args.objectName.c_str());
        return 1;
      }

      // Skeletal animation: we put the mesh into a pose from the clips. There may be
      // several clips — the engine blends them per bone (the weapon moves the upper
      // body, the movement the legs), and each touches only its own bones.
      if (!args.animationPaths.empty() && !single->skin.empty()) {
        const std::string skeletonPath =
            args.skeletonPath.empty()
                ? std::string("objects/soldiers/Common/Animations/3p_setup.ske")
                : args.skeletonPath;

        const auto skeletonBytes = files.read(skeletonPath);
        std::string skinError;
        const auto skeleton =
            skeletonBytes ? obf2::mesh::loadSkeleton(*skeletonBytes, &skinError) : std::nullopt;
        if (!skeleton) {
          std::fprintf(stderr, "skeleton: %s\n", skinError.c_str());
        } else {
          std::vector<obf2::mesh::BoneAnimation> clips;
          for (const std::string& path : args.animationPaths) {
            const auto bytes = files.read(path);
            if (!bytes) {
              std::fprintf(stderr, "no animation %s\n", path.c_str());
              continue;
            }
            auto clip = obf2::mesh::loadBoneAnimation(*bytes, &skinError);
            if (!clip) {
              std::fprintf(stderr, "animation %s: %s\n", path.c_str(), skinError.c_str());
              continue;
            }
            clips.push_back(std::move(*clip));
          }

          std::vector<obf2::mesh::PoseStage> stages;
          const auto frameIndex = static_cast<std::uint32_t>(args.frame < 0 ? 0 : args.frame);
          for (const auto& clip : clips) {
            // The frame is taken cyclically: the clips are of different lengths (the
            // legs 16 frames, the weapon 36), while we show one moment.
            const std::uint32_t frame =
                clip.frameCount == 0 ? 0 : frameIndex % clip.frameCount;
            stages.push_back(obf2::mesh::PoseStage{&clip, frame, 1.0f});
            std::printf("  clip: %zu tracks, %u frames -> frame %u\n", clip.boneIds.size(),
                        clip.frameCount, frame);
          }

          if (!stages.empty()) {
            const auto pose = obf2::mesh::poseSkeleton(*skeleton, stages);
            obf2::mesh::RenderMesh posed = *single;
            obf2::mesh::skinMesh(*single, pose, posed);
            single = std::move(posed);
          }
        }
      }
    } else {
      single = loadMesh(files, args.meshPath, args.geometryIndex, args.lodIndex, true);
      if (!single) return 1;
    }

    const obf2::Vec3f boundsMin{single->bounds.min.x, single->bounds.min.y, single->bounds.min.z};
    const obf2::Vec3f boundsMax{single->bounds.max.x, single->bounds.max.y, single->bounds.max.z};
    scene.center = (boundsMin + boundsMax) * 0.5f;
    scene.radius = std::max(0.001f, obf2::length(boundsMax - boundsMin) * 0.5f);
    scene.add(std::move(*single), obf2::Mat4::identity());
  }

  // --- GPU --------------------------------------------------------------

  obf2::gfx::WindowDesc desc;
  desc.title = "OpenBattlefield2";
  desc.width = args.width;
  desc.height = args.height;
  std::string error;
  auto device = obf2::gfx::Device::create(desc, &error);
  if (!device) {
    std::fprintf(stderr, "could not create the device: %s\n", error.c_str());
    return 1;
  }
  {
    // In pixels, not in points: on a Retina display the two differ by two, and
    // what we draw into is the pixels.
    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSizeInPixels(device->window(), &windowWidth, &windowHeight);
    std::printf("GPU backend: %s | window %dx%d (asked for %dx%d)\n",
                std::string(device->driver()).c_str(), windowWidth, windowHeight, args.width,
                args.height);
  }

  auto renderer = obf2::gfx::MeshRenderer::create(*device, &error);
  if (!renderer) {
    std::fprintf(stderr, "renderer: %s\n", error.c_str());
    return 1;
  }

  if (level) {
    // In map mode the fog only gets in the way: from above it eats the whole level.
    const float fogEnd = args.topDown ? 0.0f : level->terrain.fogEnd;
    renderer->setFog(obf2::gfx::MeshRenderer::Fog{
        obf2::gfx::Color{level->terrain.fogColor.x, level->terrain.fogColor.y,
                         level->terrain.fogColor.z, 1.0f},
        level->terrain.fogStart, fogEnd, level->terrain.fogBase,
        level->terrain.fogFloor});
    renderer->setTerrainLighting(
        obf2::gfx::Color{level->terrain.terrainSunColor.x, level->terrain.terrainSunColor.y,
                         level->terrain.terrainSunColor.z, 1.0f},
        obf2::gfx::Color{level->terrain.terrainSkyColor.x, level->terrain.terrainSkyColor.y,
                         level->terrain.terrainSkyColor.z, 1.0f});
    const obf2::level::Lighting& lighting = level->lighting;
    // The world's samplers follow the profile's texture-filtering level.
    renderer->setTextureFiltering(engine.settings().video.textureFilteringQuality);
    renderer->setStaticSpecular(
        obf2::gfx::Color{lighting.staticSpecularColor.x, lighting.staticSpecularColor.y,
                         lighting.staticSpecularColor.z, 1.0f},
        // `StaticGloss` as the engine gives it, measured in a frame dump of the
        // original (`psc c2` = 0.2 on 678 draws of one Karkand frame). No level
        // sets it; a material can, and we do not read that yet.
        0.2f);
    renderer->setVegetationLighting(
        obf2::gfx::Color{lighting.treeSunColor.x, lighting.treeSunColor.y, lighting.treeSunColor.z,
                         1.0f},
        obf2::gfx::Color{lighting.treeAmbientColor.x, lighting.treeAmbientColor.y,
                         lighting.treeAmbientColor.z, 1.0f});
    renderer->setStaticLighting(
        obf2::gfx::Color{lighting.staticSunColor.x, lighting.staticSunColor.y,
                         lighting.staticSunColor.z, 1.0f},
        obf2::gfx::Color{lighting.staticSkyColor.x, lighting.staticSkyColor.y,
                         lighting.staticSkyColor.z, 1.0f},
        lighting.sunDirection,
        obf2::gfx::Color{lighting.singlePointColor.x, lighting.singlePointColor.y,
                         lighting.singlePointColor.z, 1.0f});
    std::printf("  static lighting: sun %.2f/%.2f/%.2f, sky %.2f/%.2f/%.2f, from %.2f/%.2f/%.2f\n",
                lighting.staticSunColor.x, lighting.staticSunColor.y, lighting.staticSunColor.z,
                lighting.staticSkyColor.x, lighting.staticSkyColor.y, lighting.staticSkyColor.z,
                lighting.sunDirection.x, lighting.sunDirection.y, lighting.sunDirection.z);
  }

  int texturesLoaded = 0, texturesMissing = 0;
  std::unordered_map<std::string, std::optional<obf2::texture::Texture>> textureCache;
  // The cache is filled from several threads before the upload — see
  // `cacheTexture` below. The lock is held only around the map itself, never
  // around the unpacking and the decoding, which is the part worth spreading.
  std::mutex textureCacheMutex;

#if OBF2_HAVE_FLASH
  // The menu's movie. A frame from it is handed to the resolver under the name
  // `#flash` — the same as colour fills: it is not a file but an image we made
  // ourselves.
  obf2::flash::Movie flashMovie;
  obf2::texture::Texture flashTexture;
  // The background under the movie. `mainMenu.swf` holds no reference to
  // `images/background/` — the engine draws it, and Flash lands on top with a
  // transparent stage. We take the same picture as our own menu did.
  std::string flashBackground;
  obf2::gfx::GpuMesh flashBackgroundMesh;
  bool flashBackgroundReady = false;
  // The menu is the game's own movie. `--flash` only overrides which one:
  // in boot mode we open `mainMenu.swf` out of the mod, exactly what the
  // original opens (docs/functions/menu-bridge.md).
  //
  // It lives in `Menu_client.zip`, mounted as `Menu`, and nothing is unpacked
  // (rule 5) — so the player is given a reader over the same file system the
  // rest of the engine uses, and the movie is opened from bytes. Its pictures
  // and the movies it loads next to itself come through the same reader.
  const std::string menuMoviePath = "Menu/External/FlashMenu/mainMenu.swf";
  std::string flashPath = args.flashSwf;
  if (!flashPath.empty() || bootMode) {
    flashBackground = findMenuBackground(files);
    obf2::flash::Movie::setFileReader(
        [&files](const std::string& path) { return files.read(path); });

    bool opened = false;
    if (!flashPath.empty()) {
      opened = flashMovie.open(flashPath);
    } else if (files.exists(menuMoviePath)) {
      flashPath = menuMoviePath;
      const auto movie = files.read(menuMoviePath);
      opened = movie && flashMovie.openFromMemory(*movie, menuMoviePath);
    } else {
      std::printf("Flash: the menu's movie is not in the archives — %s\n",
                  menuMoviePath.c_str());
    }

    if (opened) {
      std::printf("Flash: %s, stage %u x %u\n", flashPath.c_str(), flashMovie.width(),
                  flashMovie.height());
      flashTexture.format = obf2::texture::Format::Bgra8;
      flashTexture.width = flashMovie.width();
      flashTexture.height = flashMovie.height();
      flashTexture.mips.push_back(obf2::texture::MipLevel{
          flashTexture.width, flashTexture.height, 0,
          static_cast<std::uint32_t>(flashTexture.width * flashTexture.height * 4)});
      flashTexture.data.assign(static_cast<std::size_t>(flashTexture.width) * flashTexture.height * 4,
                               std::byte{0});
    } else if (!flashPath.empty()) {
      std::printf("Flash: did not open — %s\n", flashPath.c_str());
    }
  }
  // A step of the movie and the transfer of the frame into a texture. RGBA -> BGRA:
  // our texture loader expects the channel order of a DDS.
  auto flashStep = [&]() {
    if (!flashMovie.isOpen()) return;
    flashMovie.advance();
    const auto& rgba = flashMovie.render();
    if (rgba.size() != flashTexture.data.size()) return;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
      flashTexture.data[i + 0] = static_cast<std::byte>(rgba[i + 2]);
      flashTexture.data[i + 1] = static_cast<std::byte>(rgba[i + 1]);
      flashTexture.data[i + 2] = static_cast<std::byte>(rgba[i + 0]);
      flashTexture.data[i + 3] = static_cast<std::byte>(rgba[i + 3]);
    }
  };
#endif
  // The half of `resolveTexture` that touches files, kept apart so that it can
  // be called from several threads at once — filling the cache before the
  // upload is the one place where that pays. It hands back a pointer into the
  // cache, which an unordered_map keeps valid however much it grows, so a
  // texture that is only being cached is never copied.
  auto cacheTexture =
      [&](const std::string& path) -> const std::optional<obf2::texture::Texture>* {
    {
      const std::lock_guard<std::mutex> lock(textureCacheMutex);
      if (const auto cached = textureCache.find(path); cached != textureCache.end()) {
        return &cached->second;
      }
    }

    auto bytes = files.read(path);
    // In the game the paths point at `.tga` while the archives hold `.dds` — that is
    // how it is with the level's map, for instance: BF2.exe asks for
    // `Levels/%s/Hud/Minimap/ingameMap.tga` while client.zip has only
    // `ingameMap.dds`. So we try the compressed variant by the same path.
    std::string swapped;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".tga") == 0) {
      swapped = path.substr(0, path.size() - 4) + ".dds";
      if (!bytes) bytes = files.read(swapped);
    }
    if (!bytes) bytes = files.read(obf2::joinAssetPath("objects", path));
    if (!bytes && !swapped.empty()) bytes = files.read(obf2::joinAssetPath("objects", swapped));
    // The HUD's paths are counted from the interface texture directory — the same as
    // is visible in `nametags.setTexture Menu/HUD/Texture/...`.
    if (!bytes) bytes = files.read(obf2::joinAssetPath("menu/hud/texture", path));
    if (!bytes && !swapped.empty()) {
      bytes = files.read(obf2::joinAssetPath("menu/hud/texture", swapped));
    }

    std::optional<obf2::texture::Texture> decoded;
    std::string textureError;
    if (bytes) decoded = obf2::texture::loadImage(*bytes, &textureError);

    const std::lock_guard<std::mutex> lock(textureCacheMutex);
    // Two threads may have asked for the same texture at once; the first one in
    // wins and the second's copy is dropped.
    const auto [where, inserted] = textureCache.emplace(path, std::move(decoded));
    if (inserted) {
      if (where->second) {
        ++texturesLoaded;
      } else {
        ++texturesMissing;
        if (bytes && texturesMissing <= 6) {
          std::printf("    the texture does not read: %s (%s)\n", path.c_str(),
                      textureError.c_str());
        }
      }
    }
    return &where->second;
  };

  auto resolveTexture =
      [&](const std::string& mapName) -> std::optional<obf2::texture::Texture> {
    // Names starting with '#' are not files but colours: the game sometimes gives a
    // colour as a number (renderer.waterColor), and we also need fills for screens
    // with no image.
    if (level && mapName == obf2::level::kWaterColorMap) {
      const obf2::Vec3f color = level->terrain.waterColor;
      return obf2::texture::solidColor(color.x, color.y, color.z);
    }
#if OBF2_HAVE_FLASH
    if (mapName == "#flash" && flashMovie.isOpen()) return flashTexture;
#endif
    if (mapName.size() == 7 && mapName[0] == '#') {
      const auto channel = [&](std::size_t offset) {
        return static_cast<float>(std::stoi(mapName.substr(offset, 2), nullptr, 16)) / 255.0f;
      };
      return obf2::texture::solidColor(channel(1), channel(3), channel(5));
    }

    const auto* cached = cacheTexture(obf2::normalizeAssetPath(mapName));
    return cached != nullptr ? *cached : std::nullopt;
  };


  // --- the in-game HUD ---------------------------------------------
  //
  // The same hudBuilder as the menu, but the tree comes from the game's files:
  // Global -> GlobalHud -> IngameHud -> dozens of sub-groups through `split`.
  obf2::hud::Builder ingameHud;
  std::vector<int> hudQuads;
  // A node's colour by mesh index: in the game setNodeColor multiplies the texture,
  // and without it the yellow captions, the tabs' highlight and the coloured bars
  // come out plain white.
  std::map<int, obf2::hud::Color> hudTints;
  // A copy of the context for rebuilding the live nodes in the drawing loop:
  // hudContext itself lives in the level loading block.
  obf2::hud::Context hudDynamicContext;
  // The spawn screen's state and its geometry. It is the only one rebuilt on the
  // fly: its contents depend on the chosen kit, the team and the tab.
  // The spawn screen's state and commands live in the module
  // `obf2/hud/spawn_interface.h` — the name comes from the original
  // (`Code/BF2/Menu/Hud/SpawnInterface.cpp`).
  obf2::hud::SpawnInterface spawnScreen;
  int& selectedKit = spawnScreen.mutableChoice().kit;
  int& selectedTeam = spawnScreen.mutableChoice().team;
  bool& membersTab = spawnScreen.mutableChoice().membersTab;
  // The chosen spawn point — the circle's index in spawnContext.spawnMarkers.
  //
  // By default the first of ours is chosen — otherwise DONE would have nothing to
  // send, and a NESelectSpawnGroup event with zero means "not chosen" to the server
  // (`Player::getSpawnGroup() > 0`). **Source not found:** which group the game
  // substitutes by default we have not reversed. Debt.
  int selectedSpawn = 0;
  // The control point's id for every circle, in the same order. It is exactly what
  // the server expects: in the engine the player sends not coordinates but a
  // group's number, and a group is the set of points of one flag
  // (docs/functions/spawn.md).
  std::vector<int> spawnMarkerPoints;
  bool spawnDirty = false;
  struct OwnedPiece {
    obf2::gfx::GpuMesh mesh;
    obf2::hud::Color tint;
    // Not a mesh of ours but a live node's place: its pieces are substituted here
    // while the frame is assembled (see `hudDynamic`).
    const obf2::hud::Node* live = nullptr;
  };
  std::vector<OwnedPiece> spawnPieces;
  // The combat HUD: not baked once and for all but rebuildable — its variables are
  // written by the engine every frame (0x78d0f0), not once at the level's start.
  std::vector<OwnedPiece> ingamePieces;
  std::function<std::vector<obf2::hud::DrawPiece>()> buildIngamePieces;
  std::function<void()> rebuildIngame;
  bool hudDirty = false;
  bool ingameReported = false;
  std::function<void(bool, bool)> updateHudVariables;
  std::function<void(int)> applyHudState;
  // What DONE does. In our own game it is a direct request to our server, in a
  // network one three engine events in a row (NESelectTeam, NESelectKit,
  // NESelectSpawnGroup, docs/functions/network-events.md).
  // Returns whether the request really went to the server: if not, the spawn screen
// has to stay where it is (otherwise the result is a frozen picture).
std::function<bool(int team, int kit, int group)> requestSpawn;
  // The moving corner regions. In `Menu/Ingame` their X is not a constant but a
  // graph variable, and what the file holds is precisely the **hidden** position:
  // BottomLeft_XPos = -295, BottomRight_XPos = 503. The shown one for the right is
  // there too — BottomRight_oldXPos = 201. They are driven by
  // SetVariableSineAction at speed 600.
  //
  // That is why in the original no wide plate under the health is visible behind
  // the spawn screen: the node BottomLeftBar (400x39, healthBackGround.tga) has no
  // show variable at all, and it is the region driving away that hides it.
  // The left region is a state machine from the client (obf2/hud/bottom_left.h).
  obf2::hud::BottomLeftPanel bottomLeft;
  float& bottomLeftX = bottomLeft.x;
  float bottomRightX = obf2::hud::kBottomRightHiddenX;
  // The right region's alpha is driven by the same graph: `BottomRight_alpha` is
  // bound to the HUD object's field +0x10, and that same field is registered as the
  // HUD variable `BottomRightAlpha` (`BF2.exe`, 0x7a62c0). Half the right corner's
  // nodes hang on it (`HudElementsPlayer.con`).
  float bottomRightAlpha = 0.0f;
  obf2::hud::BottomLeftMode bottomLeftMode = obf2::hud::BottomLeftMode::Hidden;
  // The map: its size is driven by its own animation, and MapFullSize and
  // MapMinSize are derived from the size (obf2/hud/map_node.h).
  obf2::hud::MapNode mapNode;
  // The minimap compass's angle — two smoothers in a row (the same place).
  obf2::hud::MapAngle mapAngle;
  // The `Menu/Ingame` graph — the very system the engine animates the HUD with. The
  // corner regions travel by it, and every speed lies in the file rather than here.
  obf2::meme::Graph ingameGraph;
  // The map's rectangle on the node was moved by someone else (a spawn screen
  // rebuild, a screenshot on a key) — the next frame has to put its own back.
  bool mapRectStale = true;
  // The map's window at zoom 0 is a square around the combat area. That is what was
  // measured from the original's frame dump (docs/research/03-frame-dump.md);
  // magnification divides its half-side by `pow(2.3, zoom)`.
  float mapBaseCentreU = 0.5f, mapBaseCentreV = 0.5f;
  float mapBaseHalfU = 0.0f, mapBaseHalfV = 0.0f;
  // The look angle lives between frames: the mouse gives only a delta. It is
  // declared here because the console command `openbf2.look` reads it too.
  float yaw = 0.0f;
  // The map key (`c_GIMapSize`, constant 0x23 in BF2.exe's control table at
  // 0x690244) is a toggle, not "hold". In combat it moves the HUD from state 0 into
  // state 2, where the data leaves the map alone on screen.
  bool bigMap = false;
  bool mapKeyWasDown = false;
  // There was no state yet: -1 leads into the first switch's `default` branch, that
  // is "clear absolutely everything" (0x78653c).
  int hudStatePrevious = -1;
  bool bottomRightShow = false;
  float bottomRightShownX = obf2::hud::kBottomRightShownX;
  // Nodes appearing and disappearing over time — what the MemeFile graph governs in
  // the game (see obf2/hud/animation.h).
  obf2::hud::Animator hudAnimator;
  std::chrono::steady_clock::time_point lastAnimationTick = std::chrono::steady_clock::now();
  std::function<void()> rebuildSpawn;
  // These two are needed by the rebuild, and it is called from the drawing loop —
  // that is, already outside the level loading block. Keeping them inside is not
  // allowed: a reference in the lambda would become dangling.
  std::function<void()> applySpawnState;
  obf2::hud::Context spawnContext;
  // The same case as with spawnContext: the combat HUD is now rebuilt from the
  // frame loop, and the lambda holds the context by reference. While it was local
  // to the setup block, after the block exited rebuildIngame read dead memory — the
  // path to the map's picture arrived as rubbish, and the minimap was not drawn.
  obf2::hud::Context hudContext;
  // The level's capture points — the map's markers are rebuilt from them every
  // time: a flag stands on each, while a spawn selection circle stands only on our own.
  std::vector<obf2::level::ControlPoint> hudControlPoints;
  // The screens visible only while a key is held: the scoreboard, the radio, the spawn.
  // The geometry is baked in advance — it does not change, only whether to draw it
  // this frame does.
  struct KeyScreen {
    std::string group;
    std::string action;  // the action's name in the ControlMap, not a key
    std::vector<int> quads;
    // The HUD state this screen belongs to (docs/functions/hud-states.md).
    // The spawn screen is state 1, and in the game it is **not held with a key**: it
    // stands until the player has spawned. The scoreboard is state 9, and that one
    // really is on a key.
    int state = -1;
    bool heldByKey = true;
  };
  std::vector<KeyScreen> keyScreens;
  obf2::game::ControlMap controls;
  obf2::hud::VariableMap hudVariables;
  std::map<std::string, std::string> hudStrings;
  std::map<std::string, float> hudValues;
  // The plates' alpha. This is not our invention and not zero: BF2.exe takes the
  // alpha from the player's profile (GeneralSettings.setHUDTransparency /
  // setMinimapTransparency, 204 by default), multiplies it by 1/255 — the constant
  // at 0x8a4a64 — and puts it into the variables MenuBackgroundAlpha and
  // MenuMapAlpha (0x4b68d3 and 0x4b6907). Until now we set zero, and the wide plates
  // under the health, the stamina and the ammo were not drawn at all.
  // The plates' alpha from the profile — also the "base" for the dimmed variants in
  // 0x78b600.
  const float backgroundAlpha =
      static_cast<float>(engine.settings().general.hudTransparency) / 255.0f;
  const std::map<std::string, float> hudAlpha = {
      {"MenuBackgroundAlpha",
       static_cast<float>(engine.settings().general.hudTransparency) / 255.0f},
      {"MenuMapAlpha",
       static_cast<float>(engine.settings().general.minimapTransparency) / 255.0f},
  };
  // The nodes whose contents change in the game: captions and bars. We rebuild the
  // geometry for them, but only when a value really changed.
  struct DynamicNode {
    const obf2::hud::Node* node = nullptr;
    std::string shownText;
    float shownValue = -1.0f;
    // A picture's rotation angle (`setPictureNodeRotateVariable`). The minimap's
    // compass turns every frame, and rebaking **the whole** HUD through it
    // is not allowed: 91 meshes per frame is exactly the stutter one can see.
    float shownAngle = 0.0f;
    // A node may give more than one piece: the map also draws the capture points'
    // icons and their captions.
    std::vector<OwnedPiece> pieces;
    // Whether we built at all. Without this the compass would not appear until the
    // player turned the mouse: its angle equals the shown one from the very start.
    bool built = false;
  };
  std::vector<DynamicNode> hudDynamic;
  const LoadedFont hudFont = bootMode ? LoadedFont{} : loadFont(files, "Fonts/800/dynamicText_13");
  // The nodes' fonts, by their style. Loaded on demand: the HUD has a dozen of
  // them, and there is no need to read all 358 from the archive.
  std::map<std::string, LoadedFont> hudFonts;
  obf2::hud::Screen hudScreen;
  if (!bootMode && hudFont.valid) {
    // The dictionary: without it the HUD shows keys ("HUD_TEXT_MENU_SCORE_ROUNDSWON")
    // instead of text. In the menu boot loads it, while in combat nobody loaded it —
    // hence the keys on screen.
    engine.loadLexicon(files);

    obf2::con::Interpreter hudInterpreter(
        files, [&](const obf2::con::Command& command) { ingameHud.feed(command); });
    hudInterpreter.runFile("Menu/HUD/HudSetup/HudSetupMain.con");

    // The HUD's animation system is the file `Menu/Ingame`, not code: it holds the
    // corner regions with their variable bindings, the show conditions and the
    // actions that move those variables (obf2/meme/graph.h).
    if (const auto memeData = files.read("Menu/Ingame")) {
      std::string memeError;
      if (ingameGraph.load(*memeData, &memeError)) {
        std::printf("  the Ingame graph: nodes %zu, variables %zu\n",
                    ingameGraph.file().objects().size(),
                    ingameGraph.variables().all().size());
      } else {
        std::printf("  the Ingame graph was not read: %s\n", memeError.c_str());
      }
    }
    // Link the tree: until then the nodes' coordinates stay relative to their parent
    // and the HUD scatters over the screen.
    ingameHud.finish();

    // The control layout comes from the game's data. We ask about an action, and
    // which key it is `Settings/Controls.con` decides.
    obf2::con::Interpreter controlInterpreter(
        files, [&](const obf2::con::Command& command) { controls.feed(command); });
    controlInterpreter.runFile("Settings/Controls.con");

    // The state table and the derived variables live in `obf2/hud/states.h`
    // together with their test: both the table from BF2.exe and the addresses of the
    // places that write each variable.
    // The map's rectangles come from the data (`setMiniPos`/`setMaxiSize` and the
    // rest); the animation takes them from the assembled tree so as not to parse twice.
    for (const obf2::hud::Node& node : ingameHud.nodes()) {
      if (node.type == obf2::hud::NodeType::Map || node.type == obf2::hud::NodeType::MiniMap) {
        mapNode.takeRects(node);
        break;
      }
    }

    applyHudState = [&](int state) {
      // The map changes its target by the state number — verbatim 0x777dc0.
      mapNode.applyState(state);
      // A transition rather than "set the state": in the game the first switch goes
      // on the old state, the second on the new one (0x786260).
      if (obf2::hud::applyState(hudVariables, hudStatePrevious, state)) hudDirty = true;
      hudStatePrevious = state;
    };

    // The combat HUD is state 0.
    applyHudState(0);

    // ToggleScore is the scoreboard's "Players" tab. That it is the default is
    // visible in the binary: the flag field (Scoreboard+0x365) has exactly one
    // constant store, `movb $0x1, 0x365(%esi)` at 0x7a48f7.
    hudVariables["ToggleScore"] = true;

    // Source not found: CPInterfaceEnabled (the HUD object's field 0xa8) is written
    // by many places in the game, and which of them is ours is not established.
    // Without it the capture points' bars are not visible. Debt.
    hudVariables["CPInterfaceEnabled"] = true;

    // --- the spawn screen: seven kits -------------------------------
    //
    // The nodes Kit0..Kit6 in HudElementsSpawn.con show nothing by themselves: each
    // hangs on its own variable, and the contents arrive as variables too —
    // KitName<N>String (the caption's key) and KitIcon<N>Path (the icon). The rows
    // themselves come from the level and from the kits' templates
    // (`obf2/hud/kit_list.h`); they are filled in by `applySpawnState` below,
    // because the list belongs to the side and the side can change.

    // --- the spawn screen's backend --------------------------------
    //
    // A button in the HUD has no logic of its own: it runs a console command from
    // `setButtonNodeConCmd` (docs/functions/hud-commands.md). This screen has seven
    // of them, and here they are. The state is kept right here, and a change requires
    // a rebuild — we bake the geometry in advance.
    selectedKit = args.kit;
    selectedTeam = args.team == 2 ? 2 : 1;
    {
      obf2::engine::Console& console = engine.console();
      console.bind("spawnManager.setPlayerKit", [&](const obf2::con::Command& command) {
        selectedKit = command.argInt(0).value_or(selectedKit);
        spawnDirty = true;
      });
      console.bind("spawnManager.setPlayerTeam", [&](const obf2::con::Command& command) {
        selectedTeam = command.argInt(0).value_or(selectedTeam);
        spawnDirty = true;
      });
      console.bind("SpawnManager.toggleMembers", [&](const obf2::con::Command& command) {
        membersTab = command.argInt(0).value_or(0) != 0;
        spawnDirty = true;
      });
      console.bind("hudManager.setDone", [&](const obf2::con::Command& command) {
        if (command.argInt(0).value_or(1) == 0) {
          spawnScreen.setRequested(false);
          return;
        }
        // The spawn group's number is the control point's id for the chosen circle.
        const bool haveMarker =
            selectedSpawn >= 0 && selectedSpawn < static_cast<int>(spawnMarkerPoints.size());
        const int group =
            haveMarker ? spawnMarkerPoints[static_cast<std::size_t>(selectedSpawn)] : 0;
        std::printf("  spawn screen: DONE — team %d, kit %d, point %d\n", selectedTeam,
                    selectedKit, group);

        // **We close the screen only when the request really went out.**
        // We used to set `spawnScreen.requested()` first, and when no point turned
        // out to be chosen the request did not go — while the screen was already
        // gone. The result was a frozen picture with no player and no way out: that
        // is "it hung after DONE".
        if (!haveMarker || !requestSpawn) {
          std::printf("    no spawn point chosen — the request did not go, the screen stays\n");
          return;
        }
        spawnScreen.setRequested(requestSpawn(selectedTeam, selectedKit, group));
        if (!spawnScreen.requested()) {
          std::printf("    the request did not go — the screen stays\n");
        }
      });
      console.bind("hudItems.setBool", [&](const obf2::con::Command& command) {
        // `hudItems.setBool <name> <0|1>` — this is how the interface turns its own
        // flags on, SetSpawnPoint among them.
        if (command.args.size() >= 2) {
          hudVariables[std::string(command.argStr(0))] = command.argInt(1).value_or(0) != 0;
          spawnDirty = true;
        }
      });
      // These two have nothing to work on yet, but the command has to be eaten —
      // otherwise the console will consider it unknown.
      // Our own command, not the engine's: the spawn circles have no nodes in the
      // data, the map catches them itself, so the game has no console name for them.
      // It is needed for the checks — so a spawn point can be chosen by a command
      // rather than by pointing the mouse at a pixel.
      // `openbf2.spawnAt <group number>` — ask to spawn straight by the group number
      // the server named (`CreateSpawnGroupEvent`, type 57). Needed for the checks:
      // we still place the circles on the map from the level's data, and they do not
      // always agree with whose group it really is — while the server spawns only in
      // its own.
      console.bind("openbf2.spawnAt", [&](const obf2::con::Command& command) {
        const int group = command.argInt(0).value_or(0);
        if (remote == nullptr || group <= 0) return;
        std::printf("  spawn screen: a direct spawn in group %d\n", group);
        remote->askSpawnGroup(selectedTeam, selectedKit, group);
        spawnScreen.setRequested(true);
      });
      // `openbf2.toggleMap [0|1]` — the same as the map key. Ours, not the engine's:
      // in the original the big map is governed only by the `c_GIMapSize` action from
      // the layout, which has no console name. Needed for the checks — so the map can
      // be captured by a command rather than by holding a key.
      console.bind("openbf2.toggleMap", [&](const obf2::con::Command& command) {
        bigMap = command.args.empty() ? !bigMap : command.argInt(0).value_or(0) != 0;
        std::printf("  map: the big presentation %s\n", bigMap ? "on" : "off");
      });
      // `MiniMap.setZoom <index>` — the engine's command, not ours: in the data it
      // hangs on the `MapZoom` button (HudElementsMapMenu.con), and in the client
      // 0x57a97e writes the number straight into the map node's field +0x6d0.
      // With no argument — the next zoom in a cycle, because that is how
      // the button behaves: the data passes it 0 while it toggles.
      console.bind("MiniMap.setZoom", [&](const obf2::con::Command& command) {
        const int next = command.args.empty()
                             ? (mapNode.zoomIndex() + 1) % obf2::hud::kMapZoomLevels
                             : command.argInt(0).value_or(0);
        mapNode.setZoomIndex(next);
        std::printf("  map: zoom %d\n", mapNode.zoomIndex());
      });
      // `openbf2.look <angle>` — turn the view to the given angle in degrees.
      // Ours too, for the checks: otherwise the minimap's compass cannot be captured
      // in a screenshot, because the angle comes from the mouse.
      console.bind("openbf2.look", [&](const obf2::con::Command& command) {
        yaw = command.argFloat(0).value_or(0.0f);
        std::printf("  view: angle %.1f\n", static_cast<double>(yaw));
      });
      console.bind("openbf2.selectSpawn", [&](const obf2::con::Command& command) {
        selectedSpawn = command.argInt(0).value_or(0);
        spawnDirty = true;
        std::printf("  spawn screen: circle %d of %zu chosen\n", selectedSpawn,
                    spawnMarkerPoints.size());
      });
      console.bind("spawnManager.selectNextUnlock", [](const obf2::con::Command&) {});
      console.bind("spawnManager.commitSuicide", [](const obf2::con::Command&) {});
      console.bind("sound.playSound", [](const obf2::con::Command&) {});
    }

    // The HUD's derived variables. In the game they are written not by a list at
    // startup but by two per-frame functions, and each is named here by its address:
    //
    //   0x466930 — the map's size and what follows from it;
    //   0x78d0f0 — the combat set by the current player.
    //
    // That is exactly why in the original no health or ammo bars are visible behind
    // the spawn screen: there is no player yet, and 0x78d2d9 clears the whole set.
    updateHudVariables = [&](bool hasPlayer, bool mapFullSize) {
      if (obf2::hud::applyDerived(hudVariables, obf2::hud::WorldView{hasPlayer, mapFullSize})) {
        hudDirty = true;
      }
      // The moving regions' ends are constants from `obf2/hud/animation.h`, and it
      // says where each of them comes from.
      //
      // The left region has **three** positions, not two: hidden (-295), on foot
      // (-137) and in a vehicle (54). 0x78b600 chooses among them by the flags
      // `BottomLeftHealthAlpha` and `BottomLeftVehicleAlpha`. We do not compute those
      // flags yet (see the notes: who writes them is not worked out), so we take the
      // **on foot** position — it is the right one for a player on his own two feet.
      // The vehicle one will be added once there is a source for the flags.
      //
      // The left region's mode is set by 0x78b870: "health" when there is a player
      // and "hide" when there is none. "Vehicle" is when the controlled object is not
      // a soldier; we are always a soldier for now, so we do not turn it on.
      bottomLeftMode =
          hasPlayer ? obf2::hud::BottomLeftMode::Health : obf2::hud::BottomLeftMode::Hidden;
      // On the right the switch is the same as on the left: there is a player — show.
      // The positions are a pair with the left ones too — on foot 337, in a vehicle
      // 165 (`BF2.exe`, 0x7a5b10). We do not turn the vehicle on for the same reason
      // as on the left: our controlled object is always a soldier for now.
      bottomRightShow = hasPlayer;
      bottomRightShownX = obf2::hud::kBottomRightShownX;
    };

    // What DONE does. There are two paths, and both are equally "real":
    //
    //   * our own game — a direct request to our server. It behaves like the engine:
    //     the soldier spawns only when a spawn point has been chosen;
    //   * a real BF2 server — three events in a row, NESelectTeam, NESelectKit,
    //     NESelectSpawnGroup. The order and the pauses between them are verified
    //     against an original server
    //     (docs/functions/network-events.md).
    requestSpawn = [&](int team, int kit, int group) -> bool {
      if (hostedServer != nullptr && !hostedServer->players().empty()) {
        hostedServer->requestSpawn(hostedServer->players().front().id, team, kit, group);
        return true;
      }
      if (remote != nullptr) {
        // The server has to be told **its** spawn group number, not our control point
        // id: on Dalian the server sends 515..518, while in the level's data the flags
        // have 401..404, and they are not related in any way. All they share is the
        // position, so we pass the position — and the connection picks the number when
        // the time comes to send. We used to pick it right here and on a fast press
        // got zero: the groups arrive as events only after the level has loaded.
        const obf2::level::ControlPoint* chosen = nullptr;
        for (const auto& point : hudControlPoints) {
          if (point.id == group) { chosen = &point; break; }
        }
        if (chosen == nullptr) {
          std::printf("  spawn screen: point %d is not in the level's list\n", group);
          return false;
        }
        std::printf("  spawn screen: point %d (%s) at %.0f %.0f\n", group,
                    chosen->nameKey.c_str(), chosen->position.x, chosen->position.z);
        remote->askSpawn(team, kit, chosen->position.x, chosen->position.z,
                         hudContext.mapWorldSize);
        return true;
      }
      std::printf("  spawn screen: there is no server, spawning only closes the screen\n");
      return true;
    };

    // The team's side name comes from the level itself:
    //   gameLogic.setTeamName 1 "CH"
    // For Dalian_plant that is CH and US — in exactly that order, so team one is
    // Chinese. Across all 22 levels the set of names is exactly CH, EU, MEC, US, and
    // the icon directories in Menu_client.zip are called the same.
    const auto teamName = [&](int team) -> std::string {
      if (!level || team < 0 || team > 2) return {};
      return level->teamNames[team];
    };
    // The conversions from a side's name into keys and paths live in
    // `obf2/hud/spawn.h` together with their test.
    const auto teamLabel = [&](int team) { return obf2::hud::armyLabelKey(teamName(team)); };
    const auto teamFlagIcon = [&](int team) { return obf2::hud::teamFlagIcon(teamName(team)); };

    // The team tabs at the top of the spawn screen. In the data the TeamSelectInfo
    // branch hangs on Team1Selected, and inside are two blocks — Team1Selected and
    // Team2Selected.
    // The spawn screen's variables depend on its state, so we keep them in one place
    // and recompute them after every command.
    applySpawnState = [&]() {
      // We do not touch the HUD's state here: the frame sets it by the game's current
      // state. We used to set state 1 here — and after DONE the spawn screen turned
      // itself back on every time it was rebuilt.
      // The map's markers depend on the team, so we assemble them every time.
      // A flag stands on every point, while a spawn selection circle stands only
      // where the point is held by **our** team: in Dalian_plant's data that is
      // visible directly, ObjectTemplate.team gives 1 for powerplant, 2 for
      // constructionsite, while reactors and mainentrance are neutral. Spawning at
      // another team's or a neutral one is not allowed.
      spawnContext.mapMarkers.clear();
      spawnContext.spawnMarkers.clear();
      spawnMarkerPoints.clear();
      for (const auto& point : hudControlPoints) {
        obf2::hud::Context::MapMarker marker;
        marker.worldX = point.position.x;
        marker.worldZ = point.position.z;
        marker.label = point.nameKey;
        marker.texture = obf2::hud::controlPointIcon(point.team == 0 ? "" : teamName(point.team));
        spawnContext.mapMarkers.push_back(std::move(marker));
        if (point.team == selectedTeam) {
          const bool chosen =
              static_cast<int>(spawnContext.spawnMarkers.size()) == selectedSpawn;
          spawnContext.spawnMarkers.push_back(
              obf2::hud::Context::SpawnMarker{point.position.x, point.position.z, chosen});
          spawnMarkerPoints.push_back(point.id);
        }
      }
      hudVariables["Team1Selected"] = selectedTeam != 2;
      hudVariables["Team2Selected"] = selectedTeam == 2;
      // The tabs' captions and flags. In the data they are on the variables
      // Team1NameString / Team1FlagIconPathString (HudElementsSpawn.con),
      // while in the binary one and the same 0x787260 fills them in.
      for (int team = 1; team <= 2; ++team) {
        const std::string index = std::to_string(team);
        hudStrings["Team" + index + "NameString"] =
            std::string(engine.lexicon().text(teamLabel(team)));
        hudStrings["Team" + index + "FlagIconPathString"] = teamFlagIcon(team);
      }
      // The same function also sets the "ours/theirs" pair: its first argument is the
      // player's team, the second the opposite one.
      hudStrings["FriendlyFlagIconPathString"] = teamFlagIcon(selectedTeam);
      hudStrings["EnemyFlagIconPathString"] = teamFlagIcon(selectedTeam == 2 ? 1 : 2);
      // `MapFullSizeAndSpawnShow` used to be set here by hand, because there was no
      // source. Now there is: 0x4668d0 computes it every frame as
      // `MapFullSize AND SpawnShow`, and `applyDerived` already does that
      // (obf2/hud/states.h). We no longer set it by hand.
      // KitsShow / MembersShow is switched by SpawnManager.toggleMembers — a
      // reversed command from hud-commands.md.
      hudVariables["KitsShow"] = !membersTab;
      hudVariables["MembersShow"] = membersTab;
      // The chosen kit is a consequence of spawnManager.setPlayerKit, also a reversed
      // command.
      for (int slot = 0; slot < 7; ++slot) {
        hudVariables["PlayerKitIcon" + std::to_string(slot) + "SelectShow"] = slot == selectedKit;
      }

      // --- the seven kit rows -----------------------------------------
      //
      // Everything a row shows the engine pours into these variables every frame
      // out of the kit's own ObjectTemplate — `HudInformationLayer`, 0x468510
      // (docs/functions/hud-kits.md). The kit of a row is named by the level:
      // `gameLogic.setKit <team> <row> <kit> <soldier>`, so the list changes with
      // the side and is rebuilt here along with the rest of the screen's state.
      for (int slot = 0; slot < obf2::level::Level::kKitsPerTeam; ++slot) {
        const std::string index = std::to_string(slot);
        const std::string& kitName =
            level ? level->kits[selectedTeam][slot] : std::string{};
        const obf2::hud::KitRow row = obf2::hud::buildKitRow(registry, kitName);
        // `Kit<N>Show` is `KitsShow` for every row the kit manager answers for —
        // the engine writes the layer's own flag into the row's flag at the end of
        // each pass (0x468510). A row the level named no kit for stays off.
        hudVariables["Kit" + index + "Show"] = !membersTab && !row.kitTemplate.empty();
        hudStrings["KitName" + index + "String"] = row.nameKey;
        hudStrings["KitIcon" + index + "Path"] = row.icon;
        hudStrings["KitWeaponIcon" + index + "Path"] = row.weaponIcon;
        hudStrings["KitAltWeaponIcon" + index + "Path"] = row.altWeaponIcon;
        hudValues["Kit" + index + "SprintAbility"] = row.sprintAbility;
        // The unlock's picture is shown either way; the arrow says whether the
        // player owns it, and it is the arrow that swaps the greyed-out picture
        // and the padlock for the live one. We have no profile and no unlocks, so
        // the arrow is off — which is what the original drew for the profile the
        // dump was taken with.
        hudVariables["KitUnlock" + index + "Show"] = row.unlock;
        hudVariables["KitUnlockArrow" + index + "Show"] = false;
        hudValues["Kit" + index + "UnlockBlinkAlpha"] = 0.0f;
        for (std::size_t icon = 0; icon < obf2::hud::kMaxAbilityIcons; ++icon) {
          const std::string at = index + "AbilityIcon" + std::to_string(icon);
          const bool has = icon < row.abilityIcons.size();
          hudVariables["Kit" + at + "Show"] = has;
          hudStrings["Kit" + at + "PathString"] = has ? row.abilityIcons[icon] : std::string{};
        }
      }
    };
    applySpawnState();

    // The level's map picture is not set by the HUD: BF2.exe has a template for it
    // `Levels/%s/Hud/Minimap/ingameMap.tga`.
    if (!args.levelName.empty()) {
      hudContext.mapTexture = "Levels/" + args.levelName + "/Hud/Minimap/ingameMap.tga";
    }
    // The game shows not the whole level picture but a square around the combat area.
    // The rule was taken from the original's frame dump: the square's side is 1.2 of
    // the area's larger side, and its centre is the area's centre. For Dalian_plant 16
    // that gives u 0.2694..0.7430 and v up to 0.7677, while the original has 0.2703,
    // 0.7431 and 0.7678 — a match to the third decimal.
    //
    // The picture is oriented so that z grows upwards: v = (1024 - z) / 2048.
    const auto hudGameplay =
        level ? obf2::level::loadGameplayObjects(files, level->name, "gpm_cq", 16)
              : std::optional<obf2::level::GameplayObjects>{};
    if (hudGameplay && !hudGameplay->combatArea.empty()) {
      float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
      hudGameplay->combatArea.bounds(minX, maxX, minZ, maxZ);
      const float world = static_cast<float>(level ? level->primary.size - 1 : 1024) *
                          (level ? level->primary.scale.x : 2.0f);
      const float half = std::max(maxX - minX, maxZ - minZ) * 0.5f * 1.2f;
      const float centerX = (minX + maxX) * 0.5f;
      const float centerZ = (minZ + maxZ) * 0.5f;
      const auto toU = [&](float x) { return (x + world * 0.5f) / world; };
      const auto toV = [&](float z) { return (world * 0.5f - z) / world; };
      hudContext.mapU0 = toU(centerX - half);
      hudContext.mapU1 = toU(centerX + half);
      hudContext.mapV0 = toV(centerZ + half);
      hudContext.mapV1 = toV(centerZ - half);
      hudContext.mapWorldSize = world;
      mapBaseCentreU = (hudContext.mapU0 + hudContext.mapU1) * 0.5f;
      mapBaseCentreV = (hudContext.mapV0 + hudContext.mapV1) * 0.5f;
      mapBaseHalfU = (hudContext.mapU1 - hudContext.mapU0) * 0.5f;
      mapBaseHalfV = (hudContext.mapV1 - hudContext.mapV0) * 0.5f;
      std::printf("  map: combat area %.0f..%.0f / %.0f..%.0f, visible u %.3f..%.3f v %.3f..%.3f\n",
                  minX, maxX, minZ, maxZ, hudContext.mapU0, hudContext.mapU1, hudContext.mapV0,
                  hudContext.mapV1);

      // The capture points: the position, the team and the name's key come from the
      // level — exactly what the server sees. The markers themselves are assembled by
      // applySpawnState, because they depend on the player's team.
      hudControlPoints = hudGameplay->controlPoints;
      // The main HUD is baked once, so the flags for its minimap are assembled right
      // here. There are no selection circles there — they exist only on the spawn
      // screen.
      for (const auto& point : hudControlPoints) {
        obf2::hud::Context::MapMarker marker;
        marker.worldX = point.position.x;
        marker.worldZ = point.position.z;
        marker.label = point.nameKey;
        marker.texture = obf2::hud::controlPointIcon(point.team == 0 ? "" : teamName(point.team));
        hudContext.mapMarkers.push_back(std::move(marker));
      }
      std::printf("  map: capture points %zu\n", hudControlPoints.size());
    }
    hudContext.localize = [&](std::string_view key) { return engine.lexicon().text(key); };
    // Every node's font is the one named in setTextNodeStyle. In the data the path is
    // written as "Fonts/hudFontLocalBold_9.dif" (sometimes with a backslash), while in
    // the archive the fonts lie in two sets: in the root and in the `800` directory.
    // We measure the HUD in the base 800x600, so we take `800`, and the root stays as
    // a fallback.
    hudContext.fontFor = [&](std::string_view style) -> obf2::hud::FontRef {
      std::string key(style);
      for (char& c : key) {
        if (c == '\\') c = '/';
      }
      if (key.size() > 4 && key.compare(key.size() - 4, 4, ".dif") == 0) {
        key.resize(key.size() - 4);
      }
      const auto cached = hudFonts.find(key);
      if (cached != hudFonts.end()) {
        return obf2::hud::FontRef{cached->second.valid ? &cached->second.font : nullptr,
                                  cached->second.atlasPath};
      }
      const std::size_t slash = key.rfind('/');
      const std::string dir = slash == std::string::npos ? std::string() : key.substr(0, slash + 1);
      const std::string name = slash == std::string::npos ? key : key.substr(slash + 1);
      // Some of the fonts lie in language directories rather than in the root: for
      // instance StandardTextBold_15 exists only as English/StandardTextBold_15.
      // So we try four places — the language one and the general one, each with `800`
      // (the set for 800x600) and without it.
      // The order is exactly this: the language directory first, and **without** the
      // `800` subdirectory. The original's frame dump at 800x600 shows a kit's caption
      // 79.2 wide at a height of 11, while the `800` set would give 60.3 by 9 — because
      // there the point size is 13 against 16 in the language directory's root. So
      // `800` is not meant for 800x600, as the name suggested.
      LoadedFont loaded = loadFont(files, dir + "English/" + name);
      if (!loaded.valid) loaded = loadFont(files, dir + "English/800/" + name);
      if (!loaded.valid) loaded = loadFont(files, key);
      if (!loaded.valid) loaded = loadFont(files, dir + "800/" + name);
      const auto placed = hudFonts.emplace(key, std::move(loaded)).first;
      return obf2::hud::FontRef{placed->second.valid ? &placed->second.font : nullptr,
                                placed->second.atlasPath};
    };
    hudContext.isVisible = [&](std::string_view variable) {
      if (variable == "1") return true;
      const auto found = hudVariables.find(std::string(variable));
      return found != hudVariables.end() && found->second;
    };
    hudContext.variableValue = [&](std::string_view variable) -> float {
      // Numeric and boolean variables live in different dictionaries, while a show
      // condition asks for both — see obf2/hud/states.h.
      return obf2::hud::showValue(hudVariables, hudValues, variable);
    };
    // The alpha: we know one variable so far, but an important one. The wide plates
    // under the health and ammo bars (400x39, healthBackground.tga and
    // ammoBackground.tga) hang on exactly it, and in combat it is zero — in the game
    // only a narrow squad strip, 142 units, is visible under the bars.
    hudContext.variableAlpha = [&](std::string_view variable) -> std::optional<float> {
      // The left region's four alphas are driven by its state machine
      // (obf2/hud/bottom_left.h, from BF2.exe 0x78b600). They are what separates the
      // region's two halves: on foot the health bars are visible, in a vehicle the
      // vehicle bars. While we did not compute them, both were drawn together.
      if (variable == "BottomLeftHealthAlpha") return bottomLeft.healthAlpha;
      if (variable == "BottomLeftVehicleAlpha") return bottomLeft.vehicleAlpha;
      if (variable == "BottomLeftHealthFadedAlpha") return bottomLeft.healthFadedAlpha;
      if (variable == "BottomLeftVehicleFadedAlpha") return bottomLeft.vehicleFadedAlpha;
      // On the right there is one alpha, also from the graph. Its pair
      // `BottomRightFadedAlpha` (field +0x14) we do not compute: on the left the
      // formula is visible at the end of 0x78b600, while on the right the place that
      // computes it is **a source not found**.
      if (variable == "BottomRightAlpha") return bottomRightAlpha;
      const auto found = hudAlpha.find(std::string(variable));
      if (found == hudAlpha.end()) return std::nullopt;
      return found->second;
    };
    hudContext.variableText = [&](std::string_view variable) -> std::string_view {
      const auto found = hudStrings.find(std::string(variable));
      return found == hudStrings.end() ? std::string_view{} : std::string_view(found->second);
    };

    hudDynamicContext = hudContext;

    // We measure the HUD with the **real** window rather than the requested one: the
    // screen may have turned out smaller, and the window would slide with the request.
    hudScreen.width = args.width;
    hudScreen.height = args.height;
    SDL_GetWindowSize(device->window(), &hudScreen.width, &hudScreen.height);
    // The root is the Global group (Global -> GlobalHud -> IngameHud and on). Its
    // nodes are given in absolute 800x600, so they land correctly.
    //
    // The corner layers (BottomLeftStatic, BottomRightAnimate, TopLayer ...) are not
    // drawn yet: in the `.con` they are positioned nowhere. The anchor lies in the
    // `MemeFile 2.0` files — that is data, not code, and it need not be looked for in
    // BF2.exe (see docs/formats/hud-meme.md). While it is not taken apart, the health
    // bar would drive into the middle of the screen.
    // --hud-rects: the same format as in the original's frame dump.
    const auto reportRects = [&](const char* where,
                                 const std::vector<obf2::hud::DrawPiece>& pieces) {
      if (!args.hudRects) return;
      for (const obf2::hud::DrawPiece& piece : pieces) {
        if (piece.node == nullptr || piece.geometry.vertices.empty()) continue;
        // The bounds are computed from the geometry itself rather than from the node's
        // rectangle: that way the captions are comparable with the original's dump,
        // which also outlines the drawn string rather than the node's frame.
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& vertex : piece.geometry.vertices) {
          const float px = (vertex.position.x + 1.0f) * 0.5f * hudScreen.width;
          const float py = (1.0f - vertex.position.y) * 0.5f * hudScreen.height;
          x0 = std::min(x0, px);
          y0 = std::min(y0, py);
          x1 = std::max(x1, px);
          y1 = std::max(y1, py);
        }
        std::printf("RECT %-14s %-30s %7.1f %7.1f %7.1f %7.1f %-46s [%s]\n", where,
                    piece.node->name.c_str(), x0, y0, x1 - x0, y1 - y0,
                    piece.texture.c_str(), piece.node->showVariable.c_str());
      }
    };

    // The combat HUD is rebuildable too. In the game its variables are written not
    // once at the level's start but every frame — see docs/functions/hud-states.md,
    // the section on the HUD object: 0x78d0f0 takes the current player and either
    // turns on PlayerHealthShow (0x78d154) or clears the whole set (0x78d2d9).
    // So the tree has to be assembled anew rather than baked once and for all.
    buildIngamePieces = [&]() {
      const auto layers = obf2::hud::ingameLayers(ingameGraph, bottomLeftX, bottomRightX);
      auto pieces = obf2::hud::buildIngame(
          ingameHud, layers, hudFont.font, hudFont.atlasPath, hudScreen, hudContext,
          [&](const obf2::hud::IngameLayer& layer, const std::vector<obf2::hud::DrawPiece>& made) {
            reportRects(layer.group.c_str(), made);
            if (!ingameReported) {
              std::printf("  HUD: layer %-20s corner %.0f %.0f, pieces %zu\n", layer.group.c_str(),
                          layer.x, layer.y, made.size());
            }
          });
      reportRects("Global", pieces);
      return pieces;
    };

    // Every key-held screen is unlocked by exactly **one** variable — the one that
    // stands on its root in the game's data:
    //
    //   Scoreboard -> ScoreboardShow      RadioRose -> RadioInterfaceShow
    //   SpawnMenu  -> SpawnShow           MapMenu   -> MapMenuShow
    //
    // Until now we treated everything unfamiliar as on for these screens — and on the
    // scoreboard both tabs, both sets of captions and the server data block came out
    // together, overlapping. The right way is the opposite: the same rules as in
    // combat, plus this variable itself.
    struct KeyScreenSetup {
      const char* group;
      const char* action;
      const char* gate;
      // The second branch this screen adds to itself. The map lives in the main tree
      // (MapSplit under IngameHud) rather than inside the spawn screen — on the screen
      // it simply switches to the big presentation.
      const char* extraRoot = nullptr;
      obf2::hud::MapView mapView = obf2::hud::MapView::Mini;
      int state = -1;
      bool heldByKey = true;
    };
    for (const KeyScreenSetup& setup : {
             KeyScreenSetup{"Scoreboard", "c_GIShowScoreboard", "ScoreboardShow", nullptr,
                            obf2::hud::MapView::Mini, 9, true},
             KeyScreenSetup{"RadioRose", "c_GIRadioComm", "RadioInterfaceShow"},
             KeyScreenSetup{"MapMenu", "c_GIMapSize", "MapMenuShow"},
         }) {
      const char* const group = setup.group;
      const char* const action = setup.action;
      obf2::hud::Context keyContext = hudContext;
      const std::string gate = setup.gate;
      keyContext.isVisible = [&, gate](std::string_view variable) {
        if (variable == "1" || variable == gate) return true;
        const auto found = hudVariables.find(std::string(variable));
        return found != hudVariables.end() && found->second;
      };
      keyContext.variableValue = [&, gate](std::string_view variable) -> float {
        if (variable == gate) return 1.0f;
        const auto found = hudValues.find(std::string(variable));
        return found == hudValues.end() ? 0.0f : found->second;
      };
      if (setup.mapView != obf2::hud::MapView::Mini) ingameHud.setMapView(setup.mapView);
      auto built = obf2::hud::buildTree(ingameHud, group, hudFont.font, hudFont.atlasPath,
                                        hudScreen, keyContext);
      if (setup.extraRoot != nullptr) {
        for (auto& piece : obf2::hud::buildTree(ingameHud, setup.extraRoot, hudFont.font,
                                                hudFont.atlasPath, hudScreen, keyContext)) {
          built.push_back(std::move(piece));
        }
      }
      // We assemble the report BEFORE putting the presentation back: otherwise the map
      // node would already be measured as a thumbnail while the big one was baked in.
      if (!built.empty()) reportRects(group, built);
      // We put the thumbnail back: the main HUD is measured with it.
      if (setup.mapView != obf2::hud::MapView::Mini) {
        ingameHud.setMapView(obf2::hud::MapView::Mini);
        mapRectStale = true;
      }
      if (built.empty()) continue;
      KeyScreen screen;
      screen.group = group;
      screen.action = action;
      screen.state = setup.state;
      screen.heldByKey = setup.heldByKey;
      for (auto& piece : built) {
        scene.meshes.push_back(std::move(piece.geometry));
        const int index = static_cast<int>(scene.meshes.size()) - 1;
        screen.quads.push_back(index);
        hudTints.emplace(index, piece.tint);
      }
      std::printf("  HUD: screen %-12s on %s (%s), pieces %zu\n", group, action,
                  std::string(controls.key(action)).c_str(), screen.quads.size());
      keyScreens.push_back(std::move(screen));
    }

    // The spawn screen is built separately and we keep its meshes to hand: after
    // every command (choosing a kit, a team, a tab) it is rebuilt, while the other
    // screens stay baked once and for all.
    spawnContext = hudContext;
    spawnContext.isVisible = [&](std::string_view variable) {
      if (variable == "1" || variable == "SpawnShow") return true;
      const auto found = hudVariables.find(std::string(variable));
      return found != hudVariables.end() && found->second;
    };
    // The show effects are seen by the geometry builder itself: alpha multiplies the
    // alpha, move shifts the rectangle.
    spawnContext.showState = [&](const obf2::hud::Node& node) { return hudAnimator.state(node); };
    rebuildIngame = [&]() {
      for (OwnedPiece& piece : ingamePieces) renderer->release(piece.mesh);
      ingamePieces.clear();
      for (const char* root : {"Global", "BottomLeftAnimate", "BottomLeftStatic",
                               "BottomRightAnimate", "BottomRightStatic"}) {
        obf2::hud::updateAnimator(ingameHud, root, hudAnimator, hudContext);
      }
      for (auto& piece : buildIngamePieces()) {
        if (piece.live) {
          // A live node's marker: there is no mesh here, only a place in the queue.
          ingamePieces.push_back(OwnedPiece{{}, piece.tint, piece.node});
          continue;
        }
        if (auto uploaded = renderer->upload(piece.geometry, resolveTexture)) {
          ingamePieces.push_back(OwnedPiece{*uploaded, piece.tint, nullptr});
        }
      }
      if (ingameReported) {
        std::printf("  HUD: the combat one rebuilt, pieces %zu\n", ingamePieces.size());
      }
      ingameReported = true;
    };
    hudContext.showState = [&](const obf2::hud::Node& node) { return hudAnimator.state(node); };
    rebuildIngame();

    rebuildSpawn = [&]() {
      for (OwnedPiece& piece : ingamePieces) renderer->release(piece.mesh);
  for (OwnedPiece& piece : spawnPieces) renderer->release(piece.mesh);
      spawnPieces.clear();
      applySpawnState();
      for (const char* root : {"SpawnMenu", "MapSplit", "TopLayer"}) {
        obf2::hud::updateAnimator(ingameHud, root, hudAnimator, spawnContext);
      }
      ingameHud.setMapView(obf2::hud::MapView::Maxi);
      auto built = obf2::hud::buildTree(ingameHud, "SpawnMenu", hudFont.font, hudFont.atlasPath,
                                        hudScreen, spawnContext);
      auto mapPieces = obf2::hud::buildTree(ingameHud, "MapSplit", hudFont.font,
                                            hudFont.atlasPath, hudScreen, spawnContext);
      const std::size_t mapCount = mapPieces.size();
      for (auto& piece : mapPieces) built.push_back(std::move(piece));
      // TopLayer is a real region from the data (`createSplitNode TopLayer
      // TopLayerHud` in GeneralHudSettings.con), and it is where the DONE and SUICIDE
      // buttons live: MapButtons -> DoneButton 666 539 124 17.
      for (auto& piece : obf2::hud::buildTree(ingameHud, "TopLayer", hudFont.font,
                                              hudFont.atlasPath, hudScreen, spawnContext)) {
        built.push_back(std::move(piece));
      }
      // `--hud-rects` for the spawn screen, written out here rather than
      // through `reportRects`. That one is a lambda of the level-loading block,
      // and this rebuild is called from the frame loop — after the block has
      // exited, so a call into it reads a closure that is no longer there
      // (CLAUDE.md, the first of the rakes). What this loop touches instead —
      // `args`, `hudScreen`, and its own `built` — all outlive the frame.
      if (args.hudRects) {
        for (const obf2::hud::DrawPiece& piece : built) {
          if (piece.node == nullptr || piece.geometry.vertices.empty()) continue;
          float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
          for (const auto& vertex : piece.geometry.vertices) {
            const float px = (vertex.position.x + 1.0f) * 0.5f * hudScreen.width;
            const float py = (1.0f - vertex.position.y) * 0.5f * hudScreen.height;
            x0 = std::min(x0, px);
            y0 = std::min(y0, py);
            x1 = std::max(x1, px);
            y1 = std::max(y1, py);
          }
          std::printf("RECT %-14s %-30s %7.1f %7.1f %7.1f %7.1f %-46s [%s]\n", "SpawnMenu",
                      piece.node->name.c_str(), x0, y0, x1 - x0, y1 - y0, piece.texture.c_str(),
                      piece.node->showVariable.c_str());
        }
      }
      ingameHud.setMapView(obf2::hud::MapView::Mini);
      // The map's rectangle was just moved — let the combat frame put its own back,
      // the one the animation computed.
      mapRectStale = true;
      for (auto& piece : built) {
        if (auto uploaded = renderer->upload(piece.geometry, resolveTexture)) {
          spawnPieces.push_back(OwnedPiece{*uploaded, piece.tint});
        }
      }
      std::printf("  HUD: the spawn screen rebuilt, pieces %zu (map %zu), circles %zu\n",
                  spawnPieces.size(), mapCount, spawnContext.spawnMarkers.size());
    };
    rebuildSpawn();

    // The live nodes: everything that takes its value from a variable we can fill in.
    for (const auto& node : ingameHud.nodes()) {
      const bool ticketText = node.group == "TicketInfo" &&
                              (node.textVariable == "FriendlyTicketsString" ||
                               node.textVariable == "EnemyTicketsString");
      const bool cpBar = node.type == obf2::hud::NodeType::Bar &&
                         node.group == "CPInformationItems" && !node.valueVariable.empty();
      // The caption in the middle of the screen: while the round waits for players,
      // the text in it appears and disappears, so it must not be baked in advance.
      const bool centreMessage = node.textVariable == "DisconnectMessage";
      // The compass: the only node in the data with `setPictureNodeRotateVariable`.
      const bool rotating = !node.rotateVariable.empty();
      // The map: its window follows the player and the zoom, so it must not be baked
      // in advance either.
      const bool mapNodeItself = node.type == obf2::hud::NodeType::Map ||
                                 node.type == obf2::hud::NodeType::MiniMap;
      if (!ticketText && !cpBar && !centreMessage && !rotating && !mapNodeItself) continue;
      hudDynamic.push_back(DynamicNode{&node, {}, -1.0f, 0.0f, {}});
    }

    // The live nodes are not baked into the shared geometry: otherwise a second,
    // frozen one would remain under the turning compass.
    hudContext.skipNode = [&](const obf2::hud::Node& node) {
      for (const DynamicNode& dynamic : hudDynamic) {
        if (dynamic.node == &node) return true;
      }
      return false;
    };
    // The first bake was still without this rule — redo it, otherwise the compass
    // will stay on screen twice.
    rebuildIngame();

    // --hud-screen list: what exactly landed on screen. Without it one has to guess
    // which node slid.
    if (args.hudScreenName == "list") {
      for (const auto& piece : buildIngamePieces()) {
        if (piece.node == nullptr) continue;
        std::printf("    %-10s %-28s %-22s %6.0f %6.0f %5.0f %5.0f  %s\n",
                    std::string(obf2::hud::nodeTypeName(piece.node->type)).c_str(),
                    piece.node->name.c_str(), piece.node->group.c_str(), piece.node->x,
                    piece.node->y, piece.node->width, piece.node->height,
                    piece.texture.c_str());
      }
    }

    std::printf("  HUD: %zu nodes in the tree, pieces to draw %zu, live captions %zu\n",
                ingameHud.nodes().size(), ingamePieces.size(), hudDynamic.size());

    // The commands we cannot do yet. The game is a stream of commands, so the most
    // useful thing is to see exactly what arrived and was left without a handler.
    if (ingameHud.unknownCommands() > 0) {
      std::printf("  HUD: unimplemented %lld commands, unique %zu\n",
                  ingameHud.unknownCommands(), ingameHud.unknownByName().size());
      int shown = 0;
      for (const auto& [name, count] : ingameHud.unknownByName()) {
        if (shown++ >= 8) break;
        std::printf("    no handler: %-40s x%d\n", name.c_str(), count);
      }
      if (ingameHud.unknownByName().size() > 8) {
        std::printf("    ... the rest is command_audit\n");
      }
    }
  }

  std::vector<obf2::gfx::GpuMesh> gpuMeshes(scene.meshes.size());
  std::vector<bool> uploadedOk(scene.meshes.size(), false);

  const auto uploadStarted = std::chrono::steady_clock::now();

  // Every texture the scene names, unpacked and decoded before the upload
  // begins. The upload itself has to stay on this thread — SDL's GPU device is
  // not shared — but the work in front of it is inflate and a DXT header per
  // file, and that is what the other cores are for. Afterwards the loop below
  // finds all of them in the cache.
  {
    std::vector<std::string> wanted;
    std::unordered_set<std::string> seen;
    for (const obf2::mesh::RenderMesh& mesh : scene.meshes) {
      for (const obf2::mesh::DrawRange& range : mesh.ranges) {
        for (const std::string& map : range.maps) {
          // The `#` names are colours rather than files, and the cache is not
          // where they come from.
          if (map.empty() || map.front() == '#') continue;
          if (seen.insert(map).second) wanted.push_back(obf2::normalizeAssetPath(map));
        }
      }
    }
    obf2::parallelFor(wanted.size(), [&](std::size_t i) { cacheTexture(wanted[i]); });
  }

  long long triangles = 0;
  for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
    auto uploaded = renderer->upload(scene.meshes[i], resolveTexture, &error);
    if (!uploaded) continue;
    gpuMeshes[i] = *uploaded;
    uploadedOk[i] = true;
    triangles += static_cast<long long>(scene.meshes[i].indices.size() / 3);
  }

  // The atlas pages, uploaded once. Only the pages some object actually points
  // at are loaded; a level has up to 21 and a small map uses few of them.
  std::vector<SDL_GPUTexture*> lightmapPages(
      static_cast<std::size_t>(std::max(objectLightmaps.atlasCount(), 0)), nullptr);
  {
    std::vector<bool> wanted(lightmapPages.size(), false);
    for (const Scene::Instance& instance : scene.instances) {
      if (instance.lightmapAtlas >= 0 &&
          static_cast<std::size_t>(instance.lightmapAtlas) < wanted.size()) {
        wanted[static_cast<std::size_t>(instance.lightmapAtlas)] = true;
      }
    }
    int loaded = 0;
    for (std::size_t i = 0; i < lightmapPages.size(); ++i) {
      if (!wanted[i]) continue;
      if (auto decoded = resolveTexture(objectLightmaps.atlasPath(static_cast<int>(i)))) {
        lightmapPages[i] = renderer->uploadSharedTexture(*decoded);
        if (lightmapPages[i] != nullptr) ++loaded;
      }
    }
    if (!lightmapPages.empty()) {
      std::printf("  light map atlas pages loaded: %d of %zu\n", loaded, lightmapPages.size());
    }
  }

  // The ground's own structure: one texture for the whole level, tiled over the
  // terrain from three directions. Where it shows is a map per patch, and that
  // one travels with the patch's geometry.
  if (level) {
    const std::string lowDetail = obf2::level::lowDetailTexturePath(*level, files);
    SDL_GPUTexture* uploaded = nullptr;
    if (!lowDetail.empty()) {
      if (auto decoded = resolveTexture(lowDetail)) {
        uploaded = renderer->uploadSharedTexture(*decoded);
      }
    }
    renderer->setTerrainDetail(uploaded, level->terrain.farSideTiling,
                               level->terrain.farTopTilingHi, level->terrain.farYOffset,
                               level->terrain.lowDetailmapSize);
    std::printf("  terrain detail: %s, tiling %.0f/%.0f side, %.0f top\n",
                uploaded != nullptr ? lowDetail.c_str() : "none",
                static_cast<double>(level->terrain.farSideTiling[0]),
                static_cast<double>(level->terrain.farSideTiling[1]),
                static_cast<double>(level->terrain.farTopTilingHi));
    // And the near detail: the six materials out of the compiled terrain, and
    // the chart maps that say which of them owns which texel of a patch.
    obf2::gfx::MeshRenderer::TerrainMaterial materials[
        obf2::gfx::MeshRenderer::kTerrainMaterials];
    const std::size_t count =
        std::min(level->terrain.materials.size(),
                 static_cast<std::size_t>(obf2::gfx::MeshRenderer::kTerrainMaterials));
    for (std::size_t i = 0; i < count; ++i) {
      const obf2::level::TerrainMaterial& material = level->terrain.materials[i];
      // The file names the texture without an extension, and the archives hold
      // it as `.dds` like every other texture in the game.
      if (auto decoded = resolveTexture(material.texture + ".dds")) {
        materials[i].texture = renderer->uploadSharedTexture(*decoded);
      }
      materials[i].sideTiling[0] = material.sideTilingX;
      materials[i].sideTiling[1] = material.sideTilingY;
      materials[i].topTiling = material.topTiling;
      materials[i].yOffset = material.yOffset;
      materials[i].triPlanar = material.triPlanar;
      std::printf("  terrain material %zu: %-44s top %g, side %g/%g%s%s\n", i,
                  material.texture.c_str(), static_cast<double>(material.topTiling),
                  static_cast<double>(material.sideTilingX),
                  static_cast<double>(material.sideTilingY),
                  material.triPlanar ? ", tri-planar" : "",
                  materials[i].texture != nullptr ? "" : ", not loaded");
    }
    // The chart maps' size for the half-texel correction, taken from the first
    // patch that has one — the engine takes it the same way (`RendDX9.dll`,
    // 0x100d9c30) rather than from `terrain.detailmapSize`, which on Karkand
    // says 512 where the files are 256.
    int chartSize = 0;
    if (!firstChartMap.empty()) {
      if (auto decoded = resolveTexture(firstChartMap)) {
        chartSize = static_cast<int>(decoded->width);
      }
    }
    renderer->setTerrainMaterials(materials, chartSize);
  }

  obf2::gfx::GpuMesh skyMesh;
  bool skyReady = false;
  if (skyDome) {
    if (auto uploaded = renderer->upload(*skyDome, resolveTexture, &error)) {
      skyMesh = *uploaded;
      skyReady = true;
    }
  }

  // The placeholder for other players' soldiers. The size is not by eye: it is the
  // soldier's collision shape from the game's data (`coll-soldier-radius` 0.25 and
  // `coll-soldier-stand-height` 1.7), that is 0.5 x 1.7 x 0.5.
  // Three placeholders: another player's soldier, our own and everything else. The
  // colours here are **ours**, not the game's: they are a marker for as long as we
  // cannot take the real geometry. The size, though, is not ours — it is the
  // (`coll-soldier-radius` 0.25, `coll-soldier-stand-height` 1.7).
  obf2::gfx::GpuMesh boxes[3];
  bool boxesReady = false;
  if (remote != nullptr) {
    const obf2::server::PhysicsConstants& shape = remote->physics;
    const obf2::mesh::Vec3 size{shape.radius * 2.0f, shape.standHeight, shape.radius * 2.0f};
    const char* colors[3] = {"#909090", "#3060c0", "#c03030"};  // nobody, ours, theirs
    boxesReady = true;
    for (int i = 0; i < 3; ++i) {
      auto uploaded = renderer->upload(obf2::mesh::buildBox(size, colors[i]), resolveTexture);
      if (!uploaded) { boxesReady = false; break; }
      boxes[i] = *uploaded;
    }
  }

  std::vector<obf2::gfx::MeshRenderer::DrawItem> items;
  items.reserve(scene.instances.size());
  long long drawnTriangles = 0;
  for (const Scene::Instance& instance : scene.instances) {
    const int meshIndex = instance.mesh;
    if (meshIndex < 0 || !uploadedOk[static_cast<std::size_t>(meshIndex)]) continue;
    obf2::gfx::MeshRenderer::DrawItem item{&gpuMeshes[static_cast<std::size_t>(meshIndex)],
                                           instance.transform};
    item.road = instance.road;
    item.roadBlendFactor = instance.roadBlendFactor;
    if (instance.lightmapAtlas >= 0 &&
        static_cast<std::size_t>(instance.lightmapAtlas) < lightmapPages.size()) {
      item.lightmap = lightmapPages[static_cast<std::size_t>(instance.lightmapAtlas)];
      if (item.lightmap != nullptr) {
        std::memcpy(item.lightmapOffset, instance.lightmapOffset, sizeof(item.lightmapOffset));
      }
    }
    items.push_back(item);
    drawnTriangles += static_cast<long long>(scene.meshes[static_cast<std::size_t>(meshIndex)]
                                                 .indices.size() / 3);
  }

  // The level is in the GPU — now the server can be told we have loaded, and from
  // then on we answer pings every frame.
  if (remote != nullptr) {
    remote->join.setClientLoaded();
    // The movement prediction needs the same terrain and the same constants as the
    // server: otherwise it would diverge from it at every step.
    remote->terrain = level ? &*level : nullptr;
    // The terrain is needed by the parsing too: by it one can see whether the
    // soldier's position keeps to the ground or drifted (the desync measure).
    if (level) {
      const obf2::level::Level* terrain = &*level;
      remote->world.setGroundProbe(
          [terrain](const obf2::Vec3f& at) { return terrain->groundHeightAt(at); });
    }
    remote->physics = loadPhysics(files);
    remote->maxSpeed = remote->physics.runSpeed;
    if (level) {
      remoteCollision = buildCollisionWorld(files, registry, level->objects);
      remote->collision = remoteCollision.get();
    }
    std::printf("  connection: the level is loaded, the conversation continues\n");
  }

  std::printf("in the GPU: %zu unique meshes (%lld triangles), %zu instances "
              "(%lld triangles per frame)\n  textures %d, not found %d (%.2f s)\n",
              gpuMeshes.size(), triangles, items.size(), drawnTriangles, texturesLoaded,
              texturesMissing, secondsSince(uploadStarted));

  // --- the loop ---------------------------------------------------------

  if (args.focus) scene.center = *args.focus;
  const float distance =
      args.distance > 0.0f ? args.distance : scene.radius * (level ? 1.6f : 2.6f);
  const float eyeHeight = distance * (level && !args.focus ? 0.3f : 0.35f);

  float pitch = -10.0f;
  // In the menu the mouse is not captured — otherwise the cursor could not reach a
  // button. Mouse capture is turned on not here but every frame by the HUD's state:
  // in combat yes, on the spawn screen no (see wantRelativeMouse below).
  bool relativeMouse = false;
  // A deterministic menu screenshot: we put the cursor where we were asked to.
  if (args.mouseX >= 0.0f) {
    SDL_WarpMouseInWindow(device->window(), args.mouseX, args.mouseY);
  }

  int frame = 0;
  bool execDone = false;  // --exec is run once
  // How much time passed since the previous frame. The movement prediction has to
  // count exactly that: with a fixed 1/60 the soldier would walk slower than the
  // camera on a fast machine and faster on a slow one, and the movement would jerk
  // exactly as much as the frames are uneven.
  auto lastFrameStart = std::chrono::steady_clock::now();
  float frameStep = 1.0f / 60.0f;
  while (device->pumpEvents()) {
    {
      const auto nowFrame = std::chrono::steady_clock::now();
      frameStep = std::chrono::duration<float>(nowFrame - lastFrameStart).count();
      lastFrameStart = nowFrame;
      // A long frame (loading, the window being dragged) must not turn into a jump
      // across half the map.
      frameStep = std::min(frameStep, 0.1f);
    }
    // Read the input **once** per frame and share it.
    //
    // `InputState::clicked` is an edge — pressed now, released before —
    // and `readInput()` consumes it: the second call in the same frame
    // always sees "not pressed". Calling it per consumer silently broke
    // every click that was not handled by the first consumer: menu
    // buttons under the Flash movie, and DONE on the spawn screen while
    // the local server was running.
    const obf2::gfx::Device::InputState frameInput = device->readInput();
    // The server sends pings and waits for answers: staying silent frame after frame
    // gets us disconnected. So the connection runs together with the picture, and we
    // keep the waiting short — otherwise it would be a freeze.
    if (remote != nullptr) {
      // We take **the whole** queue rather than one packet per frame. Otherwise it
      // grows: the server sends ghosts more often than we draw frames, and every
      // "our position" arrives with a delay that accumulates. That is exactly what
      // looked like a giant lag and jerking: we put the soldier where he had been
      // several seconds earlier.
      remote->pump(1);
      while (remote->pump(0)) {
      }
      // The input goes separately from the rest of the conversation and at its own rate.
      remote->sendActions();
    }

    auto acquired = device->beginFrame();
    if (!acquired) continue;

    // Our own client-server pair runs every frame regardless of whether the player
    // has spawned: otherwise the server would not manage to execute the spawn request
    // itself.
    if (hostedServer && hostedClient) {
      const auto& raw = frameInput;
      // The same chain as on a real server: pixels -> axis -> angle, with the look
      // factor from the data (`phy-soldier-look-factor-*`, 5.0 by default). The angle
      // grows clockwise (a left-handed system), so moving the mouse right has to
      // **increase** it.
      const obf2::server::PhysicsConstants& look = hostedServer->settings().physics;
      yaw += raw.mouseDeltaX * args.mouseScale * look.lookFactorX;
      pitch -= raw.mouseDeltaY * args.mouseScale * look.lookFactorY;
      pitch = std::max(-89.0f, std::min(89.0f, pitch));

      obf2::net::PlayerInput input;
      input.moveForward = raw.moveForward;
      input.moveRight = raw.moveRight;
      input.sprint = raw.sprint;
      input.jump = raw.jump;
      input.fire = raw.fire;
      input.yaw = yaw;
      input.pitch = pitch;
      hostedClient->setInput(input);

      const float step = 1.0f / 60.0f;
      hostedClient->tick(step);
      hostedServer->tick(step);

      // The soldier appears not on connecting but after DONE, so his number has to be
      // asked for again every frame.
      const std::uint32_t hadSoldier = localSoldierId;
      for (const auto& player : hostedServer->players()) localSoldierId = player.soldierId;
      if (hadSoldier == 0 && localSoldierId != 0) {
        for (const auto& object : hostedServer->objects()) {
          if (object.id != localSoldierId) continue;
          std::printf("  spawn: soldier %u at %.1f %.1f %.1f\n", object.id, object.position.x,
                      object.position.y, object.position.z);
        }
      }
    }

    // --- the camera ---
    obf2::Vec3f eye;
    obf2::Vec3f lookTarget = scene.center;

    // Until the player has spawned there is nothing to look from in first person:
    // there is no soldier yet. The spawn screen's camera from Init.con works then.
    const auto remoteSoldier =
        remote != nullptr ? remote->soldierPosition() : std::optional<obf2::Vec3f>{};
    if (args.camera) {
      // `--camera x/y/z --angles yaw pitch`: stand exactly here and look exactly
      // there, whatever mode the rest of the run is in. It outranks the soldier
      // and the spawn screen on purpose — the point of it is to put our frame
      // and the original's in the same spot, and the original is put there with
      // the same numbers.
      constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
      const float yawRadians = args.cameraYaw * kToRadians;
      const float pitchRadians = args.cameraPitch * kToRadians;
      eye = *args.camera;
      lookTarget = eye + obf2::Vec3f{std::sin(yawRadians) * std::cos(pitchRadians),
                                     std::sin(pitchRadians),
                                     std::cos(yawRadians) * std::cos(pitchRadians)};
    } else if ((hostedServer && hostedClient && localSoldierId != 0) || remoteSoldier) {
      // The soldier's position comes from whoever owns him: our server in our own
      // game, that server on a real one.
      eye = remoteSoldier ? *remoteSoldier : hostedClient->interpolatedPosition(localSoldierId);
      eye.y += 1.7f;  // the soldier's height: the camera at eye level

      // On a real server the input goes to the same place as in our own game —
      // only in another form: as the player action stream.
      if (remoteSoldier && remote != nullptr) {
        const auto& raw = frameInput;

        // The engine's chain: **pixels -> axis -> angle**, and all three links have
        // to be the same for the camera and for what we send the server.
        // That is exactly what was missing: the camera turned on `pixels * 0.15`,
        // while we sent the server raw pixels, which it multiplied by 5.0. The
        // divergence came to thirty times — hence "a 360 sweep does not give 360",
        // and the soldier ending up somewhere other than where you look.
        //
        //   axis  = pixels * sensitivity
        //   angle += axis * phy-soldier-look-factor-*   (0x5a99a0)
        //   wire   = axis * 100                         (0x5bc5f0)
        const obf2::server::PhysicsConstants& look = remote->physics;
        const float axisX = raw.mouseDeltaX * args.mouseScale;
        const float axisY = raw.mouseDeltaY * args.mouseScale;
        yaw += axisX * look.lookFactorX;
        pitch -= axisY * look.lookFactorY;
        pitch = std::max(-89.0f, std::min(89.0f, pitch));

        obf2::net::bf2::PlayerAction& out = remote->action;
        out = obf2::net::bf2::PlayerAction{};
        out.axes[obf2::net::bf2::kAxisMouseX] =
            static_cast<std::int16_t>(axisX * obf2::net::bf2::kAxisWireScale);
        out.axes[obf2::net::bf2::kAxisMouseY] =
            static_cast<std::int16_t>(axisY * obf2::net::bf2::kAxisWireScale);
        // Full forward movement in the dump is 99, so our ±1 is multiplied by it.
        out.axes[obf2::net::bf2::kAxisThrottle] =
            static_cast<std::int16_t>(raw.moveForward * obf2::net::bf2::kAxisFull);
        // Strafing is the yaw axis: in Controls.con D/A hang on exactly `c_PIYaw`,
        // and the engine has no separate strafe axis.
        out.axes[obf2::net::bf2::kAxisYaw] =
            static_cast<std::int16_t>(raw.moveRight * obf2::net::bf2::kAxisFull);
        if (raw.sprint) out.buttons |= obf2::net::bf2::kButtonSprint;
        if (raw.jump) out.buttons |= obf2::net::bf2::kButtonAction;
        if (raw.fire) out.buttons |= obf2::net::bf2::kButtonFire;

        // And we compute our own movement at once: waiting for the server's
        // correction would mean jerks a few tenths of a second apart.
        remote->predict(frameStep, yaw);
        eye = *remote->soldierPosition();
        eye.y += 1.7f;
      }

      constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
      const float yawRadians = yaw * kToRadians;
      const float pitchRadians = pitch * kToRadians;
      // A zero angle looks along +Z — the same as the server computes.
      lookTarget = eye + obf2::Vec3f{std::sin(yawRadians) * std::cos(pitchRadians),
                                     std::sin(pitchRadians),
                                     std::cos(yawRadians) * std::cos(pitchRadians)};
    } else if (args.topDown) {
      eye = obf2::Vec3f{scene.center.x, scene.center.y + distance, scene.center.z};
    } else if (level && level->hasBeforeSpawnCamera) {
      // Until the player has spawned the camera stands where the level said:
      //
      //   gameLogic.setBeforeSpawnCamera -50/185/-285 -16/-3/0
      //
      // (Levels/<level>/Init.con). The first triple is the position, the second the
      // rotation in degrees. Until now we simply orbited the camera around the map's
      // centre, and the view had nothing in common with the game.
      constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
      eye = level->beforeSpawnCameraPos;
      const float yawRadians = level->beforeSpawnCameraRot.x * kToRadians;
      const float pitchRadians = level->beforeSpawnCameraRot.y * kToRadians;
      lookTarget = eye + obf2::Vec3f{std::sin(yawRadians) * std::cos(pitchRadians),
                                     std::sin(pitchRadians),
                                     std::cos(yawRadians) * std::cos(pitchRadians)};
    } else {
      const float angle = static_cast<float>(frame) / 60.0f * 0.6f;
      eye = obf2::Vec3f{scene.center.x + std::sin(angle) * distance, scene.center.y + eyeHeight,
                        scene.center.z + std::cos(angle) * distance};
    }

    const float aspect =
        acquired->height == 0
            ? 1.0f
            : static_cast<float>(acquired->width) / static_cast<float>(acquired->height);
    // The near plane. For the game it has to be small: otherwise everything closer
    // than it disappears — and you can see straight through a wall you walked up to.
    // For the model viewer the camera is far away anyway, so there we keep a
    // proportional one — it gives better depth precision.
    const bool firstPerson = hostedServer != nullptr || remoteSoldier.has_value();
    const float nearPlane = firstPerson ? 0.1f : scene.radius * 0.002f + 0.05f;

    // How far we draw is the level's own view distance, scaled by the player's
    // setting — `GameLogic.MaximumLevelViewDistance` in the level's Init.con
    // and `VideoSettings.setViewDistanceScale` in the profile.
    //
    // It is not the fog's end, which is what we used to take. The two are
    // close on some levels and not on others: Karkand sees 140 m and fogs out
    // at 135, but Gulf of Oman sees 400 while its fog runs to 450 — there the
    // fog's end would have drawn 50 m of world the game never shows.
    const float levelView = level ? level->maximumViewDistance : 0.0f;
    const float viewDistance = levelView * engine.settings().video.viewDistanceScale;
    const float fogFar = level ? level->terrain.fogEnd : 0.0f;
    const float firstPersonFar = viewDistance > 1.0f ? viewDistance : fogFar;
    const float farPlane =
        firstPerson && firstPersonFar > 1.0f ? firstPersonFar : scene.radius * 40.0f;

    const obf2::Mat4 projection = obf2::perspective(1.05f, aspect, nearPlane, farPlane);
    // From above "screen up" has to be Z rather than Y (which would coincide with the
    // view). In a left-handed system right is cross(up, forward), so with up = +Z
    // right ends up as +X — exactly as on the level's own minimap.
    const obf2::Vec3f up =
        args.topDown && !bootMode ? obf2::Vec3f{0.0f, 0.0f, 1.0f} : obf2::Vec3f{0.0f, 1.0f, 0.0f};
    const obf2::Mat4 view = obf2::lookAt(eye, lookTarget, up);

    if (bootMode) {
      engine.update(1.0f / 60.0f);
      if (device->consumeSkip()) engine.skipMovie();

      // --- interaction with the menu ---
      obf2::gfx::Device::InputState menuInput = frameInput;
      // --mouse sets the cursor directly: in a screenshot the window may have no
      // focus, and SDL then does not give the real position.
      if (args.mouseX >= 0.0f) {
        menuInput.mouseX = args.mouseX;
        menuInput.mouseY = args.mouseY;
        if (args.click && frame == 1) menuInput.clicked = true;
      }
      // Screens the engine still draws itself: the intro fill and the
      // loading screen. The menu proper is the Flash movie below.
      const bool loading = engine.state() == obf2::engine::State::Loading;
      const int quad = engine.state() == obf2::engine::State::Intro ? introQuad
                       : loading                                    ? loadingQuad
                                                                    : -1;

      std::vector<obf2::gfx::MeshRenderer::DrawItem> screen;
      auto push = [&](int index) {
        if (index < 0 || !uploadedOk[static_cast<std::size_t>(index)]) return;
        screen.push_back(obf2::gfx::MeshRenderer::DrawItem{
            &gpuMeshes[static_cast<std::size_t>(index)], obf2::Mat4::identity()});
      };
      push(quad);
      if (loading) {
        for (const int index : loadingTextQuads) push(index);
      }
#if OBF2_HAVE_FLASH
      // The menu's movie is drawn instead of our screen: one rectangle over the whole
      // frame with the texture `#flash`. Every frame of the movie is its own, so the
      // mesh is reloaded each time; for the menu that is cheap.
      static obf2::gfx::GpuMesh flashMesh;
      static bool flashUploaded = false;
      std::vector<obf2::gfx::MeshRenderer::DrawItem> flashItems;
      if (flashMovie.isOpen()) {
        // The background is loaded once: it does not change.
        if (!flashBackgroundReady && !flashBackground.empty()) {
          if (auto uploaded = renderer->upload(buildScreenQuad(flashBackground), resolveTexture)) {
            flashBackgroundMesh = std::move(*uploaded);
            flashBackgroundReady = true;
          }
        }
        if (flashBackgroundReady) {
          flashItems.push_back(obf2::gfx::MeshRenderer::DrawItem{&flashBackgroundMesh,
                                                                 obf2::Mat4::identity(),
                                                                 {1, 1, 1, 1}});
        }
        // Input. The menu is buttons, so without a mouse it stays a picture.
        // The coordinates are converted from the window into the movie's stage: the
        // window may be a different size.
        // We take the same input as our own menu rather than reading it twice:
        // `clicked` is a **transition** from released to pressed, and `readInput()`
        // eats it. A second call within a frame always saw "not pressed", so in a
        // window none of the movie's buttons worked — even though the synthetic
        // `--click-at` did work, because it set the flag itself.
        auto flashInput = menuInput;
        // `--mouse` and `--click` are the same path as for the other screens: without
        // them a click cannot be checked without a person at the keyboard.
        if (args.mouseX >= 0.0f) {
          flashInput.mouseX = args.mouseX;
          flashInput.mouseY = args.mouseY;
        }
        if (args.click && frame == 20) flashInput.clicked = true;
        // `--click-at <frame>:<x>:<y>` — a schedule of clicks. The menu leads the
        // player through several steps, and one click will not get through it.
        for (const auto& scheduled : args.clicks) {
          if (frame != scheduled.frame) continue;
          flashInput.mouseX = scheduled.x;
          flashInput.mouseY = scheduled.y;
          flashInput.clicked = true;
        }
        // Convert from window to stage using the **actual** window size,
        // not the one asked for: the two differ whenever the window
        // manager gives us something else (`window 1221x916 (asked for
        // 1600x1200)`), and the cursor then lands a third of a screen off.
        // SDL reports mouse position in window points, and the movie is
        // stretched across the whole window, so this is a plain ratio.
        int windowWidth = args.width, windowHeight = args.height;
        SDL_GetWindowSize(device->window(), &windowWidth, &windowHeight);
        if (windowWidth <= 0) windowWidth = args.width;
        if (windowHeight <= 0) windowHeight = args.height;
        double stageX = 0.0, stageY = 0.0;
        flashMovie.toStage(windowWidth, windowHeight, flashInput.mouseX, flashInput.mouseY, &stageX,
                           &stageY);
        flashMovie.mouseMove(stageX, stageY);
        // A click is sent as two events, as it should be: down first, and the release
        // on the next frame. Flash does not manage to separate them when both come at
        // once, and the button does not fire.
        // We release **in the same place** we pressed: by the next frame the cursor is
        // somewhere else, and Flash would count that as "released outside the button"
        // — the button would not fire.
        static bool flashPressed = false;
        static double flashPressX = 0.0, flashPressY = 0.0;
        if (flashPressed) {
          flashMovie.mouseMove(flashPressX, flashPressY);
          flashMovie.mouseButton(flashPressX, flashPressY, false);
          flashPressed = false;
        } else if (flashInput.clicked) {
          flashMovie.mouseButton(stageX, stageY, true);
          flashPressX = stageX;
          flashPressY = stageY;
          flashPressed = true;
        }
        // Orders the menu gives us. The movie cannot start a level by
        // itself — in the original the engine hosts the player, so the
        // calls land in the engine directly. Here they arrive as lines
        // and go through the console, the same path our own menu uses.
        for (std::string order = flashMovie.takeCommand(); !order.empty();
             order = flashMovie.takeCommand()) {
          std::printf("Flash menu: %s\n", order.c_str());
          if (order == "quit") {
            menuQuit = true;
            continue;
          }
          if (order.rfind("level ", 0) != 0) continue;
          const std::size_t from = 6;
          const std::size_t to = order.find(' ', from);
          const std::string path = order.substr(from, to == std::string::npos ? to : to - from);
          // The menu spells level paths in lower case (`dalian_plant`),
          // the directories are not (`Dalian_plant`).
          for (const auto& level : engine.levels()) {
            if (level.directory.size() != path.size()) continue;
            const bool same = std::equal(
                level.directory.begin(), level.directory.end(), path.begin(),
                [](char a, char b) { return std::tolower(a) == std::tolower(b); });
            if (same) {
              requestedLevel = level.directory;
              break;
            }
          }
          if (requestedLevel.empty()) {
            std::printf("  the level %s is not among those found\n", path.c_str());
          }
        }
        flashStep();
        if (flashUploaded) renderer->release(flashMesh);
        flashUploaded = false;
        if (auto uploaded = renderer->upload(buildScreenQuad("#flash"), resolveTexture)) {
          flashMesh = std::move(*uploaded);
          flashUploaded = true;
          flashItems.push_back(
              obf2::gfx::MeshRenderer::DrawItem{&flashMesh, obf2::Mat4::identity(), {1, 1, 1, 1}});
        }
      }
      const std::vector<obf2::gfx::MeshRenderer::DrawItem>& overlayItems =
          flashItems.empty() ? screen : flashItems;
#else
      const std::vector<obf2::gfx::MeshRenderer::DrawItem>& overlayItems = screen;
#endif
      renderer->renderOverlay(*acquired, overlayItems, obf2::gfx::Color{0.0f, 0.0f, 0.0f, 1.0f});
    } else {
      // Foreign objects are added on top of the ready list: the level's scene is
      // assembled once, while these appear and disappear during the game.
      const std::vector<obf2::gfx::MeshRenderer::DrawItem>* toDraw = &items;
      std::vector<obf2::gfx::MeshRenderer::DrawItem> withOthers;

      // The dome rides with the camera and turns by the level's own
      // `Skydome.domeRotation`, so it is placed every frame.
      if (skyReady) {
        withOthers = items;
        obf2::gfx::MeshRenderer::DrawItem dome{
            &skyMesh, obf2::translation(eye) *
                          obf2::rotationYawPitchRoll(level->sky.domeRotation, 0.0f, 0.0f)};
        dome.sky = true;
        withOthers.push_back(dome);
        toDraw = &withOthers;
      }

      if (remote != nullptr && boxesReady && !remote->world.objects().empty()) {
        if (withOthers.empty()) withOthers = items;
        for (const auto& [id, object] : remote->world.objects()) {
          if (id == remote->ourSoldier && !args.showOwnBox) continue;  // we do not draw ourselves from inside
          // Whose soldier this is the server knows: the team came from
          // `CreatePlayerEvent` and the object from `EnterVehicleEvent`. Zero means "not a player".
          const int which =
              object.team == 0 ? 0 : (object.team == remote->world.ownTeam() ? 1 : 2);
          // The placeholder is turned by the angle we read: the yaw travels in the
          // same state, in twelve bits (`BF2.exe`, 0x62d4e0).
          obf2::Mat4 place = obf2::translation(object.position);
          if (object.yaw) {
            place = place * obf2::rotationY(*object.yaw * 3.14159265f / 180.0f);
          }
          withOthers.push_back(obf2::gfx::MeshRenderer::DrawItem{&boxes[which], place});
        }
        toDraw = &withOthers;
      }
      // The specular is the one thing that depends on where the eye is.
      renderer->setCameraPosition(eye);
      renderer->renderScene(*acquired, *toDraw, projection * view,
                            obf2::gfx::Color{0.42f, 0.55f, 0.68f, 1.0f});

      // The HUD goes as a second pass over the ready frame — without clearing the
      // target. `--no-hud` skips it: when a frame is being compared against the
      // original's, the interface is in the way of everything being compared.
      if (!args.noHud && (!hudQuads.empty() || !hudDynamic.empty())) {
        // Live values: the tickets come straight from the server, because in a
        // single-player game it is right here. To a client they will arrive in a
        // separate packet once the round's state exists on the network.
        if (hostedServer != nullptr) {
          const int own = 1, enemy = 2;
          hudStrings["FriendlyTicketsString"] = std::to_string(hostedServer->tickets(own));
          hudStrings["EnemyTicketsString"] = std::to_string(hostedServer->tickets(enemy));

          // The flag strips under the minimap: how many points each team holds.
          const auto& points = hostedServer->controlPoints();
          int ours = 0, theirs = 0;
          for (const auto& point : points) {
            if (point.team == own) ++ours;
            else if (point.team == enemy) ++theirs;
          }
          const float total = points.empty() ? 1.0f : static_cast<float>(points.size());
          hudValues["FriendlyCPs"] = static_cast<float>(ours) / total;
          hudValues["EnemyCPs"] = static_cast<float>(theirs) / total;

          // The caption in the middle of the screen while the round waits for players.
          // Its node is `GameInfo DisconnectMessage 0 200 800 40` with the variables
          // DisconnectMessage / DisconnectMessageActive; the text itself the game
          // assembles in code (BF2.exe, 0x466f75): it takes the key
          // HUD_STARTOFROUND_NRPLAYERSNEEDED and substitutes the number for the
          // marker #NROFPLAYERS#.
          const int missing = hostedServer->settings().playersNeededToStart -
                              static_cast<int>(hostedServer->playerCount());
          if (missing > 0) {
            std::string text(engine.lexicon().text("HUD_STARTOFROUND_NRPLAYERSNEEDED"));
            const std::string mark = "#NROFPLAYERS#";
            if (const std::size_t at = text.find(mark); at != std::string::npos) {
              text.replace(at, mark.size(), std::to_string(missing));
            }
            hudStrings["DisconnectMessage"] = text;
            hudVariables["DisconnectMessageActive"] = true;
          } else {
            hudVariables["DisconnectMessageActive"] = false;
          }
        }

        std::vector<obf2::gfx::MeshRenderer::DrawItem> hudItems;
        hudItems.reserve(hudQuads.size() + hudDynamic.size());

        // The scoreboard, the radio and the spawn screen go over everything while a
        // key is held. The order is the same as in the game: the combat HUD first.
        std::vector<int> extra;
        // The HUD's state. In the game this is not a set of keys but a 32-entry
        // machine (docs/functions/hud-states.md): state 1 is the spawn screen, and it
        // stands by itself until the player has spawned; state 9 is the scoreboard,
        // and that one really is on a key. So far we tell exactly those two apart.
        // DONE closes the spawn screen. In the game the button does not "hide the
        // menu" but asks the server to spawn, and the state switches on the fact of
        // the player spawning; while the server cannot do that, we close it ourselves
        // — otherwise the rest of the HUD cannot be looked at. Debt.
        // "There is a player" is not "the client is connected" but "the server gave
        // him a soldier". That is what governs the combat HUD in the engine
        // (0x78d0f0 takes the current player, and without one it clears the set).
        const bool spawned =
            hostedServer != nullptr ? localSoldierId != 0 : spawnScreen.requested();
        // The map key toggles state 0 <-> 2. In the state table state 2
        // (BF2.exe 0x787008) turns on `MapShow` alone: the rest of the HUD
        // disappears on the big map.
        {
          const std::string_view mapKey = controls.key("c_GIMapSize");
          const bool down = !mapKey.empty() && device->isKeyDown(mapKey);
          if (down && !mapKeyWasDown) bigMap = !bigMap;
          mapKeyWasDown = down;
        }
        const int hudState = spawned ? (bigMap ? 2 : 0) : 1;
        const bool spawnVisible = hudState == 1 || args.hudScreenName == "SpawnMenu";

        // The mouse: in combat the window captures it (otherwise the cursor runs into
        // the screen's edge and looking simply stops — that is exactly what looked
        // like "the controls do not work"), while on the spawn screen it is free,
        // because there it presses buttons. Until now capture was turned on only in
        // our own game, and on a real server looking was always broken.
        const bool wantRelativeMouse = spawned && !spawnVisible;
        if (wantRelativeMouse != relativeMouse) {
          relativeMouse = wantRelativeMouse;
          device->setRelativeMouse(relativeMouse);
        }
        // The HUD's state and the variables derived from it — every frame, as in the
        // game (0x786260 switches the state, 0x466930 and 0x78d0f0 compute the derived ones).
        // A full-screen map is precisely the spawn screen: in state 1 the handler
        // itself turns on MapBorderAlternateShow, and at 0x4669ae that equals the
        // negation of MapMinSize.
        // The team is assigned by the server rather than by our choice: in the
        // captured traffic the original client does not even send `NESelectTeam` — it
        // accepts the one the server gave in CreatePlayerEvent. So as soon as we learn
        // it, the spawn screen has to show the circles on that team's flags.
        // The team is assigned by the server rather than by our choice: in the
        // captured traffic the original client does not even send `NESelectTeam` — it
        // accepts the one the server gave in `CreatePlayerEvent`. Resetting the chosen
        // point here is no detail: the circles belong to our own team's flags, and
        // without the reset we would ask to spawn at another team's flag, which the
        // server does not do (`ServerGameLogic::uPlayingSpawning` takes the player's
        // group, and that is their own team's).
        if (remote != nullptr && remote->world.ownTeam() > 0 &&
            selectedTeam != remote->world.ownTeam()) {
          spawnScreen.setTeamFromServer(remote->world.ownTeam());
          selectedSpawn = spawnScreen.choice().marker;
          spawnDirty = true;
          std::printf("  spawn screen: the server gave team %d\n", selectedTeam);
        }
        if (applyHudState) applyHudState(hudState);
        // `MapFullSize` is no longer "we are on the spawn screen" but "the map's size
        // has reached the big one" — that is how 0x77d3f8 derives it.
        if (updateHudVariables) updateHudVariables(spawned, mapNode.fullSize());

        // A click on the spawn screen. A button has no logic of its own — it runs a
        // console command from setButtonNodeConCmd, so all that is needed here is to
        // find it under the cursor and run it.
        // `--exec` goes the same path as a button click: a button runs a console
        // command, and we run a console command.
        // Only once the screen has been assembled — otherwise there is nothing to
        // make a choice on.
        // We wait not only for the assembled screen but for the spawn circles too:
        // they arrive as events only after the level has loaded, and without them
        // there is nothing to choose from.
        // We also wait for the screen to be assembled for **our** team: before the
        // server's answer it shows the flags of the default one, and the choice would
        // land in another team's spawn group — and the server does not spawn into
        // another team's.
        const bool teamKnown =
            remote == nullptr || (remote->world.ownTeam() > 0 &&
                                  selectedTeam == remote->world.ownTeam());
        if (!args.execLines.empty() && spawnVisible && !spawnPieces.empty() &&
            !spawnMarkerPoints.empty() && teamKnown && !execDone) {
          execDone = true;
          for (const std::string& line : args.execLines) {
            std::printf("  console: %s\n", line.c_str());
            if (!engine.console().executeLine(line)) {
              std::printf("    the command was not recognised\n");
            }
          }
        }

        if (spawnVisible && !spawnPieces.empty()) {
          const auto& input = frameInput;
          // --click --mouse gives one synthetic click: that is how the screen is
          // checked by a screenshot, hands-free.
          const bool clicked = input.clicked || (args.click && frame == 1);
          const float clickX = args.click ? args.mouseX : input.mouseX;
          const float clickY = args.click ? args.mouseY : input.mouseY;
          if (clicked) {
            // The spawn circles first: the data has no nodes for them, the map catches
            // the mouse itself. The game takes the selected one's texture from a
            // separate array (BF2.exe 0x77f7eb against 0x77f7fa — 0x960 for the
            // selected one, 0x950 for not).
            // On the spawn screen the map is in its big presentation, so the mouse has
            // to be caught in that one: in the thumbnail the node stands elsewhere.
            ingameHud.setMapView(obf2::hud::MapView::Maxi);
            const auto hitSpawn = obf2::hud::spawnMarkerAt(
                ingameHud, "MapSplit", hudScreen, spawnContext, clickX, clickY);
            ingameHud.setMapView(obf2::hud::MapView::Mini);
            mapRectStale = true;
            if (hitSpawn) {
              selectedSpawn = static_cast<int>(*hitSpawn);
              spawnDirty = true;
              std::printf("  spawn screen: point %d\n", selectedSpawn);
            }
            // The spawn screen's buttons lie in two branches: SpawnMenu itself and
            // TopLayer, where DONE and SUICIDE sit.
            const obf2::hud::Node* hit = nullptr;
            for (const char* root : {"SpawnMenu", "TopLayer"}) {
              if (const obf2::hud::Node* found = obf2::hud::buttonAt(
                      ingameHud, root, hudScreen, clickX, clickY, &spawnContext)) {
                hit = found;
              }
            }
            if (hit != nullptr) {
              // A button has several commands, and each has its own event. In the data
              // there are four: 0 (247 times), 1 (73), 3 (50) and 2 (11).
              // A click owns 0 and 3 — on the three hang, among others,
              // spawnManager.setPlayerTeam and scoreboard.setToggleShow;
              // 1 and 2 are hover and unhover, holding only sounds.
              for (const auto& [event, line] : hit->commands) {
                if (event != 0 && event != 3) continue;
                std::printf("  spawn screen: %s -> %s\n", hit->name.c_str(), line.c_str());
                if (!engine.console().executeLine(line)) {
                  std::printf("  spawn screen: a command without a handler — %s\n", line.c_str());
                }
              }
            }
          }
        }
        // While at least one node is travelling or fading, the screen has to be
        // rebaked every frame: our geometry lives in meshes on the graphics card.
        {
          const auto now = std::chrono::steady_clock::now();
          const float dt =
              std::chrono::duration<float>(now - lastAnimationTick).count();
          lastAnimationTick = now;
          hudAnimator.advance(dt > 0.25f ? 0.25f : dt);
          if (hudAnimator.animating()) {
            spawnDirty = true;
            // **And the combat one too.** Until now only the spawn screen was rebaked,
            // and the combat HUD's nodes with `setNodeInTime` did not move at all: the
            // geometry stayed as it had been baked.
            hudDirty = true;
          }
          // The speed of 600 comes from the file itself (SetVariableSineAction), and
          // the curve from `MemeDll.dll` 0x10001050: outside the braking stretch the
          // movement is linear, and in `Menu/Ingame` the braking is zero.
          const float slice = dt > 0.25f ? 0.25f : dt;

          // The corner regions. The division of labour here is exactly as in the game:
          // the client's state machine (0x78b600) reads the current values and writes
          // the **targets**, while the `Menu/Ingame` graph moves them. There is no
          // speed in our code at all — they are in the file.
          //
          // We do not turn the "vehicle" mode on yet: 0x78b870 sets it when the
          // player's controlled object is not their soldier.
          const float wasX = bottomLeft.x;
          const float wasHealth = bottomLeft.healthAlpha;
          const float wasRightX = bottomRightX;
          const float wasRightAlpha = bottomRightAlpha;
          if (ingameGraph.file().root() >= 0) {
            auto& variables = ingameGraph.variables();
            bottomLeft.x = variables.get("BottomLeft/BottomLeft_XPos");
            bottomLeft.healthAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha1");
            bottomLeft.vehicleAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha2");
            bottomLeft.update(bottomLeftMode, backgroundAlpha);
            variables.set("BottomLeft/BottomLeft_nextXPos", bottomLeft.targetX);
            variables.set("BottomLeft/Alpha/BottomLeft_nextAlpha1", bottomLeft.targetHealthAlpha);
            variables.set("BottomLeft/Alpha/BottomLeft_nextAlpha2", bottomLeft.targetVehicleAlpha);
            // The right region travels by the graph too, and now exactly as in the
            // game. The machine in the file is five `CullVariableActionNode`
            // (docs/formats/hud-meme-graph.md), while the engine drives only three of
            // its variables, bound to the HUD object's fields (0x7a62c0):
            //
            //   `BottomRight_oldXPos` (+0x28) — the shown position,
            //   `BottomRight_newXPos` (+0x2c) — the hidden one,
            //   `BottomRight_direction` (+0x18) — show it or not.
            //
            // The graph does the rest: to show — travel to `oldXPos` and fade in
            // there; to hide — fade out first and **only once faded** travel to
            // `newXPos`. The right region's alpha comes from here too.
            variables.set("BottomRight/BottomRight_oldXPos", bottomRightShownX);
            variables.set("BottomRight/BottomRight_newXPos",
                          obf2::hud::kBottomRightHiddenX);
            variables.set("BottomRight/BottomRight_direction",
                          bottomRightShow ? 1.0f : 0.0f);
            ingameGraph.update(slice);
            bottomLeft.x = variables.get("BottomLeft/BottomLeft_XPos");
            bottomLeft.healthAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha1");
            bottomLeft.vehicleAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha2");
            bottomLeft.recomputeFaded(backgroundAlpha);
            bottomRightX = variables.get("BottomRight/BottomRight_XPos");
            bottomRightAlpha = variables.get("BottomRight/Alpha/BottomRight_alpha");
          }
          if (bottomLeft.x != wasX || bottomLeft.healthAlpha != wasHealth) hudDirty = true;

          // The map. Its rectangle is not one of the three ready views but the one the
          // animation computed: in the client that is the same target/current pair
          // (0x77c330), and that is exactly why the minimap <-> big map transition
          // looks smooth rather than like a jump.
          // The minimap's compass. 0x751d8b computes the direction as
          // `atan2(direction.x, direction.z)`, and ours is exactly that: zero looks
          // along +Z, so this is simply the look angle.
          // The angle goes into a variable — while the compass is drawn by a live node
          // (`hudDynamic`), not by the shared geometry. It must not be routed through
          // `hudDirty`: rebaking the whole HUD every frame is exactly the frame drop
          // one can see by eye.
          mapAngle.setTarget(yaw * 3.14159265358979323846f / 180.0f);
          mapAngle.update(slice);
          hudValues["MinimapDelayedMapAngle"] = mapAngle.delayed();

          // Where the map looks: the player's position in fractions of the world.
          // 0x751d55 computes it (`(sizeX/2 + playerX) / sizeX`) and hands it to
          // 0x773630, which writes the target +0x740/+0x744.
          if (hudContext.mapWorldSize > 1.0f) {
            const float world = hudContext.mapWorldSize;
            mapNode.setCentre((world * 0.5f + eye.x) / world, (world * 0.5f - eye.z) / world);
          }

          const auto wasSize = mapNode.size();
          const auto wasPosition = mapNode.position();
          mapNode.update(slice);
          const auto position = mapNode.position();
          const auto size = mapNode.size();
          const bool moved = size.x != wasSize.x || size.y != wasSize.y ||
                             position.x != wasPosition.x || position.y != wasPosition.y;
          // `setMapRect` drags a `finish()` over all 1659 nodes with it, so we call it
          // only when the rectangle really changed — or when somebody else managed to
          // move it (a spawn screen rebuild sets the big presentation itself, through
          // `setMapView`).
          if (moved || mapRectStale) {
            mapRectStale = false;
            ingameHud.setMapRect(position.x, position.y, size.x, size.y,
                                 mapNode.minSize() ? obf2::hud::MapView::Mini
                                                   : obf2::hud::MapView::Maxi);
            hudDirty = true;
          }

          // The map's window. The thumbnail in the corner follows the player and zooms
          // in by the zoom index; the big map shows the whole combat area, as before.
          // How exactly the engine blends these two centres — 0x773870 takes the weight
          // from +0x694 — has **not been worked out**, so for now there are simply two
          // branches.
          if (mapBaseHalfU > 0.0f) {
            const float scale = mapNode.minSize() ? mapNode.zoomScale() : 1.0f;
            const float halfU = mapBaseHalfU / scale;
            const float halfV = mapBaseHalfV / scale;
            const float cu = mapNode.minSize() ? mapNode.centre().x : mapBaseCentreU;
            const float cv = mapNode.minSize() ? mapNode.centre().y : mapBaseCentreV;
            hudContext.mapU0 = cu - halfU;
            hudContext.mapU1 = cu + halfU;
            hudContext.mapV0 = cv - halfV;
            hudContext.mapV1 = cv + halfV;
          }

          // The right region is driven by the graph — see above. All that is left here
          // is to say that it moved.
          if (bottomRightX != wasRightX || bottomRightAlpha != wasRightAlpha) hudDirty = true;
        }
        // We rebuild the spawn screen only while it is on screen.
        if (spawnDirty && spawnVisible && rebuildSpawn) {
          spawnDirty = false;
          rebuildSpawn();
        }
        if (hudDirty && rebuildIngame) {
          hudDirty = false;
          rebuildIngame();
        }
        for (const KeyScreen& screen : keyScreens) {
          const bool forced = screen.group == args.hudScreenName;
          bool visible = forced;
          if (!visible && screen.heldByKey) {
            const std::string_view key = controls.key(screen.action);
            visible = !key.empty() && device->isKeyDown(key);
          } else if (!visible) {
            visible = screen.state == hudState;
          }
          if (!visible) continue;
          extra.insert(extra.end(), screen.quads.begin(), screen.quads.end());
        }

        // The live nodes are rebuilt **before** the frame is assembled: their pieces
        // are substituted in place of the markers in `ingamePieces`.
        for (DynamicNode& dynamic : hudDynamic) {
          const obf2::hud::Node& node = *dynamic.node;
          const bool isBar = node.type == obf2::hud::NodeType::Bar;
          const bool isRotating = !node.rotateVariable.empty();
          const bool isMap = node.type == obf2::hud::NodeType::Map ||
                             node.type == obf2::hud::NodeType::MiniMap;

          std::string text;
          float value = 0.0f;
          float angle = 0.0f;
          if (isRotating) {
            const auto found = hudValues.find(node.rotateVariable);
            angle = found == hudValues.end() ? 0.0f : found->second;
          } else if (isBar) {
            const auto found = hudValues.find(node.valueVariable);
            value = found == hudValues.end() ? 0.0f : found->second;
          } else if (!isMap) {
            const auto found = hudStrings.find(node.textVariable);
            if (found != hudStrings.end()) text = found->second;
          }

          // A rotation step below which nothing is visible on screen any more: the
          // compass is 192 pixels, so 0.005 radians is half a pixel at the edge.
          // Without this threshold we would rebake the node every frame even while the
          // angle settles in its last digits.
          //
          // The map is rebaked when its window moved: the centre follows the player
          // and the window's size follows the zoom. The threshold is 0.0002 of the
          // world's width, that is less than a minimap pixel.
          const bool changed =
              !dynamic.built ? true
              : isMap        ? std::abs(hudContext.mapU0 - dynamic.shownValue) > 0.0002f ||
                                   std::abs(hudContext.mapV0 - dynamic.shownAngle) > 0.0002f
              : isRotating   ? std::abs(angle - dynamic.shownAngle) > 0.005f
              : isBar        ? std::abs(value - dynamic.shownValue) > 0.001f
                             : text != dynamic.shownText;
          if (changed) {
            // The value changed — we rebuild only this node.
            for (OwnedPiece& piece : dynamic.pieces) renderer->release(piece.mesh);
            dynamic.pieces.clear();
            dynamic.built = true;
            dynamic.shownValue = isMap ? hudContext.mapU0 : value;
            dynamic.shownText = text;
            dynamic.shownAngle = isMap ? hudContext.mapV0 : angle;

            obf2::hud::Node copy = node;
            // We take the general context rather than an empty one: otherwise the live
            // captions are drawn with the default font instead of their own
            // (setTextNodeStyle) and without localisation.
            obf2::hud::Context single = hudDynamicContext;
            if (isMap) {
              // The map's window lives in `hudContext` and changes every frame, while
              // `hudDynamicContext` is a snapshot; we carry it over by hand.
              single.mapU0 = hudContext.mapU0;
              single.mapU1 = hudContext.mapU1;
              single.mapV0 = hudContext.mapV0;
              single.mapV1 = hudContext.mapV1;
            } else if (isRotating) {
              single.variableValue = [&](std::string_view) { return angle; };
            } else if (isBar) {
              single.variableValue = [&](std::string_view) { return value; };
            } else {
              copy.text = text;
              copy.textVariable.clear();
            }
            if (isMap || isRotating || isBar || !text.empty()) {
              // The map gives more than one piece: the picture itself, the capture
              // points' icons and their captions.
              for (auto& built : obf2::hud::buildNode(copy, hudFont.font, hudFont.atlasPath,
                                                      hudScreen, single)) {
                if (auto uploaded = renderer->upload(built.geometry, resolveTexture)) {
                  dynamic.pieces.push_back(OwnedPiece{*uploaded, built.tint});
                }
              }
            }
          }
        }

        const auto pushHud = [&](int index) {
          if (index < 0 || !uploadedOk[static_cast<std::size_t>(index)]) return;
          obf2::gfx::MeshRenderer::DrawItem item{&gpuMeshes[static_cast<std::size_t>(index)],
                                                 obf2::Mat4::identity()};
          const auto tint = hudTints.find(index);
          if (tint != hudTints.end()) {
            item.tint[0] = tint->second.r;
            item.tint[1] = tint->second.g;
            item.tint[2] = tint->second.b;
            item.tint[3] = tint->second.a;
          }
          hudItems.push_back(item);
        };
        const auto pushPiece = [&](const OwnedPiece& piece) {
          obf2::gfx::MeshRenderer::DrawItem item{&piece.mesh, obf2::Mat4::identity()};
          item.tint[0] = piece.tint.r;
          item.tint[1] = piece.tint.g;
          item.tint[2] = piece.tint.b;
          item.tint[3] = piece.tint.a;
          hudItems.push_back(item);
        };
        for (const OwnedPiece& piece : ingamePieces) {
          if (piece.live == nullptr) {
            pushPiece(piece);
            continue;
          }
          // The live node's place: we substitute its pieces exactly here rather than at
          // the end — otherwise the map would land over its own frame.
          for (const DynamicNode& dynamic : hudDynamic) {
            if (dynamic.node != piece.live) continue;
            for (const OwnedPiece& own : dynamic.pieces) pushPiece(own);
            break;
          }
        }
        for (const int index : extra) pushHud(index);
        if (spawnVisible) {
          for (const OwnedPiece& piece : spawnPieces) {
            obf2::gfx::MeshRenderer::DrawItem item{&piece.mesh, obf2::Mat4::identity()};
            item.tint[0] = piece.tint.r;
            item.tint[1] = piece.tint.g;
            item.tint[2] = piece.tint.b;
            item.tint[3] = piece.tint.a;
            hudItems.push_back(item);
          }
        }

        renderer->renderOverlay(*acquired, hudItems, obf2::gfx::Color{}, false);
      }
    }

    const bool lastFrame = args.frames > 0 && frame + 1 >= args.frames;
    if (lastFrame && !args.screenshot.empty()) {
      if (device->submitAndSave(*acquired, args.screenshot.c_str(), &error)) {
        std::printf("screenshot: %s\n", args.screenshot.c_str());
      } else {
        std::fprintf(stderr, "%s\n", error.c_str());
      }
    } else {
      device->submit(*acquired);
    }

    if (frame == 0 && !bootMode) {
      std::printf("culling: drawn %d, culled %d of %zu instances\n",
                  renderer->drawnLastFrame(), renderer->culledLastFrame(), items.size());
    }

    if (menuQuit) break;
    // A level from the menu: we end the menu's session and pass the choice upwards.
    if (!requestedLevel.empty()) break;

    ++frame;
    if (args.frames > 0 && frame >= args.frames) break;
  }

  for (auto& dynamic : hudDynamic) {
    for (OwnedPiece& piece : dynamic.pieces) renderer->release(piece.mesh);
  }
  for (OwnedPiece& piece : spawnPieces) renderer->release(piece.mesh);
  for (auto& gpuMesh : gpuMeshes) renderer->release(gpuMesh);
  std::printf("frames drawn: %d\n", frame);
  if (nextLevel != nullptr) *nextLevel = requestedLevel;
  return 0;
}

int main(int argc, char** argv) {
  Args args = parseArgs(argc, argv);

  obf2::FileSystem files;
  std::vector<std::string> mountErrors;
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += obf2::mountArchivesFromCon(files, args.modDir, args.modDir / list, &mountErrors);
  }
  files.mountDirectory(args.modDir);

  std::printf("OpenBattlefield2 | %s/%s\n", OBF2_PLATFORM_NAME, OBF2_ARCH_NAME);
  std::printf("mod: %s | archives: %d\n", args.modDir.string().c_str(), mounted);
  for (const auto& e : mountErrors) std::printf("  [mount] %s\n", e.c_str());

  if (!args.calibrate.empty()) return runCalibrate(args, files);

  // Connecting to a real server is a mode of its own: neither a window nor a level
  // is needed here.
  if (args.probe) return runProbe(args, files);

  // Playing on a real server: first we carry the handshake to the point where the
  // server says which level it is playing, and only then open the window — the level
  // is assigned by it, not by us.
  if (!args.connectTo.empty()) {
    RemoteWorld remote(args, files);
    if (!remote.connect()) return 1;
    for (int i = 0; i < 40 && !remote.levelReady; ++i) remote.pump(500);
    if (!remote.levelReady) {
      std::fprintf(stderr, "the server did not say which level it is playing\n");
      remote.disconnect();
      return 1;
    }
    args.levelName = remote.levelName;

    // Wait for at least a few objects: the server sends them right after the level,
    // and the very first flag says where to look. Otherwise the camera stands where
    // we put it ourselves — that is, nowhere.
    for (int i = 0; i < 20 && remote.objects.size() < 4; ++i) remote.pump(200);
    if (!args.focus && !remote.objects.empty()) {
      const obf2::Vec3f at = remote.objects.begin()->second;
      std::printf("camera: next to object %u (%.0f %.0f %.0f)\n",
                  remote.objects.begin()->first, at.x, at.y, at.z);
      args.focus = at;
      if (args.distance <= 0.0f) args.distance = 60.0f;
    }

    const int code = runSession(args, files, nullptr, &remote);
    remote.report();
    remote.disconnect();
    return code;
  }

  // The menu and the game are two sessions in a row: a level chosen in the menu
  // simply starts the next one with different arguments.
  while (true) {
    std::string nextLevel;
    const int code = runSession(args, files, &nextLevel);
    if (code != 0 || nextLevel.empty()) return code;

    std::printf("menu -> game: %s\n", nextLevel.c_str());
    args.levelName = nextLevel;
    args.hosted = true;
    args.screen.clear();
  }
}
