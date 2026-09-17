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
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "obf2/app/command_line.h"
#include "obf2/app/hosted_game.h"
#include "obf2/app/scene_build.h"
#include "obf2/core/math.h"
#include "obf2/core/parallel.h"
#include "obf2/core/path.h"
#include "obf2/core/platform.h"
#include "obf2/engine/engine.h"
#include "obf2/font/font_file.h"
#include "obf2/font/text.h"
#include "obf2/game/object_mesh.h"
#include "obf2/hud/bottom_left.h"
#include "obf2/hud/ingame.h"
#include "obf2/hud/combat_area.h"
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
#include "obf2/game/template_numbers.h"
#include "obf2/game/soldier_model.h"
#include "obf2/anim/system.h"
#include "obf2/anim/player.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/gameplay.h"
#include "obf2/session/remote_world.h"
#include "obf2/level/level.h"
#include "obf2/level/placement_index.h"
#include "obf2/level/lightmap_atlas.h"
#include "obf2/server/game_client.h"
#include "obf2/server/physics.h"
#include "obf2/server/soldier_look.h"
#include "obf2/server/soldier_move.h"
#include "obf2/server/soldier_sprint.h"
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
#include "obf2/server/collision_objects.h"
#include "obf2/server/collision_world.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

namespace {

// The session lives in its own module now (`obf2/session/remote_world.h`); these
// keep the names the rest of this file has always used.
using obf2::session::ContentHashes;
using obf2::session::DrawStage;
using obf2::session::KnownObject;
using obf2::session::RemoteWorld;
using obf2::session::buildKnownObjects;
using obf2::session::buildRegistry;
using obf2::session::contentHashes;
using obf2::session::drawStageName;

// The command line is its own module (`obf2/app/command_line.h`), the way the
// engine reads its arguments before anything is loaded.
using obf2::app::Args;
using obf2::app::parseArgs;

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The pieces that used to live here and now have modules of their own. Only the
// names are kept, so that the command line's two overrides are applied in one
// place rather than at every call.
using obf2::font::LoadedFont;
using obf2::font::loadFont;
using obf2::game::resolveGeometryPath;
using obf2::mesh::buildScreenQuad;

obf2::game::DrawStage checkDrawable(obf2::FileSystem& files, const obf2::game::Registry& registry,
                                    const std::string& templateName, const Args& args) {
  return obf2::game::checkDrawable(files, registry, templateName, args.geometryIndex,
                                   args.lodIndex);
}

std::optional<obf2::mesh::RenderMesh> loadMesh(obf2::FileSystem& files, const std::string& path,
                                               int geometryOverride, int lodIndex, bool verbose) {
  return obf2::game::loadMesh(files, path, geometryOverride, lodIndex, verbose);
}

std::optional<obf2::mesh::RenderMesh> buildObjectMesh(obf2::FileSystem& files,
                                                      const obf2::game::Registry& registry,
                                                      const std::string& templateName,
                                                      const Args& args, bool verbose) {
  return obf2::game::buildObjectMesh(files, registry, templateName, args.geometryIndex,
                                     args.lodIndex, verbose);
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

// Text mode: connect, spin for a while and report what arrived.
// What the session takes from the command line, out of everything `Args` holds.
obf2::session::Settings sessionSettings(const Args& args) {
  obf2::session::Settings settings;
  settings.connectTo = args.connectTo;
  settings.connectPassword = args.connectPassword;
  settings.playerName = args.playerName;
  settings.modDir = args.modDir;
  settings.recordTo = args.recordTo;
  settings.ordinal = args.ordinal;
  settings.blockReady = args.blockReady;
  settings.skipContent = args.skipContent;
  settings.skipDatabase = args.skipDatabase;
  settings.startSimulation = args.startSimulation;
  settings.traceOwnState = args.traceOwnState;
  return settings;
}

// There is deliberately no window here — this is an instrument for taking the protocol apart.
int runProbe(const Args& args, obf2::FileSystem& files) {
  RemoteWorld remote(sessionSettings(args), files);
  remote.drawabilityOf = [&](const std::string& name) {
    return checkDrawable(files, remote.registry, name, args);
  };
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

  // The scene and everything the level adds to it (`obf2/app/scene.h`).
  obf2::app::Scene scene;
  obf2::app::LevelScene levelScene;

  std::optional<obf2::level::Level> level;
  obf2::game::Registry registry;
  // The collision meshes every world's layers point into: declared before the
  // worlds, so it outlives them.
  std::unique_ptr<obf2::server::CollisionLibrary> collisionLibrary;
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
    levelScene = obf2::app::buildLevelScene(files, *level, scene);

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
      collisionLibrary = std::make_unique<obf2::server::CollisionLibrary>(files, registry);
      obf2::app::HostedGame hosted = obf2::app::startHostedGame(files, *level, *collisionLibrary);
      hostedServer = std::move(hosted.server);
      hostedClient = std::move(hosted.client);
      localSoldierId = hosted.localSoldierId;
      placement = std::move(hosted.placement);
    }

    obf2::app::SceneOptions sceneOptions;
    sceneOptions.geometryIndex = args.geometryIndex;
    sceneOptions.lodIndex = args.lodIndex;
    sceneOptions.noLightmaps = args.noLightmaps;
    obf2::app::placeObjects(files, registry, placement, sceneOptions, levelScene, scene,
                            [remote]() {
                              if (remote != nullptr) remote->keepAlive();
                            });

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
            stages.push_back(obf2::mesh::PoseStage{&clip, static_cast<float>(frame), 1.0f});
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

  // The spawn screen's red hatch, handed to the resolver under `#combatarea` —
  // like `#flash`, a picture we make rather than a file. It is the game's own
  // `map_CombatArea32.dds` with the alpha cleared inside the level's combat area
  // (obf2/hud/combat_area.h). It is declared here, at the outer level, because
  // the resolver below is called from the frame loop: kept inside the level
  // block it would be dead memory by then (CLAUDE.md, the first of the rakes).
  std::optional<obf2::texture::Texture> combatAreaOverlay;

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
    if (mapName == obf2::hud::kCombatAreaTexture) return combatAreaOverlay;
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
  int& selectedSpawn = spawnScreen.mutableChoice().marker;
  // The control point's id for every circle, in the same order. It is exactly what
  // the server expects: in the engine the player sends not coordinates but a
  // group's number, and a group is the set of points of one flag
  // (docs/functions/spawn.md).
  // They live in the module (`SpawnInterface::markerPoints`), set on every rebuild.
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
  bool zoomKeyWasDown = false;
  // There was no state yet: -1 leads into the first switch's `default` branch, that
  // is "clear absolutely everything" (0x78653c).
  int hudStatePrevious = -1;
  bool bottomRightShow = false;
  float bottomRightShownX = obf2::hud::kBottomRightShownX;
  // Nodes appearing and disappearing over time — what the MemeFile graph governs in
  // the game (see obf2/hud/animation.h).
  obf2::hud::Animator hudAnimator;
  std::chrono::steady_clock::time_point lastAnimationTick = std::chrono::steady_clock::now();
  // What the interface's rebuilds cost, in milliseconds and in number, split
  // between building the geometry and putting it on the card.
  int hudRebuilds = 0;
  float hudRebuildMs = 0.0f;
  float hudRebuildMax = 0.0f;
  float hudUploadMs = 0.0f;
  int hudUploaded = 0;
  std::function<void()> rebuildSpawn;
  // These two are needed by the rebuild, and it is called from the drawing loop —
  // that is, already outside the level loading block. Keeping them inside is not
  // allowed: a reference in the lambda would become dangling.
  std::function<void()> applySpawnState;
  // And the three the spawn screen's state asks for a side's name with. They are
  // the same case once more: written inside the loading block, held by reference
  // by `applySpawnState`, and called from the frame loop long after that block has
  // gone. AddressSanitizer caught it as a stack-use-after-scope at the line that
  // reads `level->teamNames`, and in the release build it was a plain crash on the
  // first frame with `--level` — the rebuild called a lambda whose captures no
  // longer existed, so the whole HUD came out with zero pieces before it died.
  std::function<std::string(int)> teamName;
  std::function<std::string(int)> teamLabel;
  std::function<std::string(int)> teamFlagIcon;
  // `--hud-rects`, and the same case a third time: the combat HUD's rebuild hands
  // it to `buildIngame` as a callback, and that rebuild runs from the frame loop.
  std::function<void(const char*, const std::vector<obf2::hud::DrawPiece>&)> reportRects;
  obf2::hud::Context spawnContext;
  // The same case as with spawnContext: the combat HUD is now rebuilt from the
  // frame loop, and the lambda holds the context by reference. While it was local
  // to the setup block, after the block exited rebuildIngame read dead memory — the
  // path to the map's picture arrived as rubbish, and the minimap was not drawn.
  obf2::hud::Context hudContext;
  // The level's capture points — the map's markers are rebuilt from them every
  // time: a flag stands on each, while a spawn selection circle stands only on our own.
  std::vector<obf2::level::ControlPoint> hudControlPoints;
  // The map's vehicles and strategic objects. They do not depend on the player's
  // side, so they are built once with the level and re-seeded into the spawn
  // screen's context every time it is rebuilt.
  std::vector<obf2::hud::Context::MapMarker> assetMapMarkers;
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
      // The spawn screen is baked geometry we keep, and its nodes hang on the same
      // variables: a state that changes them changes what it should be showing. The
      // engine has no such problem — it walks the tree every frame — so this is our
      // cache to invalidate, and forgetting it is what left the screen empty. The
      // first build runs before the state is ever set to 1, so **every** piece of
      // it was hidden, and on a round where nothing else asked for a rebuild it
      // stayed that way and the player could not spawn.
      if (hudStatePrevious != state) spawnDirty = true;
      hudStatePrevious = state;
    };

