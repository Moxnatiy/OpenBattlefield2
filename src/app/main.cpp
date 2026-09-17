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
#include "obf2/app/main_menu.h"
#include "obf2/app/render_context.h"
#include "obf2/app/scene_build.h"
#include "obf2/app/world_view.h"
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
#include "obf2/hud/manager.h"
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
  // The screens before a round (`obf2/app/main_menu.h`): the intro, the loading
  // screen and the game's own Flash menu.
  obf2::app::MainMenu menu;
  obf2::app::MainMenu::Options menuOptions;
  menuOptions.screen = args.screen;
  menuOptions.flashSwf = args.flashSwf;
  menuOptions.mouseX = args.mouseX;
  menuOptions.mouseY = args.mouseY;
  menuOptions.click = args.click;
  menuOptions.width = args.width;
  menuOptions.height = args.height;
  for (const auto& click : args.clicks) {
    menuOptions.clicks.push_back(
        obf2::app::MainMenu::Options::Click{click.frame, click.x, click.y});
  }

  if (bootMode) {
    menu.boot(files, engine, args.modDir, scene, menuOptions);
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

  std::string error;
  obf2::app::RenderContext render = obf2::app::createRenderContext(args.width, args.height, &error);
  if (!render.device || !render.renderer) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  obf2::gfx::Device* device = render.device.get();
  obf2::gfx::MeshRenderer* renderer = render.renderer.get();

  if (level) {
    obf2::app::applyLevelLighting(*renderer, *level, args.topDown,
                                  engine.settings().video.textureFilteringQuality);
  }

  // The interface itself (`obf2/hud/manager.h`). It is declared before the
  // texture resolver: the resolver asks it for the combat-area hatch, and it is
  // called from the frame loop, so neither may outlive the other.
  obf2::hud::Manager hudManager;
  obf2::app::TextureCache textureCache(files);
  const auto cacheTexture = [&](const std::string& path) { return textureCache.cache(path); };


  // The menu's movie is opened here, before the texture resolver below: the
  // resolver hands its current frame out under `#flash`.
  if (!args.flashSwf.empty() || bootMode) menu.openMovie(files, menuOptions);

  auto resolveTexture =
      [&](const std::string& mapName) -> std::optional<obf2::texture::Texture> {
    // Names starting with '#' are not files but colours: the game sometimes gives a
    // colour as a number (renderer.waterColor), and we also need fills for screens
    // with no image.
    if (level && mapName == obf2::level::kWaterColorMap) {
      const obf2::Vec3f color = level->terrain.waterColor;
      return obf2::texture::solidColor(color.x, color.y, color.z);
    }
    if (mapName == "#flash") return menu.flashFrame();
    // The spawn screen's red hatch, under `#combatarea` — like `#flash`, a picture
    // we make rather than a file: the game's own `map_CombatArea32.dds` with the
    // alpha cleared inside the level's combat area. The manager builds it.
    if (mapName == obf2::hud::kCombatAreaTexture) return hudManager.combatAreaOverlay();
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
  // The interface itself lives in `obf2::hud::Manager` — the tree from the game's
  // files, the variables, the state machine, the spawn screen and the animation
  // (the engine keeps it in `BF2HudManager` too). What stays here is the graphics
  // side of it: the geometry the manager gives back, put on the card.
  struct OwnedPiece {
    obf2::gfx::GpuMesh mesh;
    obf2::hud::Color tint;
    // Not a mesh of ours but a live node's place: its pieces are substituted here
    // while the frame is assembled.
    const obf2::hud::Node* live = nullptr;
  };
  std::vector<OwnedPiece> spawnPieces;
  // The combat HUD: not baked once and for all but rebuildable — its variables are
  // written by the engine every frame (0x78d0f0), not once at the level's start.
  std::vector<OwnedPiece> ingamePieces;
  // The screens on a key, baked once, and the live nodes, each rebuilt on its own.
  std::vector<std::vector<OwnedPiece>> keyScreenPieces;
  std::vector<std::vector<OwnedPiece>> dynamicPieces;
  // What the interface's rebuilds cost, in milliseconds and in number, split
  // between building the geometry and putting it on the card.
  int hudRebuilds = 0;
  float hudRebuildMs = 0.0f;
  float hudRebuildMax = 0.0f;
  float hudUploadMs = 0.0f;
  int hudUploaded = 0;
  // The look angle lives between frames: the mouse gives only a delta. It is
  // declared here because the console command `openbf2.look` reads it too.
  float yaw = 0.0f;
  // These three are called from the frame loop, so they live at the outer level:
  // a lambda of an inner block would hold references to variables that no longer
  // exist by then (CLAUDE.md, the first of the rakes).
  std::function<void(std::vector<obf2::hud::DrawPiece>&, std::vector<OwnedPiece>&)> putOnCard;
  std::function<void()> rebuildIngame;
  std::function<void()> rebuildSpawn;
  std::chrono::steady_clock::time_point lastAnimationTick = std::chrono::steady_clock::now();

  if (!bootMode) {
    obf2::hud::Manager::Setup hudSetup;
    hudSetup.screenWidth = args.width;
    hudSetup.screenHeight = args.height;
    // We measure the HUD with the **real** window rather than the requested one:
    // the screen may have turned out smaller, and the window would slide with the
    // request.
    SDL_GetWindowSize(device->window(), &hudSetup.screenWidth, &hudSetup.screenHeight);
    hudSetup.kit = args.kit;
    hudSetup.team = args.team;
    hudSetup.levelName = args.levelName;

    obf2::hud::Manager::Hooks hooks;
    // What DONE does. There are two paths, and both are equally "real":
    //
    //   * our own game — a direct request to our server. It behaves like the
    //     engine: the soldier spawns only when a spawn point has been chosen;
    //   * a real BF2 server — three events in a row, NESelectTeam, NESelectKit,
    //     NESelectSpawnGroup, verified against an original server
    //     (docs/functions/network-events.md).
    hooks.requestSpawn = [&](int team, int kit, int group) -> bool {
      if (hostedServer != nullptr && !hostedServer->players().empty()) {
        hostedServer->requestSpawn(hostedServer->players().front().id, team, kit, group);
        return true;
      }
      if (remote != nullptr) {
        // The server has to be told **its** spawn group number, not our control
        // point id: on Dalian the server sends 515..518 while the level's flags are
        // 401..404, and they are not related in any way. All they share is the
        // position, so we pass the position — and the connection picks the number
        // when the time comes to send.
        const obf2::level::ControlPoint* chosen = nullptr;
        for (const auto& point : hudManager.controlPoints()) {
          if (point.id == group) { chosen = &point; break; }
        }
        if (chosen == nullptr) {
          std::printf("  spawn screen: point %d is not in the level's list\n", group);
          return false;
        }
        std::printf("  spawn screen: point %d (%s) at %.0f %.0f\n", group,
                    chosen->nameKey.c_str(), chosen->position.x, chosen->position.z);
        remote->askSpawn(team, kit, chosen->position.x, chosen->position.z, hudManager.mapWorldSize());
        return true;
      }
      std::printf("  spawn screen: there is no server, spawning only closes the screen\n");
      return true;
    };
    hooks.commitSuicide = [&]() {
      if (remote != nullptr) remote->commitSuicide();
    };
    hooks.spawnAtGroup = [&](int team, int kit, int group) {
      if (remote != nullptr) remote->askSpawnGroup(team, kit, group);
    };
    if (args.hudRects) {
      // `--hud-rects`: the same format as in the original's frame dump. The bounds
      // are computed from the geometry itself rather than from the node's
      // rectangle — that way the captions are comparable with the dump, which also
      // outlines the drawn string rather than the node's frame.
      hooks.reportRects = [&](const char* where,
                              const std::vector<obf2::hud::DrawPiece>& pieces) {
        for (const obf2::hud::DrawPiece& piece : pieces) {
          if (piece.node == nullptr || piece.geometry.vertices.empty()) continue;
          float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
          for (const auto& vertex : piece.geometry.vertices) {
            const float px = (vertex.position.x + 1.0f) * 0.5f * hudManager.screen().width;
            const float py = (1.0f - vertex.position.y) * 0.5f * hudManager.screen().height;
            x0 = std::min(x0, px);
            y0 = std::min(y0, py);
            x1 = std::max(x1, px);
            y1 = std::max(y1, py);
          }
          std::printf("RECT %-14s %-30s %7.1f %7.1f %7.1f %7.1f %-46s [%s]\n", where,
                      piece.node->name.c_str(), x0, y0, x1 - x0, y1 - y0, piece.texture.c_str(),
                      piece.node->showVariable.c_str());
        }
      };
    }
    // `openbf2.look <angle>` — turn the view to the given angle in degrees. Ours,
    // for the checks: otherwise the minimap's compass cannot be captured in a
    // screenshot, because the angle comes from the mouse.
    engine.console().bind("openbf2.look", [&](const obf2::con::Command& command) {
      yaw = command.argFloat(0).value_or(0.0f);
      std::printf("  view: angle %.1f\n", static_cast<double>(yaw));
    });

    hudManager.load(files, engine, level ? &*level : nullptr, registry, hudSetup, std::move(hooks));
  }

  if (hudManager.ready()) {
    // The geometry on the card. A rebuild writes the new geometry into the buffers
    // that are already there and only makes new ones when a piece has outgrown its
    // own (`MeshRenderer::refill`): creating a pair of buffers per piece is a trip
    // into the driver each, and that was most of what a rebuild cost.
    putOnCard = [&](std::vector<obf2::hud::DrawPiece>& built, std::vector<OwnedPiece>& into) {
      std::vector<OwnedPiece> reusable = std::move(into);
      std::size_t reuse = 0;
      into.clear();
      const auto uploadStart = std::chrono::steady_clock::now();
      // One command buffer for the whole screen instead of one per piece.
      renderer->beginUploadBatch();
      for (auto& piece : built) {
        if (piece.live) {
          // A live node's marker: there is no mesh here, only a place in the queue.
          into.push_back(OwnedPiece{{}, piece.tint, piece.node});
          continue;
        }
        if (reuse < reusable.size()) {
          obf2::gfx::GpuMesh& older = reusable[reuse].mesh;
          if (older.vertices != nullptr &&
              renderer->refill(older, piece.geometry, resolveTexture)) {
            into.push_back(OwnedPiece{older, piece.tint, nullptr});
            older = obf2::gfx::GpuMesh{};  // it belongs to the new list now
            ++reuse;
            continue;
          }
          ++reuse;  // it does not fit; it is freed below with the rest
        }
        if (auto put = renderer->upload(piece.geometry, resolveTexture)) {
          into.push_back(OwnedPiece{*put, piece.tint, nullptr});
          ++hudUploaded;
        }
      }
      renderer->endUploadBatch();
      // Only our own meshes are released. A rebuild that handed back somebody
      // else's left the frame loop drawing freed handles (CLAUDE.md, the rakes).
      for (OwnedPiece& left : reusable) renderer->release(left.mesh);
      hudUploadMs += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() -
                                                              uploadStart)
                         .count();
    };
    rebuildIngame = [&]() {
      auto built = hudManager.buildIngame();
      putOnCard(built, ingamePieces);
    };
    rebuildSpawn = [&]() {
      auto built = hudManager.buildSpawn();
      putOnCard(built, spawnPieces);
    };
    rebuildIngame();
    rebuildSpawn();
    keyScreenPieces.resize(hudManager.keyScreens().size());
    for (std::size_t i = 0; i < hudManager.keyScreens().size(); ++i) {
      auto pieces = hudManager.keyScreens()[i].pieces;
      putOnCard(pieces, keyScreenPieces[i]);
    }
    dynamicPieces.resize(hudManager.dynamicNodes().size());

    // --hud-screen list: what exactly landed on screen. Without it one has to guess
    // which node slid.
    if (args.hudScreenName == "list") {
      for (const auto& piece : hudManager.buildIngame()) {
        if (piece.node == nullptr) continue;
        std::printf("    %-10s %-28s %-22s %6.0f %6.0f %5.0f %5.0f  %s\n",
                    std::string(obf2::hud::nodeTypeName(piece.node->type)).c_str(),
                    piece.node->name.c_str(), piece.node->group.c_str(), piece.node->x,
                    piece.node->y, piece.node->width, piece.node->height, piece.texture.c_str());
      }
    }
  }

  const auto uploadStarted = std::chrono::steady_clock::now();
  obf2::app::UploadedScene uploaded = obf2::app::uploadScene(
      *renderer, textureCache, resolveTexture, scene, levelScene, level ? &*level : nullptr, files);
  std::vector<obf2::gfx::GpuMesh>& gpuMeshes = uploaded.meshes;
  const std::vector<bool>& uploadedOk = uploaded.ok;
  const long long triangles = uploaded.triangles;
  const std::vector<SDL_GPUTexture*>& lightmapPages = uploaded.lightmapPages;
  obf2::gfx::GpuMesh& skyMesh = uploaded.sky;
  const bool skyReady = uploaded.skyReady;

  // Other players and the server's own objects on screen (`obf2/app/world_view.h`).
  obf2::app::WorldView worldView;
  // Whether `--group` has already been asked for — it is a one-shot.
  bool askedForGroup = false;
  if (remote != nullptr) {
    obf2::app::WorldView::Options viewOptions;
    viewOptions.drawPredicted = args.drawPredicted;
    viewOptions.showOwnBox = args.showOwnBox;
    viewOptions.geometryIndex = args.geometryIndex;
    viewOptions.lodIndex = args.lodIndex;
    viewOptions.traceFrom = args.traceFrom;
    viewOptions.traceFrames = args.traceFrames;
    worldView.init(files, registry, *renderer, resolveTexture, *remote, level ? &*level : nullptr,
                   viewOptions);
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
              gpuMeshes.size(), triangles, items.size(), drawnTriangles, textureCache.loaded(),
              textureCache.missing(), secondsSince(uploadStarted));

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
      const std::uint16_t watched =
          worldView.watchSoldier(remoteSoldier ? *remoteSoldier : eye);
      const auto& objects = remote->world.objects();
      if (watched != 0 && objects.count(watched) != 0) {
        const auto& object = objects.at(watched);
        const auto pose =
            remote->world.poseOf(watched, remote->tick.pending / obf2::server::kTickTime);
        const obf2::Vec3f at = pose ? pose->position : object.position;
        lookTarget = at;
        eye = at + obf2::Vec3f{2.8f, 0.6f, 2.8f};

        // Whether the soldier stands still because he stands still, or because the
        // server stopped telling us about him: the newest update's age against the
        // ghost clock, and how the pose was made.
        if (frame % 60 == 0) {
          const auto* newest = object.track.newest();
          const float nowMs =
              static_cast<float>(remote->world.gameTick()) * obf2::net::bf2::kGhostTickMs;
          const float fromUs = remoteSoldier ? obf2::length(at - *remoteSoldier) : -1.0f;
          std::printf("  watched %u: updates %d, samples %zu, newest %.0f ms ago, mode %d, "
                      "speed %.2f, %.0f m from our body, at %.2f %.2f %.2f\n",
                      watched, object.updates, object.track.count(),
                      newest != nullptr ? nowMs - newest->timeMs : -1.0f,
                      pose ? static_cast<int>(pose->mode) : -1,
                      newest != nullptr && newest->velocity ? obf2::length(*newest->velocity)
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
      const auto overlay =
          menu.frame(engine, *device, *renderer, resolveTexture, frameInput, frame, menuOptions,
                     gpuMeshes, uploadedOk);
      renderer->renderOverlay(*acquired, overlay, obf2::gfx::Color{0.0f, 0.0f, 0.0f, 1.0f});
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

      if (remote != nullptr && !remote->world.objects().empty()) {
        if (withOthers.empty()) withOthers = items;
        worldView.collect(withOthers, frame, frameStep);
        toDraw = &withOthers;
      }
      // The specular is the one thing that depends on where the eye is.
      renderer->setCameraPosition(eye);
      renderer->renderScene(*acquired, *toDraw, projection * view,
                            obf2::gfx::Color{0.42f, 0.55f, 0.68f, 1.0f});

      // The HUD goes as a second pass over the ready frame — without clearing the
      // target. `--no-hud` skips it: when a frame is being compared against the
      // original's, the interface is in the way of everything being compared.
      if (!args.noHud && hudManager.ready()) {
        // Live values: the tickets come straight from the server, because in a
        // single-player game it is right here. To a client they will arrive in a
        // separate packet once the round's state exists on the network.
        if (hostedServer != nullptr) {
          const int own = 1, enemy = 2;
          hudManager.strings()["FriendlyTicketsString"] =
              std::to_string(hostedServer->tickets(own));
          hudManager.strings()["EnemyTicketsString"] = std::to_string(hostedServer->tickets(enemy));

          // The flag strips under the minimap: how many points each team holds.
          const auto& points = hostedServer->controlPoints();
          int ours = 0, theirs = 0;
          for (const auto& point : points) {
            if (point.team == own) ++ours;
            else if (point.team == enemy) ++theirs;
          }
          const float total = points.empty() ? 1.0f : static_cast<float>(points.size());
          hudManager.values()["FriendlyCPs"] = static_cast<float>(ours) / total;
          hudManager.values()["EnemyCPs"] = static_cast<float>(theirs) / total;

          // The caption in the middle of the screen while the round waits for
          // players. Its node is `GameInfo DisconnectMessage 0 200 800 40` with the
          // variables DisconnectMessage / DisconnectMessageActive; the text itself
          // the game assembles in code (BF2.exe, 0x466f75): it takes the key
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
            hudManager.strings()["DisconnectMessage"] = text;
            hudManager.variables()["DisconnectMessageActive"] = true;
          } else {
            hudManager.variables()["DisconnectMessageActive"] = false;
          }
        }

        std::vector<obf2::gfx::MeshRenderer::DrawItem> hudItems;

        // The HUD's state. In the game this is not a set of keys but a 32-entry
        // machine (docs/functions/hud-states.md): state 1 is the spawn screen, and
        // it stands by itself until the player has spawned; state 9 is the
        // scoreboard, and that one really is on a key.
        //
        // "There is a player" is not "the client is connected" but "the server gave
        // him a soldier". That is what governs the combat HUD in the engine
        // (0x78d0f0 takes the current player, and without one it clears the set).
        const bool spawned = hostedServer != nullptr ? localSoldierId != 0
                                                     : hudManager.spawnScreen().requested();
        {
          const std::string_view mapKey = hudManager.controls().key("c_GIMapSize");
          hudManager.mapKey(!mapKey.empty() && device->isKeyDown(mapKey));
          // The profile puts the zoom on N (`Controls.con`,
          // `addKeyToTriggerMapping c_GIMapZoom IDFKeyboard IDKey_N`).
          const std::string_view zoomKey = hudManager.controls().key("c_GIMapZoom");
          hudManager.zoomKey(!zoomKey.empty() && device->isKeyDown(zoomKey));
        }
        // The map key toggles state 0 <-> 2. In the state table state 2
        // (BF2.exe 0x787008) turns on `MapShow` alone: the rest of the HUD
        // disappears on the big map.
        const int hudState = spawned ? (hudManager.bigMap() ? 2 : 0) : 1;
        const bool spawnVisible = hudState == 1 || args.hudScreenName == "SpawnMenu";

        // The mouse: in combat the window captures it (otherwise the cursor runs
        // into the screen's edge and looking simply stops), while on the spawn
        // screen it is free, because there it presses buttons.
        const bool wantRelativeMouse = spawned && !spawnVisible;
        if (wantRelativeMouse != relativeMouse) {
          relativeMouse = wantRelativeMouse;
          device->setRelativeMouse(relativeMouse);
        }
        // The team is assigned by the server rather than by our choice: in the
        // captured traffic the original client does not even send `NESelectTeam` —
        // it accepts the one the server gave in `CreatePlayerEvent`. Resetting the
        // chosen point here is no detail: the circles belong to our own team's
        // flags, and without the reset we would ask to spawn at another team's,
        // which the server does not do.
        if (remote != nullptr && remote->world.ownTeam() > 0 &&
            hudManager.spawnScreen().choice().team != remote->world.ownTeam()) {
          hudManager.spawnScreen().setTeamFromServer(remote->world.ownTeam());
          hudManager.markSpawnDirty();
          std::printf("  spawn screen: the server gave team %d\n",
                      hudManager.spawnScreen().choice().team);
        }
        hudManager.applyState(hudState);
        // `MapFullSize` is not "we are on the spawn screen" but "the map's size has
        // reached the big one" — that is how 0x77d3f8 derives it.
        hudManager.updateVariables(spawned, hudManager.map().fullSize());

        // `--exec` goes the same path as a button click: a button runs a console
        // command, and we run a console command. We wait for the assembled screen,
        // for the spawn circles (they arrive as events only after the level has
        // loaded) and for the screen to be assembled for **our** team.
        const bool teamKnown =
            remote == nullptr || (remote->world.ownTeam() > 0 &&
                                  hudManager.spawnScreen().choice().team ==
                                      remote->world.ownTeam());
        for (const auto& scheduled : args.scheduledLines) {
          if (frame != scheduled.frame) continue;
          std::printf("  console (frame %d): %s\n", frame, scheduled.line.c_str());
          if (!engine.console().executeLine(scheduled.line)) {
            std::printf("    the command was not recognised\n");
          }
        }
        if (!args.execLines.empty() && spawnVisible && !spawnPieces.empty() &&
            !hudManager.spawnScreen().markerPoints().empty() && teamKnown && !execDone) {
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
          if (clicked) hudManager.clickSpawn(clickX, clickY, engine.console());
        }

        // While at least one node is travelling or fading, the screen has to be
        // rebaked every frame: our geometry lives in meshes on the graphics card.
        {
          const auto now = std::chrono::steady_clock::now();
          const float dt = std::chrono::duration<float>(now - lastAnimationTick).count();
          lastAnimationTick = now;
          hudManager.animate(dt, yaw, eye);
        }

        // We rebuild the spawn screen only while it is on screen.
        if ((hudManager.spawnDirty() && spawnVisible && rebuildSpawn) ||
            (hudManager.dirty() && rebuildIngame)) {
          const auto before = std::chrono::steady_clock::now();
          if (hudManager.spawnDirty() && spawnVisible && rebuildSpawn) {
            hudManager.clearSpawnDirty();
            rebuildSpawn();
          }
          if (hudManager.dirty() && rebuildIngame) {
            hudManager.clearDirty();
            rebuildIngame();
          }
          const float spent =
              std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - before)
                  .count();
          ++hudRebuilds;
          hudRebuildMs += spent;
          hudRebuildMax = std::max(hudRebuildMax, spent);
        }

        // The live nodes are rebuilt **before** the frame is assembled: their pieces
        // are substituted in place of the markers in `ingamePieces`.
        for (std::size_t i = 0; i < hudManager.dynamicNodes().size(); ++i) {
          obf2::hud::Manager::DynamicNode& live = hudManager.dynamicNodes()[i];
          if (!hudManager.dynamicChanged(live)) continue;
          for (OwnedPiece& piece : dynamicPieces[i]) renderer->release(piece.mesh);
          dynamicPieces[i].clear();
          for (auto& built : hudManager.buildDynamic(live)) {
            if (auto put = renderer->upload(built.geometry, resolveTexture)) {
              dynamicPieces[i].push_back(OwnedPiece{*put, built.tint, nullptr});
            }
          }
        }

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
          // The live node's place: we substitute its pieces exactly here rather than
          // at the end — otherwise the map would land over its own frame.
          for (std::size_t i = 0; i < hudManager.dynamicNodes().size(); ++i) {
            if (hudManager.dynamicNodes()[i].node != piece.live) continue;
            for (const OwnedPiece& own : dynamicPieces[i]) pushPiece(own);
            break;
          }
        }
        // The scoreboard, the radio and the map menu go over everything while a key
        // is held. The order is the same as in the game: the combat HUD first.
        for (std::size_t i = 0; i < hudManager.keyScreens().size(); ++i) {
          const obf2::hud::Manager::KeyScreen& screen = hudManager.keyScreens()[i];
          const bool forced = screen.group == args.hudScreenName;
          bool visible = forced;
          if (!visible && screen.heldByKey) {
            const std::string_view key = hudManager.controls().key(screen.action);
            visible = !key.empty() && device->isKeyDown(key);
          } else if (!visible) {
            visible = screen.state == hudState;
          }
          if (!visible) continue;
          for (const OwnedPiece& piece : keyScreenPieces[i]) pushPiece(piece);
        }
        if (spawnVisible) {
          for (const OwnedPiece& piece : spawnPieces) pushPiece(piece);
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

    if (menu.quit()) break;
    // A level from the menu: we end the menu's session and pass the choice upwards.
    if (!menu.requestedLevel().empty()) break;

    ++frame;
    if (args.frames > 0 && frame >= args.frames) break;
  }

  for (auto& pieces : dynamicPieces) {
    for (OwnedPiece& piece : pieces) renderer->release(piece.mesh);
  }
  for (auto& pieces : keyScreenPieces) {
    for (OwnedPiece& piece : pieces) renderer->release(piece.mesh);
  }
  for (OwnedPiece& piece : ingamePieces) renderer->release(piece.mesh);
  // --hud-vars: every variable the tree asks for, and whether anyone fills it.
  if (args.hudVars && hudManager.ready()) hudManager.reportVariables();

  for (OwnedPiece& piece : spawnPieces) renderer->release(piece.mesh);
  for (auto& gpuMesh : gpuMeshes) renderer->release(gpuMesh);
  worldView.release(*renderer);
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
  worldView.report();
  menu.release(*renderer);
  if (nextLevel != nullptr) *nextLevel = menu.requestedLevel();
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