    // The combat HUD is state 0.
    applyHudState(0);

    // ToggleScore is the scoreboard's "Players" tab. That it is the default is
    // visible in the binary: the flag field (Scoreboard+0x365) has exactly one
    // constant store, `movb $0x1, 0x365(%esi)` at 0x7a48f7.
    hudVariables["ToggleScore"] = true;
    hudVariables["ToggleSquads"] = false;
    hudVariables["ToggleManage"] = false;

    // Source not found: CPInterfaceEnabled (the HUD object's field 0xa8) is written
    // by many places in the game, and which of them is ours is not established.
    // Without it the capture points' bars are not visible. Debt.
    hudVariables["CPInterfaceEnabled"] = true;

    // The bar across the top of the spawn screen — `SpawnInfo` in
    // `HudElementsSpawn.con`: the plate `TopMiddleBar` at 250,0 270x19 and the
    // caption `TimeToSpawn` on `SpawnInfoString` over it.
    //
    // Both come from `HudInformationLayer`'s per-frame update (`BF2.exe`,
    // 0x4668d0). `SpawnInfoShow` (+0x1d3) is on unless the HUD state is 11 or 12
    // (0x467381 sets one, 0x467391 zero). The caption is `presstospawn` when the
    // state is 13 or 17 **and** a list the game keeps is not empty (0x4672c2 and
    // 0x4672eb), and `selectspawnpoint` otherwise — which is the branch the spawn
    // screen takes. The other two keys the same function writes,
    // `invalidspawnpoint` and `timetospawn`, belong to states we do not model yet.
    // The caption. The same function chooses between four keys by the HUD's state
    // and a list it keeps: `presstospawn` in the states 1, 13, 17 and 18 when that
    // list is not empty (0x46729c through 0x4672eb), `selectspawnpoint` otherwise,
    // and `timetospawn` / `instantspawn` in branches of their own. The spawn screen
    // with nothing chosen takes `selectspawnpoint`, which is what the original
    // draws; the other three are debt, because what the list holds is not
    // established. `SpawnInfoShow` is set per frame, see `updateHudVariables`.
    hudStrings["SpawnInfoString"] = "HUD_CENTERINFOBOX_selectspawnpoint";

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
      // The screen's own commands are the module's, the one the test covers
      // (`obf2/hud/spawn_interface.h`). They used to be written out a second time
      // right here — the same logic in two places, and the tested copy was not the
      // one that ran.
      spawnScreen.bind(
          console,
          [&](int team, int kit, int group) {
            return requestSpawn ? requestSpawn(team, kit, group) : false;
          },
          [&]() {
            if (remote != nullptr) remote->commitSuicide();
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
      // The scoreboard's three tabs. The buttons in the data run
      // `scoreboard.setToggleShow 0|1|2` (`HudElementsScoreboard.con`), and the
      // three flags they pick between are neighbours in the Scoreboard object —
      // `ToggleSquads` +0x364, `ToggleScore` +0x365, `ToggleManage` +0x366
      // (docs/functions/hud-variables.md). Exactly one is on at a time: each tab's
      // picture and its bright label hang on its own flag, and the faded label on
      // `NOT` it.
      console.bind("scoreboard.setToggleShow", [&](const obf2::con::Command& command) {
        const int tab = command.argInt(0).value_or(0);
        hudVariables["ToggleScore"] = tab == 0;
        hudVariables["ToggleSquads"] = tab == 1;
        hudVariables["ToggleManage"] = tab == 2;
        hudDirty = true;
        std::printf("  scoreboard: tab %d\n", tab);
      });
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
      // The bar across the top belongs to the spawn flow. `SpawnInfoShow`
      // (+0x1d3) is written by 0x4668d0, and there it is `state != 11 && state
      // != 12` — but the whole block sits behind an outer branch at 0x467278
      // that we have not read, so in combat the condition is **not measured**.
      // What is measured is the spawn screen: the original draws the bar there,
      // and the caption on it is `selectspawnpoint`
      // (docs/research/spawn-screen-named.md). So we show it exactly there.
      hudVariables["SpawnInfoShow"] = !hasPlayer;
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
    teamName = [&](int team) -> std::string {
      if (!level || team < 0 || team > 2) return {};
      return level->teamNames[team];
    };
    // The conversions from a side's name into keys and paths live in
    // `obf2/hud/spawn.h` together with their test.
    teamLabel = [&](int team) { return obf2::hud::armyLabelKey(teamName(team)); };
    teamFlagIcon = [&](int team) { return obf2::hud::teamFlagIcon(teamName(team)); };

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
      std::vector<int> markerPoints;
      for (const auto& point : hudControlPoints) {
        obf2::hud::Context::MapMarker marker;
        marker.worldX = point.position.x;
        marker.worldZ = point.position.z;
        marker.label = point.nameKey;
        marker.texture = obf2::hud::controlPointIcon(point.team == 0 ? "" : teamName(point.team),
                                                     point.unableToChangeTeam);
        spawnContext.mapMarkers.push_back(std::move(marker));
        if (point.team == selectedTeam) {
          const bool chosen =
              static_cast<int>(spawnContext.spawnMarkers.size()) == selectedSpawn;
          spawnContext.spawnMarkers.push_back(
              obf2::hud::Context::SpawnMarker{point.position.x, point.position.z, chosen});
          markerPoints.push_back(point.id);
        }
      }
      spawnScreen.setMarkerPoints(std::move(markerPoints));
      // The vehicles and the strategic objects come after the flags, the way the
      // original's batch has them.
      spawnContext.mapMarkers.insert(spawnContext.mapMarkers.end(), assetMapMarkers.begin(),
                                     assetMapMarkers.end());
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
      // The scoreboard's two headers name the sides with the same pair
      // (`FriendlyTeamNameString` +0x52c, `EnemyTeamNameString` +0x530), and the
      // same 0x787260 fills them beside the flags. `FriendlyTeamWinsString` and
      // `EnemyTeamWinsString` are the rounds won; we count no rounds, so those two
      // stay empty and the headers show the caption without a number — debt.
      hudStrings["FriendlyTeamNameString"] =
          std::string(engine.lexicon().text(teamLabel(selectedTeam)));
      hudStrings["EnemyTeamNameString"] =
          std::string(engine.lexicon().text(teamLabel(selectedTeam == 2 ? 1 : 2)));
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
    // The game shows not the whole level picture but a square around the combat area,
    // centred on the area's centre. Its side is the area's **larger side plus a fixed
    // margin**, and the margin is a fraction of the picture and not of the area:
    //
    //   side = max(width, height) + world * 0.078125
    //
    // 0.078125 is 40 texels of the 512-wide map picture on each side. Measured from
    // frame dumps of the original on two levels whose terrains differ in size, and it
    // is the difference in size that separates this rule from the "times 1.2" we used
    // before:
    //
    //   Strike at Karkand, world 1024, area 640.4 tall -> side 720.4, v span 0.70349
    //                                                     the original: 0.7035
    //   Dalian Plant,      world 2048, area 808 tall   -> side 968,   u span 0.47266
    //                                                     the original: 0.4728
    //
    // Times 1.2 gives Karkand 0.7505, which is half the map out. Where the address of
    // the constant is in the binary is not known — it is a measurement, not a read.
    //
    // The square may hang off the picture; the renderer cuts it there rather than
    // sliding it back, and narrows the rectangle on screen to match.
    //
    // The picture is oriented so that z grows upwards: v = (1024 - z) / 2048.
    constexpr float kMapMargin = 40.0f / 512.0f;
    const auto hudGameplay =
        level ? obf2::level::loadGameplayObjects(files, level->name, "gpm_cq", 16)
              : std::optional<obf2::level::GameplayObjects>{};
    if (hudGameplay && !hudGameplay->combatArea.empty()) {
      float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
      hudGameplay->combatArea.bounds(minX, maxX, minZ, maxZ);
      const float world = static_cast<float>(level ? level->primary.size - 1 : 1024) *
                          (level ? level->primary.scale.x : 2.0f);
      const float half = (std::max(maxX - minX, maxZ - minZ) + world * kMapMargin) * 0.5f;
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

      // The red hatch over everything outside the combat area. The square it
      // covers is the **uncut** one — the same square the crop is built from,
      // before the map's edge takes a bite out of it — because the original's
      // overlay covers the map node whole while its picture does not.
      if (const auto bytes = files.read(obf2::normalizeAssetPath(obf2::hud::kCombatAreaSource))) {
        std::string textureError;
        if (const auto hatch = obf2::texture::loadImage(*bytes, &textureError)) {
          const obf2::hud::WorldSquare square{centerX - half, centerZ - half, centerX + half,
                                              centerZ + half};
          combatAreaOverlay =
              obf2::hud::buildCombatAreaOverlay(*hatch, hudGameplay->combatArea.points, square);
          hudContext.combatAreaTexture = std::string(obf2::hud::kCombatAreaTexture);
          std::printf("  map: combat-area hatch over %.0f..%.0f / %.0f..%.0f, %zu points\n",
                      square.minX, square.maxX, square.minZ, square.maxZ,
                      hudGameplay->combatArea.points.size());
        } else {
          std::printf("  map: the combat-area hatch did not read: %s\n", textureError.c_str());
        }
      }

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
        marker.texture = obf2::hud::controlPointIcon(point.team == 0 ? "" : teamName(point.team),
                                                     point.unableToChangeTeam);
        hudContext.mapMarkers.push_back(std::move(marker));
      }
      std::printf("  map: capture points %zu\n", hudControlPoints.size());

      // --- what stands on the map besides the flags ------------------
      //
      // The original's map carries two more kinds of icon, and both come out of
      // the objects' own templates rather than out of the HUD's data:
      //
      //   * a vehicle spawner shows the vehicle it issues, 16x16, from that
      //     template's `vehicleHud.miniMapIcon`;
      //   * anything with a `StrategicObject` component shows 19x19, from its
      //     `StrategicObject.intactIcon` — bridges, mobile radars, the air
      //     control tower (the UAV), the artillery pieces. The component also
      //     carries a `destroyedIcon`, which we do not use yet: nothing tells us
      //     an object has been destroyed.
      //
      // The sizes are measured on the original's spawn screen, icon by icon
      // (docs/research/spawn-screen-named.md).
      const auto iconOf = [&](const std::string& templateName, std::string_view component,
                              std::string_view property) -> std::string {
        const auto* object = registry.find(templateName);
        if (object == nullptr) return {};
        const auto* part = object->component(component);
        if (part == nullptr) return {};
        const auto at = part->properties.find(std::string(property));
        if (at == part->properties.end() || at->second.empty()) return {};
        std::string path(at->second.back().value());
        for (char& c : path) {
          if (c == '\\') c = '/';
        }
        return path;
      };

      for (const auto& spawner : hudGameplay->spawners) {
        // Which side's vehicle stands there is the point's business; where the
        // point is neutral or unknown we take whichever template the spawner
        // lists first, because the icon is the same shape either way.
        const auto* point = hudGameplay->controlPoint(spawner.controlPointId);
        std::string vehicle;
        if (point != nullptr) {
          if (const auto found = spawner.templateByTeam.find(point->team);
              found != spawner.templateByTeam.end()) {
            vehicle = found->second;
          }
        }
        if (vehicle.empty() && !spawner.templateByTeam.empty()) {
          vehicle = spawner.templateByTeam.begin()->second;
        }
        if (vehicle.empty()) continue;
        // A spawner can put a strategic object on the field rather than a
        // vehicle — the mobile radars, the artillery pieces. Those show the
        // strategic icon at its own size, which is why the original's map has a
        // `Radar` and two `AirDef` on it and no vehicle icon in their place.
        std::string icon = iconOf(vehicle, "StrategicObject", "intacticon");
        float size = 19.0f;
        if (icon.empty()) {
          icon = iconOf(vehicle, "VehicleHud", "minimapicon");
          size = 16.0f;
        }
        if (icon.empty()) continue;
        assetMapMarkers.push_back(obf2::hud::Context::MapMarker{
            spawner.position.x, spawner.position.z, std::move(icon), {}, size});
      }
      if (level) {
        for (const auto& object : level->objects) {
          std::string icon = iconOf(object.templateName, "StrategicObject", "intacticon");
          if (icon.empty()) continue;
          assetMapMarkers.push_back(obf2::hud::Context::MapMarker{
              object.position.x, object.position.z, std::move(icon), {}, 19.0f});
        }
      }
      hudContext.mapMarkers.insert(hudContext.mapMarkers.end(), assetMapMarkers.begin(),
                                   assetMapMarkers.end());
      std::printf("  map: vehicles and assets %zu\n", assetMapMarkers.size());
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
    reportRects = [&](const char* where, const std::vector<obf2::hud::DrawPiece>& pieces) {
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
      std::vector<OwnedPiece> reusable = std::move(ingamePieces);
      std::size_t reuse = 0;
      ingamePieces.clear();
      for (const char* root : {"Global", "BottomLeftAnimate", "BottomLeftStatic",
                               "BottomRightAnimate", "BottomRightStatic"}) {
        obf2::hud::updateAnimator(ingameHud, root, hudAnimator, hudContext);
      }
      auto ingameBuilt = buildIngamePieces();
      const auto ingameUploadStart = std::chrono::steady_clock::now();
      renderer->beginUploadBatch();
      for (auto& piece : ingameBuilt) {
        if (piece.live) {
          // A live node's marker: there is no mesh here, only a place in the queue.
          ingamePieces.push_back(OwnedPiece{{}, piece.tint, piece.node});
          continue;
        }
        if (reuse < reusable.size()) {
          obf2::gfx::GpuMesh& older = reusable[reuse].mesh;
          if (older.vertices != nullptr &&
              renderer->refill(older, piece.geometry, resolveTexture)) {
            ingamePieces.push_back(OwnedPiece{older, piece.tint, nullptr});
            older = obf2::gfx::GpuMesh{};
            ++reuse;
            continue;
          }
          ++reuse;
        }
        if (auto uploaded = renderer->upload(piece.geometry, resolveTexture)) {
          ingamePieces.push_back(OwnedPiece{*uploaded, piece.tint, nullptr});
          ++hudUploaded;
        }
      }
      renderer->endUploadBatch();
      for (OwnedPiece& left : reusable) renderer->release(left.mesh);
      hudUploadMs += std::chrono::duration<float, std::milli>(
                         std::chrono::steady_clock::now() - ingameUploadStart)
                         .count();
      if (ingameReported) {
        std::printf("  HUD: the combat one rebuilt, pieces %zu\n", ingamePieces.size());
      }
      ingameReported = true;
    };
    hudContext.showState = [&](const obf2::hud::Node& node) { return hudAnimator.state(node); };
    rebuildIngame();

    rebuildSpawn = [&]() {
      // Only our own meshes. This used to release `ingamePieces` as well — without
      // clearing the vector and without rebuilding it — so every rebuild of the
      // spawn screen handed the graphics card's meshes back while the frame loop
      // went on drawing from the same handles. When a rebuild came with
      // `hudDirty` set the combat HUD was rebaked straight after and the damage
      // was invisible; when it came from a click alone — choosing a kit, a side,
      // the squad tab — nothing rebuilt it, and the combat HUD flickered over the
      // screen until the next animation frame.
      // The pieces of the previous bake are kept, not freed: a rebuild writes the
      // new geometry into the buffers that are already there and only falls back
      // to making new ones when a piece has outgrown its own
      // (`MeshRenderer::refill`). Creating a pair of buffers per piece is a trip
      // into the driver each, and that was most of what a rebuild cost.
      std::vector<OwnedPiece> reusable = std::move(spawnPieces);
      std::size_t reuse = 0;
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
      // `--hud-rects` for the spawn screen. It used to be written out again right
      // here, because `reportRects` was a lambda of the level-loading block and a
      // call into it from the frame loop read a closure that was no longer there
      // (CLAUDE.md, the first of the rakes). It outlives the block now, so the one
      // copy of the loop serves both.
      if (reportRects) reportRects("SpawnMenu", built);
      ingameHud.setMapView(obf2::hud::MapView::Mini);
      // The map's rectangle was just moved — let the combat frame put its own back,
      // the one the animation computed.
      mapRectStale = true;
      const auto uploadStart = std::chrono::steady_clock::now();
      // One command buffer for the whole screen instead of one per piece.
      renderer->beginUploadBatch();
      for (auto& piece : built) {
        if (reuse < reusable.size()) {
          obf2::gfx::GpuMesh& older = reusable[reuse].mesh;
          if (renderer->refill(older, piece.geometry, resolveTexture)) {
            spawnPieces.push_back(OwnedPiece{older, piece.tint});
            older = obf2::gfx::GpuMesh{};  // it belongs to the new list now
            ++reuse;
            continue;
          }
          ++reuse;  // it does not fit; it is freed below with the rest
        }
        if (auto uploaded = renderer->upload(piece.geometry, resolveTexture)) {
          spawnPieces.push_back(OwnedPiece{*uploaded, piece.tint});
          ++hudUploaded;
        }
      }
      renderer->endUploadBatch();
      for (OwnedPiece& left : reusable) renderer->release(left.mesh);
      hudUploadMs += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() -
                                                              uploadStart)
                         .count();
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
      static_cast<std::size_t>(std::max(levelScene.objectLightmaps.atlasCount(), 0)), nullptr);
  {
    std::vector<bool> wanted(lightmapPages.size(), false);
    for (const obf2::app::Scene::Instance& instance : scene.instances) {
      if (instance.lightmapAtlas >= 0 &&
          static_cast<std::size_t>(instance.lightmapAtlas) < wanted.size()) {
        wanted[static_cast<std::size_t>(instance.lightmapAtlas)] = true;
      }
    }
    int loaded = 0;
    for (std::size_t i = 0; i < lightmapPages.size(); ++i) {
      if (!wanted[i]) continue;
      if (auto decoded = resolveTexture(levelScene.objectLightmaps.atlasPath(static_cast<int>(i)))) {
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
    if (!levelScene.firstChartMap.empty()) {
      if (auto decoded = resolveTexture(levelScene.firstChartMap)) {
        chartSize = static_cast<int>(decoded->width);
      }
    }
    renderer->setTerrainMaterials(materials, chartSize);
  }

  obf2::gfx::GpuMesh skyMesh;
  bool skyReady = false;
  if (levelScene.skyDome) {
    if (auto uploaded = renderer->upload(*levelScene.skyDome, resolveTexture, &error)) {
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

  // What the server's created objects are, by where they stand
  // (obf2/level/placement_index.h). A spawner's object is drawn as the vehicle it
  // issues; a static placement is skipped, because the level draws it already —
  // until now every barrel and fence the server created got a grey box on top of
  // itself, and every jeep, tank and machine gun got nothing but a grey box.
  //
  // The meshes are built the first time a vehicle type is seen and kept in a
  // deque, whose elements stay put: the frame's draw list holds pointers to them
  // while new ones are still being added in the same loop.
  std::optional<obf2::level::PlacementIndex> remotePlacement;
  std::deque<obf2::gfx::GpuMesh> remoteMeshes;
  std::unordered_map<std::string, obf2::gfx::GpuMesh*> remoteMeshByName;
  struct RemoteResolved {
    obf2::Vec3f at;
    obf2::level::PlacedAt placed;
  };
  std::unordered_map<std::uint16_t, RemoteResolved> remoteResolved;
  struct DrawStat {
    int frames[3] = {0, 0, 0};  // GhostPrediction: newest, extrapolated, interpolated
    obf2::Vec3f last;
    bool seen = false;
    float largestStep = 0.0f;
    // A soldier's feet under the terrain by more than 5 cm: as predicted, and as
    // drawn. The measure for "drops through the floor and pops back".
    int predictedUnder = 0;
    int drawnUnder = 0;
  };
  std::map<std::uint16_t, DrawStat> drawStats;
  // Other players' soldiers as their physics carries them
  // (`obf2::server::carryRemoteSoldier`): the body, the game tick it was last
  // carried to, and where it stood before that tick, which the frame blends from.
  struct CarriedSoldier {
    obf2::server::BodyState body;
    obf2::server::SwimState swim;
    std::uint32_t tick = 0;
    obf2::Vec3f from;
    bool ready = false;
  };
  std::unordered_map<std::uint16_t, CarriedSoldier> carriedSoldiers;
  std::set<std::uint16_t> boxReported;

  // Other players' soldiers, drawn as the game assembles them
  // (obf2/game/soldier_model.h): the body's third-person geometry and the kit's
  // piece of the kits' mesh, skinned on the soldier's skeleton. One mesh per
  // soldier template and kit, kept in a deque so the draw list's pointers stay put.
  //
  // The pose comes from the animation systems the templates name: the legs from
  // the soldier's `AnimationSystem3p.inc` and the upper body from the weapon's,
  // walked by the state's speed and direction (`obf2::anim`). What the tick does
  // with the bundles it picked — the times and the four-clip blend — is
  // `obf2::anim::Player` (docs/functions/animation-system.md).
  //
  // One "look" per soldier template and kit holds the bind-pose mesh, the skeleton
  // and the two systems; every soldier on screen then has his own posed copy,
  // because his legs are at his own point in the run.
  struct SoldierLook {
    obf2::mesh::RenderMesh bind;
    // The bind pose on the GPU, uploaded once. It is never written again: the
    // pose reaches the vertex shader as the range's bone matrices, which is
    // where the original deforms a soldier too
    // (`Shaders_client.zip:SkinnedMesh.fx:46`).
    obf2::gfx::GpuMesh gpu;
    obf2::mesh::Skeleton skeleton;
    std::optional<obf2::anim::System> legs;
    obf2::anim::System weapon;
    bool hasWeapon = false;
    bool ready = false;
  };
  std::unordered_map<std::string, SoldierLook> soldierLooks;
  // The clips, by the path the data names them with. A clip that could not be read
  // is remembered as empty so it is not looked for every frame.
  std::unordered_map<std::string, std::optional<obf2::mesh::BoneAnimation>> soldierClips;
  const auto clipAt = [&](const std::string& path) -> const obf2::mesh::BoneAnimation* {
    const std::string key = obf2::normalizeAssetPath(path);
    auto found = soldierClips.find(key);
    if (found == soldierClips.end()) {
      std::optional<obf2::mesh::BoneAnimation> clip;
      if (const auto bytes = files.read(key)) clip = obf2::mesh::loadBoneAnimation(*bytes);
      found = soldierClips.emplace(key, std::move(clip)).first;
    }
    return found->second ? &*found->second : nullptr;
  };

  const auto soldierLookFor = [&](const std::string& soldierName,
                                  const std::string& kitName) -> SoldierLook* {
    const std::string key = soldierName + "|" + kitName;
    if (const auto found = soldierLooks.find(key); found != soldierLooks.end()) {
      return found->second.ready ? &found->second : nullptr;
    }
    SoldierLook look;
    const auto model = obf2::game::soldierModel(registry, soldierName, kitName);
    const auto loadPart = [&](const obf2::game::SoldierPart& part) {
      const std::string path = resolveGeometryPath(files, part.templateFile, part.geometryName);
      return path.empty() ? std::nullopt : loadMesh(files, path, part.geometry, 0, false);
    };
    std::optional<obf2::mesh::RenderMesh> bind = model ? loadPart(model->body) : std::nullopt;
    if (bind && model->kit) {
      if (const auto kit = loadPart(*model->kit)) obf2::mesh::appendSkinned(*bind, *kit);
    }
    // The weapon in his hands. It is a BundledMesh, not a skinned one: its parts
    // are carried by the skeleton's bones 64 and up, one each, and the weapon's
    // own third-person clips move exactly those
    // (`obf2::mesh::bindPartsToBones`). Bound that way it is the same mesh and the
    // same shader as the body.
    if (bind && model->weapon) {
      if (auto weapon = loadPart(*model->weapon)) {
        obf2::mesh::bindPartsToBones(*weapon);
        obf2::mesh::appendSkinned(*bind, *weapon);
      }
    }
    std::optional<obf2::mesh::Skeleton> skeleton;
    if (bind && !model->skeleton3p.empty()) {
      if (const auto bytes = files.read(obf2::normalizeAssetPath(model->skeleton3p))) {
        skeleton = obf2::mesh::loadSkeleton(*bytes);
      }
    }
    if (bind && skeleton) {
      look.bind = std::move(*bind);
      look.skeleton = std::move(*skeleton);
      look.legs = obf2::anim::System::load(files, obf2::normalizeAssetPath(model->animationSystem3p));
      if (!model->weaponAnimationSystem3p.empty()) {
        if (auto weapon = obf2::anim::System::load(
                files, obf2::normalizeAssetPath(model->weaponAnimationSystem3p))) {
          look.weapon = std::move(*weapon);
          look.hasWeapon = true;
        }
      }
      if (const auto uploaded = renderer->upload(look.bind, resolveTexture)) look.gpu = *uploaded;
      look.ready = look.gpu.vertices != nullptr;
      std::printf("  soldier look %s: %zu vertices, kit %s, legs %s, weapon %s, on the gpu %s\n",
                  key.c_str(), look.bind.vertices.size(), model->kit ? "yes" : "no",
                  look.legs ? "yes" : "no", look.hasWeapon ? "yes" : "no",
                  look.gpu.skin != nullptr ? "skinned" : "unskinned");
    } else {
      std::printf("  soldier look %s: not assembled (model %d, mesh %d, skeleton %d)\n",
                  key.c_str(), model ? 1 : 0, bind ? 1 : 0, skeleton ? 1 : 0);
    }
    auto& stored = soldierLooks.emplace(key, std::move(look)).first->second;
    return stored.ready ? &stored : nullptr;
  };

  // One soldier on screen: his own posed copy of the look's mesh and his own place
  // in the clips.
  struct DrawnSoldier {
    SoldierLook* look = nullptr;
    // His own pose: one world matrix per bone of the skeleton. The geometry is
    // the look's and is shared with everyone wearing the same body and kit.
    std::vector<obf2::mesh::Mat4> pose;
    obf2::anim::Player legs;
    obf2::anim::Player weapon;
    std::string key;
  };
  std::map<std::uint16_t, DrawnSoldier> drawnSoldiers;
  // Who `--watch-soldier` is holding on to.
  std::uint16_t watchedSoldier = 0;
  // Whether `--group` has already been asked for — it is a one-shot.
  bool askedForGroup = false;
  if (remote != nullptr && level) {
    const std::string mode = remote->serverGameMode.empty() ? "gpm_cq" : remote->serverGameMode;
    const int size = remote->serverSize > 0 ? remote->serverSize : 16;
    if (const auto gameplay = obf2::level::loadGameplayObjects(files, level->name, mode, size)) {
      remotePlacement.emplace(*gameplay, *level);
      std::printf("  server objects are matched against %s/%d: %zu spawners\n", mode.c_str(), size,
                  gameplay->spawners.size());
    }
  }
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
  for (const obf2::app::Scene::Instance& instance : scene.instances) {
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
    remote->physics = obf2::server::loadPhysicsConstants(files);
    remote->maxSpeed = remote->physics.runSpeed;
    if (level) {
      collisionLibrary = std::make_unique<obf2::server::CollisionLibrary>(files, registry);
      remoteCollision = obf2::server::buildCollisionWorld(*collisionLibrary, level->objects);
      remote->collision = remoteCollision.get();
      remote->collisionLibrary = collisionLibrary.get();
      if (args.collisionNear) {
        const obf2::Vec3f at = *args.collisionNear;
        for (const auto& object : level->objects) {
          const float dx = object.position.x - at.x;
          const float dz = object.position.z - at.z;
          if (dx * dx + dz * dz > 15.0f * 15.0f) continue;
          const auto* root = registry.find(object.templateName);
          std::printf("  near: %s at %.2f %.2f %.2f rot %.1f %.1f %.1f, collisionMesh '%s'\n",
                      object.templateName.c_str(), object.position.x, object.position.y,
                      object.position.z, object.rotation.x, object.rotation.y, object.rotation.z,
                      root ? std::string(root->text("collisionMesh")).c_str() : "(no template)");
          if (root == nullptr) continue;
          for (const auto& child : root->children) {
            const auto* childTemplate = registry.find(child.name);
            std::printf("    child %s at %.2f %.2f %.2f, collisionMesh '%s'\n", child.name.c_str(),
                        child.position.x, child.position.y, child.position.z,
                        childTemplate ? std::string(childTemplate->text("collisionMesh")).c_str()
                                      : "(no template)");
          }
        }
        std::vector<obf2::server::MeshContact> contacts;
        remoteCollision->sphereContacts(at, obf2::Vec3f{}, 1.0f, contacts);
        std::printf("  near: %zu contacts of a 1 m sphere at the point\n", contacts.size());
      }
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
  const auto firstFrameAt = std::chrono::steady_clock::now();
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
    obf2::gfx::Device::InputState frameInput = device->readInput();
    for (const auto& look : args.looks) {
      if (frame < look.frame || frame >= look.frame + look.frames) continue;
      frameInput.mouseDeltaX += look.dx;
      frameInput.mouseDeltaY += look.dy;
    }
    for (const auto& move : args.moves) {
      if (frame < move.frame || frame >= move.frame + move.frames) continue;
      frameInput.moveForward = move.dx;
      frameInput.moveRight = move.dy;
      frameInput.sprint = move.sprint != 0;
    }
    for (const int jumpFrame : args.jumps) {
      if (frame == jumpFrame) frameInput.jump = true;
    }
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
      // `--group <n>`: spawn without the screen. The spawn screen is a debt of its
      // own (it builds empty on some rounds), and without a soldier the client
      // sends the server nothing at all — which is a different measurement from
      // the one we want.
      if (args.spawnGroupGiven && args.spawnGroup != 0 && !askedForGroup &&
          !remote->spawnGroups.empty()) {
        std::printf("  --group %d: asking to spawn as team %d with kit %d\n", args.spawnGroup,
                    args.team, args.kit);
        remote->askSpawnGroup(args.team, args.kit, args.spawnGroup);
        askedForGroup = true;
      }
      // The game tick runs whether we have a soldier or not; with one, the
      // soldier's branch below runs the same ticks.
      if (!remote->soldierPosition()) remote->predict(frameStep);
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

        // The engine's chain: **pixels -> axis -> wire -> angle**. The frame only
        // gathers the input; the look turns and the body moves per tick, from the
        // quantized action that is also sent (RemoteWorld::predict). Turning the
        // camera per frame from the raw mouse, while the server got one frame's
        // movement per 33 ms, made a 360 on our screen about 180 on the server.
        //
        //   axis  = pixels * sensitivity               (--mouse-scale, not measured)
        //   wire  = trunc(axis * 100)                   (0x5bc890 -> 0x5bc5f0)
        //   angle += wire * 0.01 * look factor          (0x5bc6a0, phy-soldier-look-factor-*)
        obf2::net::bf2::PlayerAction out;
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

        remote->queueInput(raw.mouseDeltaX * args.mouseScale, raw.mouseDeltaY * args.mouseScale,
                           out);
        // And we compute our own movement at once: waiting for the server's
        // correction would mean jerks a few tenths of a second apart.
        remote->predict(frameStep);
        // Drawn between ticks, as `BF2FrameInterpolator` does (RemoteWorld).
        yaw = remote->cameraYaw();
        pitch = remote->cameraPitch();
        eye = *remote->drawnSoldierPosition();
        eye.y += 1.7f;
      }

      constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
      const float yawRadians = yaw * kToRadians;
      const float pitchRadians = pitch * kToRadians;
      if (remote != nullptr && frame >= args.traceFrom && frame < args.traceFrom + args.traceFrames) {
        std::printf("  trace frame %d: step %.4f, eye %.4f %.4f %.4f, yaw %.3f pitch %.3f, game tick %u "
                    "fraction %.3f, unanswered %zu\n",
                    frame, frameStep, eye.x, eye.y, eye.z, yaw, pitch, remote->world.gameTick(),
                    remote->tick.pending / obf2::server::kTickTime, remote->sent.size());
      }
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

    // `--watch-soldier`: a debugging camera, ours and not the game's. It stands four
    // metres from the nearest other soldier, at a fixed bearing, and looks at his
    // chest — so a screenshot shows how other players are drawn without anyone
    // having to be found by hand.
    if (args.watchSoldier && remote != nullptr) {
      // The camera holds on to **one** soldier. Taking the nearest every frame
      // made it jump from one to another as they passed each other, which reads
      // as the watched soldier teleporting.
      const auto& objects = remote->world.objects();
      const auto stillThere = [&](std::uint16_t id) {
        const auto found = objects.find(id);
        return found != objects.end() &&
               found->second.netClass == obf2::net::bf2::GhostClass::Soldier &&
               id != remote->ourSoldier;
      };
      if (watchedSoldier != 0 && !stillThere(watchedSoldier)) watchedSoldier = 0;
      if (watchedSoldier == 0) {
        float nearest = 1e9f;
        const obf2::Vec3f from = remoteSoldier ? *remoteSoldier : eye;
        for (const auto& [id, object] : objects) {
          if (!stillThere(id)) continue;
          const auto pose =
              remote->world.poseOf(id, remote->tick.pending / obf2::server::kTickTime);
          const float away = obf2::length((pose ? pose->position : object.position) - from);
          if (away < nearest) {
            nearest = away;
            watchedSoldier = id;
          }
        }
        if (watchedSoldier != 0) {
          std::printf("  watching soldier %u\n", watchedSoldier);
          remote->world.setTraceObject(watchedSoldier);
        }
      }
      if (watchedSoldier != 0) {
        const auto& object = objects.at(watchedSoldier);
        const auto pose =
            remote->world.poseOf(watchedSoldier, remote->tick.pending / obf2::server::kTickTime);
        const obf2::Vec3f at = pose ? pose->position : object.position;
        lookTarget = at;
        eye = at + obf2::Vec3f{2.8f, 0.6f, 2.8f};

        // Whether the soldier stands still because he stands still, or because the
        // server stopped telling us about him: the newest update's age against the
        // ghost clock, and how the pose was made.
        if (frame % 60 == 0) {
          const auto* newest = object.track.newest();
          const float nowMs = static_cast<float>(remote->world.gameTick()) *
                              obf2::net::bf2::kGhostTickMs;
          const float fromUs =
              remoteSoldier ? obf2::length(at - *remoteSoldier) : -1.0f;
          std::printf("  watched %u: updates %d, samples %zu, newest %.0f ms ago, mode %d, "
                      "speed %.2f, %.0f m from our body, at %.2f %.2f %.2f\n",
                      watchedSoldier, object.updates, object.track.count(),
                      newest != nullptr ? nowMs - newest->timeMs : -1.0f,
                      pose ? static_cast<int>(pose->mode) : -1,
                      newest != nullptr && newest->velocity
                          ? obf2::length(*newest->velocity)
                          : -1.0f,
                      fromUs, at.x, at.y, at.z);
        }
      }
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
          // Not a player's soldier: find out what stands there. Resolved once per
          // object and again only if it has moved off the spot it was matched on.
          // By class, not by team: a jeep a player entered has a team and is still
          // a jeep, and a soldier whose `EnterVehicleEvent` came before we joined
          // has none and is still a soldier.
          const bool soldier = object.netClass == obf2::net::bf2::GhostClass::Soldier;
          // Drawn where its track puts it now (ghost_track.h), not where the last
          // update left it.
          const auto pose =
              remote->world.poseOf(id, remote->tick.pending / obf2::server::kTickTime);
          obf2::Vec3f drawAt = pose ? pose->position : object.position;
          // A soldier is not drawn from the predicted point but carried by his
          // physics from it, once per game tick: `predict` seeds the node with the
          // position and the velocity (Linux 0x5dc314) and the node's tick and the
          // ground pass move him (0x6f1de0). Between ticks the frame blends the
          // last two results by how far into the tick it is — the engine's own
          // frame interpolator (`FUN_0045c190`, docs/functions/soldier-physics.md).
          if (soldier && pose && !args.drawPredicted) {
            CarriedSoldier& carried = carriedSoldiers[id];
            const std::uint32_t nowTick = remote->world.gameTick();
            const float pivot = remote->physics.pivotHeight;
            const obf2::Vec3f feet{pose->position.x, pose->position.y - pivot, pose->position.z};
            const auto* newestUpdate = object.track.newest();
            const obf2::Vec3f velocity = newestUpdate && newestUpdate->velocity
                                             ? *newestUpdate->velocity
                                             : obf2::Vec3f{};
            if (!carried.ready || nowTick < carried.tick || nowTick - carried.tick > 8) {
              // First sight, or a gap too long to replay: stand him where the
              // network says.
              carried.body = obf2::server::BodyState{};
              carried.body.position = feet;
              carried.body.onGround = true;
              carried.from = feet;
              carried.tick = nowTick;
              carried.ready = true;
            }
            while (carried.tick < nowTick) {
              carried.from = carried.body.position;
              obf2::server::carryRemoteSoldier(carried.body, carried.swim, feet, velocity,
                                               pose->bodyYaw, remote->physics, remote->terrain,
                                               remote->collision, obf2::server::kTickTime);
              ++carried.tick;
            }
            const float fraction =
                std::clamp(remote->tick.pending / obf2::server::kTickTime, 0.0f, 1.0f);
            const obf2::Vec3f blended =
                carried.from + (carried.body.position - carried.from) * fraction;
            drawAt = obf2::Vec3f{blended.x, blended.y + pivot, blended.z};
            if (remote->terrain != nullptr) {
              auto& stat = drawStats[id];
              if (feet.y < remote->terrain->groundHeightAt(feet) - 0.05f) ++stat.predictedUnder;
              if (blended.y < remote->terrain->groundHeightAt(blended) - 0.05f) ++stat.drawnUnder;
            }
          }
          // The measure of smoothness: how the pose was made, and the largest
          // step between two frames for an object that is moving.
          if (pose && object.track.count() >= 2) {
            auto& stat = drawStats[id];
            ++stat.frames[static_cast<int>(pose->mode)];
            if (stat.seen) {
              stat.largestStep = std::max(stat.largestStep, obf2::length(drawAt - stat.last));
            }
            stat.last = drawAt;
            stat.seen = true;
          }
          if (!soldier && remotePlacement) {
            // What it is, by the spot it was **created** on — a jeep that drove off
            // stays a jeep. The template number is stable per template, so a match
            // is remembered by number too, for objects first seen away from their
            // spawner.
            auto resolved = remoteResolved.find(id);
            if (resolved == remoteResolved.end()) {
              obf2::level::PlacedAt placed = remotePlacement->at(object.createdAt);
              if (placed.kind == obf2::level::PlacedAt::Kind::Unknown) {
                // Not on a placement: the template number names it
                // (obf2/game/template_numbers.h).
                if (const std::string* name = remote->templateNumbers.nameOf(object.templateId);
                    name != nullptr && object.templateId != 0) {
                  placed.kind = obf2::level::PlacedAt::Kind::Spawned;
                  placed.templateName = *name;
                  placed.hasRotation = false;
                }
              }
              resolved =
                  remoteResolved.insert_or_assign(id, RemoteResolved{object.createdAt, placed}).first;
            }
            const obf2::level::PlacedAt& placed = resolved->second.placed;
            if (placed.kind == obf2::level::PlacedAt::Kind::Static) continue;
            if (placed.kind == obf2::level::PlacedAt::Kind::Spawned) {
              auto mesh = remoteMeshByName.find(placed.templateName);
              if (mesh == remoteMeshByName.end()) {
                obf2::gfx::GpuMesh* uploaded = nullptr;
                if (auto built = buildObjectMesh(files, registry, placed.templateName, args, false)) {
                  if (auto gpu = renderer->upload(*built, resolveTexture)) {
                    remoteMeshes.push_back(*gpu);
                    uploaded = &remoteMeshes.back();
                  }
                }
                std::printf("  server object %u is %s%s\n", id, placed.templateName.c_str(),
                            uploaded != nullptr ? "" : " (no geometry)");
                mesh = remoteMeshByName.emplace(placed.templateName, uploaded).first;
              }
              if (mesh->second != nullptr) {
                // The rotation is the spawner's: a simple object's own rotation
                // travels in its record and is not read yet.
                obf2::Mat4 place = obf2::translation(drawAt);
                if (placed.hasRotation) {
                  place = place * obf2::rotationYawPitchRoll(placed.rotation.x, placed.rotation.y,
                                                             placed.rotation.z);
                }
                withOthers.push_back(obf2::gfx::MeshRenderer::DrawItem{mesh->second, place});
                continue;
              }
            }
          }
          // Every object that ends up a box says why, once: a box is a debt.
          if (!soldier && boxReported.insert(id).second) {
            const auto resolved = remoteResolved.find(id);
            const int kind = resolved == remoteResolved.end()
                                 ? -1
                                 : static_cast<int>(resolved->second.placed.kind);
            std::printf("  box: object %u, template %u, class %d, created at %.1f %.1f %.1f, "
                        "now %.1f %.1f %.1f, placement kind %d\n",
                        id, object.templateId, static_cast<int>(object.netClass),
                        object.createdAt.x, object.createdAt.y, object.createdAt.z, drawAt.x,
                        drawAt.y, drawAt.z, kind);
          }
          // Whose soldier this is the server knows: the team came from
          // `CreatePlayerEvent` and the object from `EnterVehicleEvent`. Zero means "not a player".
          const int which =
              object.team == 0 ? 0 : (object.team == remote->world.ownTeam() ? 1 : 2);
          // A soldier's networked position is its pivot, `coll-soldier-pivot-height`
          // above the feet (`FUN_006ed4c0`); the box stands on its base.
          obf2::Vec3f base = drawAt;
          if (soldier) base.y -= remote->physics.pivotHeight;
          // The watched soldier's height as drawn, every frame: the pose the track
          // gave, how it was made, the ground under it and the newest update's
          // vertical velocity — which the extrapolation runs along
          // (`SoldierNetworkable::predict`, Linux 0x5dc278).
          if (soldier && pose && id == watchedSoldier && remote->terrain != nullptr &&
              frame >= args.traceFrom && frame < args.traceFrom + args.traceFrames) {
            const auto* newestUpdate = object.track.newest();
            std::printf("    drawn %u: frame %d, mode %d, predicted y %.2f, drawn y %.2f, "
                        "velocity y %.2f, newest y %.2f\n",
                        id, frame, static_cast<int>(pose->mode), pose->position.y, drawAt.y,
                        newestUpdate && newestUpdate->velocity ? newestUpdate->velocity->y : 0.0f,
                        newestUpdate ? newestUpdate->position.y : 0.0f);
          }
          obf2::Mat4 place = obf2::translation(base);
          if (soldier && pose) {
            place = place * obf2::rotationY(pose->bodyYaw * 3.14159265f / 180.0f);
          }
          if (soldier && pose && frame >= args.traceFrom &&
              frame < args.traceFrom + args.traceFrames) {
            const auto* newest = object.track.newest();
            std::printf("    soldier %u: at %.4f %.4f %.4f yaw %.3f mode %d, samples %zu, newest "
                        "%.4f %.4f %.4f at tick %.1f\n",
                        id, base.x, base.y, base.z, pose->bodyYaw, static_cast<int>(pose->mode),
                        object.track.count(), newest ? newest->position.x : 0.0f,
                        newest ? newest->position.y : 0.0f, newest ? newest->position.z : 0.0f,
                        newest ? newest->timeMs / obf2::net::bf2::kGhostTickMs : 0.0f);
          }
          if (soldier) {
            const std::string* soldierName = remote->templateNumbers.nameOf(object.templateId);
            const std::uint32_t kit = remote->world.kitTemplateOf(id);
            const std::string* kitName = kit != 0 ? remote->templateNumbers.nameOf(kit) : nullptr;
            SoldierLook* look =
                soldierName != nullptr
                    ? soldierLookFor(*soldierName, kitName != nullptr ? *kitName : std::string())
                    : nullptr;
            if (look != nullptr) {
              const std::string key = *soldierName + "|" + (kitName != nullptr ? *kitName : "");
              DrawnSoldier& drawn = drawnSoldiers[id];
              // A soldier who respawned with another kit is another look, so his
              // mesh is built again.
              if (drawn.look != look || drawn.key != key) {
                drawn = DrawnSoldier{};
                drawn.look = look;
                drawn.key = key;
                const auto length = [&](const std::string& path) {
                  const obf2::mesh::BoneAnimation* clip = clipAt(path);
                  return clip != nullptr ? clip->duration() : 0.0f;
                };
                drawn.legs.setLengthOf(length);
                drawn.weapon.setLengthOf(length);
              }

              // The state the conditions read: the speed and the direction the
              // server itself reports for this soldier, turned into his own frame.
              // The yaw a ghost carries is the body's plus the aim's, which is the
              // matrix the movement is built from (docs/functions/soldier-physics.md).
              obf2::anim::State animState;
              const auto* newest = object.track.newest();
              const obf2::Vec3f velocity =
                  newest != nullptr && newest->velocity ? *newest->velocity : obf2::Vec3f{};
              const float planar =
                  std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
              animState.speed = planar;
              if (planar > 0.001f) {
                const float yawRadians = pose->bodyYaw * 3.14159265358979323846f / 180.0f;
                const float forward =
                    (velocity.x * std::sin(yawRadians) + velocity.z * std::cos(yawRadians)) / planar;
                const float side =
                    (velocity.x * std::cos(yawRadians) - velocity.z * std::sin(yawRadians)) / planar;
                animState.direction[0] = side;
                animState.direction[1] = 0.0f;
                animState.direction[2] = forward;
              }
              if (look->legs) drawn.legs.update(*look->legs, animState, frameStep);
              if (look->hasWeapon) drawn.weapon.update(look->weapon, animState, frameStep);

              // The legs first and the weapon over them: a clip touches only its
              // own bones, and the weapon's clips own the upper body
              // (`obf2::mesh::poseSkeleton`).
              std::vector<obf2::mesh::PoseStage> stages;
              const auto addStages = [&](const obf2::anim::Player& player) {
                for (const obf2::anim::PlayingAnimation& playing : player.playing()) {
                  if (playing.weight <= 0.001f) continue;
                  const obf2::mesh::BoneAnimation* clip = clipAt(playing.path);
                  if (clip == nullptr || clip->frameCount == 0) continue;
                  // Fractional, and not wrapped here: the clip is sampled between
                  // two frames and wraps itself, the way the engine does
                  // (`BoneAnimation::sample`). Rounding this down to a whole frame
                  // is what made a soldier at sixty frames a second look like ten.
                  const float frame = playing.time * obf2::mesh::kAnimationFramesPerSecond;
                  stages.push_back(obf2::mesh::PoseStage{clip, frame, playing.weight});
                }
              };
              addStages(drawn.legs);
              addStages(drawn.weapon);
              if (frame >= args.traceFrom && frame < args.traceFrom + args.traceFrames) {
                // The yaw the direction is measured against, next to the heading of
                // the velocity itself. A sprinting soldier cannot strafe
                // (`updateSoldierSpeed`, soldier-physics.md), so above sprint speed
                // the two have to agree — the difference is the error in our yaw.
                const float heading =
                    std::atan2(velocity.x, velocity.z) * 180.0f / 3.14159265358979323846f;
                // What the original's animation would read instead: the node's
                // displacement over the last tick times 30
                // (`PointPhysicsNode::postFrameUpdate`, Linux 0x6ddca0).
                float nodeSpeed = -1.0f;
                if (const auto carriedIt = carriedSoldiers.find(id);
                    carriedIt != carriedSoldiers.end() && carriedIt->second.ready) {
                  const obf2::Vec3f moved =
                      carriedIt->second.body.position - carriedIt->second.from;
                  nodeSpeed = std::sqrt(moved.x * moved.x + moved.z * moved.z) * 30.0f;
                }
                std::printf("    soldier %u node speed %.2f\n", id, nodeSpeed);
                std::printf("    soldier %u anim: speed %.2f, direction %.2f %.2f, yaw %.1f, "
                            "heading %.1f, angles body %.1f aim %.1f 0x8 %.1f pitch %.1f, clips",
                            id, animState.speed, animState.direction[0], animState.direction[2],
                            pose->bodyYaw, heading, object.lastBodyYaw, object.lastAimYaw,
                            object.lastAngle8, object.lastPitch);
                for (const obf2::anim::Player* player : {&drawn.legs, &drawn.weapon}) {
                  for (const obf2::anim::PlayingAnimation& playing : player->playing()) {
                    if (playing.weight <= 0.001f) continue;
                    const std::size_t slash = playing.path.find_last_of("/\\");
                    std::printf(" %s@%.2f×%.2f",
                                slash == std::string::npos ? playing.path.c_str()
                                                           : playing.path.c_str() + slash + 1,
                                playing.time, playing.weight);
                  }
                }
                std::printf("\n");
              }

              if (!stages.empty()) drawn.pose = obf2::mesh::poseSkeleton(look->skeleton, stages);
              if (look->gpu.vertices != nullptr) {
                obf2::gfx::MeshRenderer::DrawItem item{&look->gpu, place};
                // With no clip to stand on he is drawn as the file holds him,
                // which is the bind pose — the same as before any animation
                // system was read.
                if (!drawn.pose.empty()) item.pose = &drawn.pose;
                withOthers.push_back(item);
                continue;
              }
            }
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
        // The map's zoom is its own action: `c_GIMapZoom`, control 0x24, right
        // beside `c_GIMapSize`'s 0x23 in the table the game builds at 0x690222.
        // The profile puts it on N (`Controls.con`,
        // `addKeyToTriggerMapping c_GIMapZoom IDFKeyboard IDKey_N`).
        //
        // **The engine's handler for that action has not been found**, only the
        // action itself. What the console command behind the on-screen button
        // does is a plain property — 0x57a97e reads and writes the map node's
        // +0x6d0 and nothing else — so the step from one zoom to the next lives
        // somewhere we have not looked. Cycling through the three levels is ours,
        // and it is debt, not reversing.
        {
          const std::string_view zoomKey = controls.key("c_GIMapZoom");
          const bool down = !zoomKey.empty() && device->isKeyDown(zoomKey);
          if (down && !zoomKeyWasDown) {
            mapNode.setZoomIndex((mapNode.zoomIndex() + 1) % obf2::hud::kMapZoomLevels);
            std::printf("  map: zoom %d\n", mapNode.zoomIndex());
          }
          zoomKeyWasDown = down;
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
        for (const auto& scheduled : args.scheduledLines) {
          if (frame != scheduled.frame) continue;
          std::printf("  console (frame %d): %s\n", frame, scheduled.line.c_str());
          if (!engine.console().executeLine(scheduled.line)) {
            std::printf("    the command was not recognised\n");
          }
        }
        if (!args.execLines.empty() && spawnVisible && !spawnPieces.empty() &&
            !spawnScreen.markerPoints().empty() && teamKnown && !execDone) {
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
          bool clicked = input.clicked || (args.click && frame == 1);
          float clickX = args.click ? args.mouseX : input.mouseX;
          float clickY = args.click ? args.mouseY : input.mouseY;
          // `--click-at` reaches the spawn screen too: "DONE pressed straight after
          // joining" has to be repeatable without a hand on the mouse.
          for (const auto& scheduled : args.clicks) {
            if (frame != scheduled.frame) continue;
            clicked = true;
            clickX = scheduled.x;
            clickY = scheduled.y;
          }
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
        // A choice changed by the screen's own commands (the module marks itself).
        if (spawnScreen.dirty()) {
          spawnDirty = true;
          spawnScreen.clearDirty();
        }
        // We rebuild the spawn screen only while it is on screen.
        if ((spawnDirty && spawnVisible && rebuildSpawn) || (hudDirty && rebuildIngame)) {
          const auto before = std::chrono::steady_clock::now();
          if (spawnDirty && spawnVisible && rebuildSpawn) {
            spawnDirty = false;
            rebuildSpawn();
          }
          if (hudDirty && rebuildIngame) {
            hudDirty = false;
            rebuildIngame();
          }
          const float spent =
              std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - before)
                  .count();
          ++hudRebuilds;
          hudRebuildMs += spent;
          hudRebuildMax = std::max(hudRebuildMax, spent);
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
    const std::string* shotHere = nullptr;
    for (const auto& [at, file] : args.screenshotAt) {
      if (at == frame) shotHere = &file;
    }
    if (shotHere != nullptr) {
      if (device->submitAndSave(*acquired, shotHere->c_str(), &error)) {
        std::printf("screenshot: %s (frame %d)\n", shotHere->c_str(), frame);
      } else {
        std::fprintf(stderr, "%s\n", error.c_str());
      }
    } else if (lastFrame && !args.screenshot.empty()) {
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
  // --hud-vars: every variable the tree asks for, and whether anyone fills it.
  //
  // The HUD is a stream of nodes hanging off names, and a name nobody writes is
  // a node that never appears — silently. This is the list of those names, so
  // the debt is a list rather than a feeling. It is printed once, after the tree
  // is built, and it says the kind because the kinds live in different maps: a
  // show condition is a bool, a bar's fill a float, a caption and a texture path
  // a string.
  if (args.hudVars) {
    struct Use {
      std::string kind;
      int nodes = 0;
    };
    std::map<std::string, Use> used;
    const auto note = [&](const std::string& name, const char* kind) {
      if (name.empty() || name == "1" || name == "0") return;
      Use& use = used[name];
      if (use.kind.empty()) use.kind = kind;
      else if (use.kind.find(kind) == std::string::npos) use.kind += std::string("+") + kind;
      ++use.nodes;
    };
    for (const auto& node : ingameHud.nodes()) {
      note(node.showVariable, "show");
      for (const auto& test : node.showTests) note(test.variable, "show");
      note(node.alphaVariable, "alpha");
      note(node.textVariable, "string");
      note(node.textureVariable, "texture");
      note(node.valueVariable, "value");
      note(node.rotateVariable, "rotate");
      note(node.positionVariableX, "pos");
      note(node.positionVariableY, "pos");
      for (const auto& rgb : node.rgbVariables) note(rgb, "rgb");
      for (const auto& occupied : node.occupiedPosVariables) note(occupied, "pos");
    }
    int known = 0;
    std::vector<std::pair<int, std::string>> missing;
    for (const auto& [name, use] : used) {
      // Three maps and one lambda: the corner regions' alphas are computed by
      // their own state machine rather than kept in a map, and `variableAlpha`
      // is where a node asks for them.
      const bool have = hudVariables.count(name) != 0 || hudValues.count(name) != 0 ||
                        hudStrings.count(name) != 0 ||
                        (hudContext.variableAlpha && hudContext.variableAlpha(name).has_value());
      if (have) {
        ++known;
        continue;
      }
      missing.emplace_back(use.nodes, name + "  (" + use.kind + ")");
    }
    std::sort(missing.begin(), missing.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::printf("  HUD variables: %zu asked for, %d filled, %zu not\n", used.size(), known,
                missing.size());
    for (const auto& [nodes, text] : missing) {
      std::printf("    %4d nodes  %s\n", nodes, text.c_str());
    }
  }

  for (OwnedPiece& piece : spawnPieces) renderer->release(piece.mesh);
  for (auto& gpuMesh : gpuMeshes) renderer->release(gpuMesh);
  for (auto& gpuMesh : remoteMeshes) renderer->release(gpuMesh);
  {
    // Frames per second over everything after the first frame — the loading is
    // not part of it. The measure for "the picture jerks": a number, not a feeling.
    const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                                       firstFrameAt)
                              .count();
    std::printf("frames drawn: %d in %.1f s (%.1f per second)\n", frame, seconds,
                seconds > 0.0f ? static_cast<float>(frame) / seconds : 0.0f);
    // What baking the interface again costs. The engine walks its node tree every
    // frame and writes the quads into a buffer; we build meshes and put them on
    // the card, so a rebuild is the one place in the frame that can allocate
    // hundreds of times. A number here says whether that is what freezes.
    if (hudRebuilds > 0) {
      std::printf("  hud rebuilds: %d in %.0f ms (%.2f ms each, at most %.2f); "
                  "of that %.0f ms putting %d pieces on the card\n",
                  hudRebuilds, hudRebuildMs, hudRebuildMs / static_cast<float>(hudRebuilds),
                  hudRebuildMax, hudUploadMs, hudUploaded);
    }
  }
  if (remote != nullptr) {
    std::printf("  game tick %u, newest packet tick %u at the end\n", remote->world.gameTick(),
                remote->world.newestPacketTick());
  }
  for (const auto& [id, stat] : drawStats) {
    if (stat.largestStep < 0.01f) continue;  // standing still: nothing to measure
    std::printf("  drawn object %u: interpolated %d, extrapolated %d, newest %d frames, "
                "largest step between frames %.2f m, under the terrain: predicted %d, drawn %d\n",
                id, stat.frames[2], stat.frames[1], stat.frames[0], stat.largestStep,
                stat.predictedUnder, stat.drawnUnder);
  }
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
    RemoteWorld remote(sessionSettings(args), files);
    remote.drawabilityOf = [&](const std::string& name) {
      return checkDrawable(files, remote.registry, name, args);
    };
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
