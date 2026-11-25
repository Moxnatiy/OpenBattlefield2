// One session of the game: the scene, the window, the HUD and the frame loop,
// for the menu, a level, a hosted round or a live server (`--connect`).
// Moved here from main.cpp unchanged; main.cpp keeps the arguments and the
// choice of mode (CLAUDE.md, rule 11).
#include "obf2/app/game.h"


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

#include "obf2/app/calibrate.h"
#include "obf2/app/camera.h"
#include "obf2/app/collision_probe.h"
#include "obf2/app/command_line.h"
#include "obf2/app/scripted_input.h"
#include "obf2/app/hosted_game.h"
#include "obf2/app/loading_screen.h"
#include "obf2/app/main_menu.h"
#include "obf2/app/combat_feedback.h"
#include "obf2/app/environment_audio.h"
#include "obf2/app/field_ui.h"
#include "obf2/app/map_contacts.h"
#include "obf2/app/map_hit_sound.h"
#include "obf2/app/match_voice.h"
#include "obf2/app/own_impacts.h"
#include "obf2/app/menu_music.h"
#include "obf2/app/model_view.h"
#include "obf2/app/render_context.h"
#include "obf2/app/scene_build.h"
#include "obf2/app/scoreboard_players.h"
#include "obf2/app/ambient_effects.h"
#include "obf2/app/live_hud.h"
#include "obf2/app/squad_roster.h"
#include "obf2/app/spawn_circles.h"
#include "obf2/app/ammo_hud.h"
#include "obf2/app/player_hud.h"
#include "obf2/app/crosshair.h"
#include "obf2/app/first_person.h"
#include "obf2/app/world_view.h"
#include "obf2/core/math.h"
#include "obf2/core/parallel.h"
#include "obf2/core/path.h"
#include "obf2/core/platform.h"
#include "obf2/engine/engine.h"
#include "obf2/font/font_file.h"
#include "obf2/font/text.h"
#include "obf2/game/lod_switch.h"
#include "obf2/game/object_mesh.h"
#include "obf2/hud/bottom_left.h"
#include "obf2/hud/ingame.h"
#include "obf2/hud/combat_area.h"
#include "obf2/hud/kit_list.h"
#include "obf2/hud/manager.h"
#include "obf2/hud/map_node.h"
#include "obf2/meme/graph.h"
#include "obf2/hud/render.h"
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

namespace obf2::app {
namespace {

using obf2::session::RemoteWorld;
using obf2::session::buildRegistry;

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

// `shared`: the window and renderer of the whole run — the menu's session
// makes them, the game's session after it takes the same ones, as the original
// keeps one window from the intro to the round. Each session releases what it
// uploaded before it ends.
int runSession(const Args& args, obf2::FileSystem& files, Args* next, RemoteWorld* remote,
               obf2::app::RenderContext& shared) {
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

  obf2::engine::Engine engine;
  // The player's documents folder: `--home`, or `~/Documents/Battlefield 2`.
  std::filesystem::path homeDir = args.homeDir;
  if (homeDir.empty()) {
    if (const char* home = std::getenv("HOME")) {
      const std::filesystem::path documents = std::filesystem::path(home) / "Documents" / "Battlefield 2";
      std::error_code ec;
      if (std::filesystem::is_directory(documents, ec)) homeDir = documents;
    }
  }

  // A level started from the menu loads behind the loading screen
  // (`obf2/app/loading_screen.h`): the window and the engine's settings and
  // dictionary are made first, and the screen is drawn between the phases.
  obf2::app::RenderContext& render = shared;
  obf2::app::LoadingScreen loadingScreen;
  if (args.loadingScreen && !args.levelName.empty()) {
    engine.boot(files, args.modDir, homeDir);
    engine.skipAllMovies();
    std::string renderError;
    if (!render.device || !render.renderer) {
      render = obf2::app::createRenderContext(args.width, args.height, &renderError);
    }
    if (render.device && render.renderer) {
      loadingScreen.open(files, engine, args.levelName, args.loadingMode, args.loadingSize);
    }
  }
  // One phase of our load done and the next begun. The parts are our loader's,
  // each weighted 1 — not the engine's parts (engine/load_stat.h).
  const auto loadPhase = [&](const char* name) {
    if (!loadingScreen.isOpen()) return;
    loadingScreen.stat().step();
    loadingScreen.stat().nextPart(name, 1, 1.0f);
    loadingScreen.frame(*render.device, *render.renderer);
  };

  if (!args.levelName.empty()) {
    loadPhase("Level");
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
    loadPhase("Terrain");
    levelScene = obf2::app::buildLevelScene(files, *level, scene);

    if (remote != nullptr) remote->keepAlive();
    std::printf("  loading: the level's own files took %.2f s\n", secondsSince(started));
    loadPhase("Objects");
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
      loadPhase("Server");
      collisionLibrary = std::make_unique<obf2::server::CollisionLibrary>(files, registry);
      obf2::app::HostedGame hosted = obf2::app::startHostedGame(files, *level, *collisionLibrary);
      hostedServer = std::move(hosted.server);
      hostedClient = std::move(hosted.client);
      localSoldierId = hosted.localSoldierId;
      placement = std::move(hosted.placement);
    }

    obf2::app::SceneOptions sceneOptions;
    sceneOptions.geometryIndex = args.geometryIndex;
    sceneOptions.lodIndex = args.explicitLod
                                ? args.lodIndex
                                : obf2::app::skippedLods(engine.settings().video.geometryQuality);
    sceneOptions.clampLod = !args.explicitLod;
    sceneOptions.noLightmaps = args.noLightmaps;
    loadPhase("Placement");
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

  const bool bootMode = args.levelName.empty() && args.objectName.empty() && args.meshPath.empty();
  // The screens before a round (`obf2/app/main_menu.h`): the intro, the loading
  // screen and the game's own Flash menu.
  obf2::app::MainMenu menu;
  obf2::app::MainMenu::Options menuOptions;
  menuOptions.screen = args.screen;
  menuOptions.homeDir = homeDir;
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
    // The model viewer: one mesh or one object, posed by its clips
    // (`obf2/app/model_view.h`).
    obf2::app::ModelRequest model;
    model.meshPath = args.meshPath;
    model.objectName = args.objectName;
    model.geometryIndex = args.geometryIndex;
    model.lodIndex = args.lodIndex;
    model.animationPaths = args.animationPaths;
    model.skeletonPath = args.skeletonPath;
    model.frame = args.frame;
    if (!args.objectName.empty()) {
      registry = buildRegistry(files);
      std::printf("registry: %zu templates\n", registry.size());
    }
    if (!obf2::app::addModelToScene(files, registry, model, scene)) return 1;
  }

  // --- GPU --------------------------------------------------------------

  std::string error;
  if (!render.device || !render.renderer) {
    render = obf2::app::createRenderContext(args.width, args.height, &error);
  }
  if (!render.device || !render.renderer) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  obf2::gfx::Device* device = render.device.get();
  obf2::gfx::MeshRenderer* renderer = render.renderer.get();
  // Road statics are drawn from medium terrain quality up (0x100fe618).
  renderer->setTerrainQuality(engine.settings().video.terrainQuality);

  if (level) {
    obf2::app::applyLevelLighting(*renderer, *level, args.topDown,
                                  engine.settings().video.textureFilteringQuality);
  }

  // The interface itself (`obf2/hud/manager.h`). It is declared before the
  // texture resolver: the resolver asks it for the combat-area hatch, and it is
  // called from the frame loop, so neither may outlive the other.
  obf2::hud::Manager hudManager;
  // The round's voice (match_voice.h). The frame loop updates it, so it lives at
  // the session's level.
  obf2::app::MatchVoice matchVoice;
  // The radio messages' voice, one at a time (menu_music.h plays WAV and Ogg).
  obf2::app::MenuMusic radioAudio;
  obf2::app::MenuMusic mapHitAudio;
  // The communication roses (field_ui.h): the open rose, its pieces on the card
  // and the revision they were built from. The console commands the rose tables
  // run aim where the last frame looked.
  obf2::app::FieldUi fieldUi;
  int roseShown = -1;
  obf2::Vec3f lastCommEye{}, lastCommDirection{0.0f, 0.0f, 1.0f};
  // `openbf2.roseHold <frames> <T|Q> <dx> <dy>` (ours, for the checks): the rose
  // key held for a number of frames, the pointer moved once when it opens.
  struct RoseHold {
    int frames = 0;
    bool tactical = false;
    float dx = 0.0f, dy = 0.0f;
    bool moved = false;
  } roseHold;
  obf2::app::TextureCache textureCache(files);
  const auto cacheTexture = [&](const std::string& path) { return textureCache.cache(path); };


  // The menu's movie is opened here, before the texture resolver below: the
  // resolver hands its current frame out under `#flash`.
  if (!args.flashSwf.empty() || bootMode) menu.openMovie(files, engine, menuOptions);

  auto resolveTexture =
      [&](const std::string& mapName) -> std::optional<obf2::texture::Texture> {
    // Names starting with '#' are not files: the menu's frames, and fills for
    // screens with no image.
    if (mapName == "#flash") return menu.flashFrame();
    if (mapName == "#movie") return menu.movieFrame();
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
  // The open communication rose (field_ui.h), rebuilt when it changes.
  std::vector<OwnedPiece> rosePieces;
  // What the interface's rebuilds cost, in milliseconds and in number, split
  // between building the geometry and putting it on the card.
  int shownSpawnGroups = -1;  // the session's spawn group revision the HUD has
  // Our soldier's crosshair, fed once per action we play (obf2/app/crosshair.h),
  // the newest action it has seen, and what it last put into the HUD — the HUD is
  // rebuilt only when that changes.
  obf2::app::Crosshair crosshair;
  // The item our own number key last picked, and the soldier it was for.
  std::optional<int> ownItem;
  std::uint16_t ownItemSoldier = 0;
  std::optional<std::uint32_t> crosshairAction;
  std::map<std::string, float> crosshairShown;
  // The weapon's firing per action tick, from the first-person weapon to the
  // crosshair's deviation (bit 0 a round fired, bit 1 the fire state).
  std::array<std::uint8_t, 256> weaponFireAt{};
  std::array<std::uint32_t, 256> weaponFireTick{};
  std::string crosshairKey;
  // The weapon's ammunition on the HUD (obf2/app/ammo_hud.h) and the last
  // `PrimaryReloadTime`, which the next frame's block reads (0x7a8b2f).
  obf2::app::AmmoHud ammoShown;
  obf2::app::AmmoHud playerShown;
  float ammoReloadProgress = 1.0f;
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
    if (remote != nullptr && remote->roundGameplay) hudSetup.gameplay = &*remote->roundGameplay;

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
      if (remote != nullptr && hudManager.circlesAreServerGroups()) {
        // The circles are the server's own groups (spawn_circles.h), so the
        // number is the group's, which is what `NESelectSpawnGroup` takes.
        std::printf("  spawn screen: spawn group %d\n", group);
        remote->askSpawnGroup(team, kit, group);
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
    hooks.selectKit = [&](int kit) {
      if (remote != nullptr) remote->selectKit(kit);
    };
    // The squad tab's requests (`hud::SquadList::Action`), sent as the squad
    // manager sends them (squad_roster.h).
    hooks.squadAction = [&](const obf2::hud::SquadList::Action& action) {
      if (remote == nullptr) {
        std::printf("  squad tab: no server — %d ignored\n", static_cast<int>(action.kind));
        return;
      }
      const bool sent = obf2::app::sendSquadListAction(*remote, action);
      std::printf("  squad menu: action %d squad %d %s\n", static_cast<int>(action.kind), action.squad,
                  sent ? "sent" : "refused");
    };
    hooks.spawnAtGroup = [&](int team, int kit, int group) {
      if (remote != nullptr) remote->askSpawnGroup(team, kit, group);
    };
    // The scoreboard's manage page: column 4 mutes (`VoipPlayerMuteEvent`,
    // docs/functions/voip-mute.md), 7 and 8 vote a kick (`VoteEvent`).
    hooks.toggleMute = [&](int player) {
      if (remote == nullptr) return;
      const bool now = remote->toggleMute(static_cast<std::uint32_t>(player));
      std::printf("  scoreboard: player %d %s\n", player, now ? "muted" : "unmuted");
    };
    hooks.kickVote = [&](int player, int column) {
      if (remote == nullptr) return;
      const std::string answer = remote->scoreboardKick(static_cast<std::uint32_t>(player), column);
      std::printf("  scoreboard: kick vote on player %d column %d: %s\n", player, column,
                  answer.c_str());
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
          // The tint last, as the dump's vertex carries it (docs/research/03-frame-dump.md).
          std::printf("RECT %-14s %-30s %7.1f %7.1f %7.1f %7.1f %-46s [%s] rgba %.3f %.3f %.3f %.3f\n",
                      where, piece.node->name.c_str(), x0, y0, x1 - x0, y1 - y0,
                      piece.texture.c_str(), piece.node->showVariable.c_str(),
                      static_cast<double>(piece.tint.r), static_cast<double>(piece.tint.g),
                      static_cast<double>(piece.tint.b), static_cast<double>(piece.tint.a));
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
    // The rose tables' own console commands (HUD/HudSetup/Communication/*.con),
    // `SquadLeader.sendOrder 5 0` and the rest, routed onto the session's
    // `communicate` (field_ui.h, `roseCommand`) and aimed where the last frame
    // looked.
    for (const char* name :
         {"SquadLeader.sendOrder", "SquadLeader.sendRequest", "SquadLeader.sendRequestForOrder",
          "Radio.sendSpottedMessage", "Radio.callForAmmo", "Radio.callForMedic",
          "Radio.callForRepair", "game.radioMessage", "Commander.uav", "Commander.artillery",
          "Commander.supplies", "Commander.vehicleDrop"}) {
      engine.console().bind(name, [&, name = std::string(name)](const obf2::con::Command& command) {
        if (!remote) return;
        std::string retail = name;
        for (const auto& argument : command.args) retail += " " + argument;
        const auto line = obf2::app::roseCommand(retail);
        if (!line) {
          std::printf("  rose: not routed yet: %s\n", retail.c_str());
          return;
        }
        const auto status = remote->communicate(*line, lastCommEye, lastCommDirection);
        std::printf("  rose: %s -> %s: %s\n", retail.c_str(), line->c_str(), status.c_str());
      });
    }
    engine.console().bind("openbf2.roseHold", [&](const obf2::con::Command& command) {
      roseHold = {};
      roseHold.frames = command.argInt(0).value_or(60);
      roseHold.tactical = command.args.size() > 1 && (command.args[1] == "T" || command.args[1] == "t");
      roseHold.dx = command.argFloat(2).value_or(0.0f);
      roseHold.dy = command.argFloat(3).value_or(0.0f);
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

  loadPhase("Upload");
  const auto uploadStarted = std::chrono::steady_clock::now();
  obf2::app::UploadedScene uploaded = obf2::app::uploadScene(
      *renderer, textureCache, resolveTexture, scene, levelScene, level ? &*level : nullptr, files);
  std::vector<obf2::gfx::GpuMesh>& gpuMeshes = uploaded.meshes;
  const std::vector<bool>& uploadedOk = uploaded.ok;
  const long long triangles = uploaded.triangles;
  const std::vector<SDL_GPUTexture*>& lightmapPages = uploaded.lightmapPages;
  obf2::gfx::GpuMesh& skyMesh = uploaded.sky;
  const bool skyReady = uploaded.skyReady;

  // The level's own effects: the `ObjectTemplate.create Effect…` objects its
  // placement names, emitting where they stand (ambient_effects.h).
  obf2::app::AmbientEffects ambientEffects;
  if (level) {
    ambientEffects.load(files, registry, *renderer, resolveTexture, level->objects, level->lighting);
  }

  // Other players and the server's own objects on screen (`obf2/app/world_view.h`).
  obf2::app::WorldView worldView;
  // Whether `--group` has already been asked for — it is a one-shot.
  bool askedForGroup = false;
  if (remote != nullptr) {
    obf2::app::WorldView::Options viewOptions;
    viewOptions.drawPredicted = args.drawPredicted;
    viewOptions.showOwnBox = args.showOwnBox;
    viewOptions.geometryIndex = args.geometryIndex;
    viewOptions.lodIndex = args.explicitLod
                               ? args.lodIndex
                               : obf2::app::skippedLods(engine.settings().video.geometryQuality);
    viewOptions.clampLod = !args.explicitLod;
    viewOptions.geometryQuality = engine.settings().video.geometryQuality;
    viewOptions.traceFrom = args.traceFrom;
    viewOptions.traceFrames = args.traceFrames;
    viewOptions.watchPose = args.watchPose;
    viewOptions.watchWeapon = args.watchWeapon;
    viewOptions.watchRagdoll = args.watchRagdoll;
    viewOptions.watchSeated = args.watchSeated;
    worldView.init(files, registry, *renderer, resolveTexture, *remote, level ? &*level : nullptr,
                   viewOptions);
  }
  // Our own arms and weapon, drawn through their own camera over the world
  // (`obf2/app/first_person.h`).
  obf2::app::FirstPersonView firstPersonView;
  std::vector<obf2::gfx::MeshRenderer::DrawItem> firstPersonItems;
  // The newest of our actions the weapon's trigger has played.
  std::optional<std::uint32_t> firstPersonAction;
  int firstPersonFireActions = 0;  // for the end-of-run report
  if (remote != nullptr) firstPersonView.init(files, registry, *renderer, resolveTexture);
  // Our weapon's sounds (combat_feedback.h) through the level's reverb
  // (environment_audio.h), from the world view's one pool of sources.
  obf2::app::EnvironmentAudio environmentAudio;
  obf2::app::CombatFeedback weaponSounds;
  // Where our rounds land: their marks and effects (own_impacts.h).
  obf2::app::OwnImpacts ownImpacts;
  if (remote != nullptr) {
    ownImpacts.init(files, *renderer, &worldView.voices());
    // The server names the level on `--connect`.
    const std::string& levelName = remote->levelName.empty() ? args.levelName : remote->levelName;
    std::printf("  reverb: %s\n", environmentAudio.load(files, levelName)
                                      ? "the level's EAX environment is loaded"
                                      : "none (the level names no environment, or it is unreadable)");
    weaponSounds.prepareAudio();
    weaponSounds.setEnvironment(&environmentAudio);
    weaponSounds.setVoices(&worldView.voices());
    weaponSounds.setVolume(engine.settings().audio.effectsVolume);
  }

  std::vector<obf2::gfx::MeshRenderer::DrawItem> items;
  items.reserve(scene.instances.size());
  // The items whose LOD is picked each frame by distance: {item, lod chain}.
  struct LodItem {
    std::size_t item;
    int chain;
    const obf2::app::Scene::Instance* instance;
  };
  std::vector<LodItem> lodItems;
  // The first field of view the camera is given counts as 1 in the LOD factor
  // (setFov, `RendDX9.dll` 0x101c2bd0); set on the first frame.
  float firstFieldOfView = 0.0f;
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
    if (instance.lodChain >= 0) lodItems.push_back({items.size(), instance.lodChain, &instance});
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
        obf2::app::probeCollisionNear(*args.collisionNear, *level, registry, *remoteCollision);
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

  // The load is over: the loading screen's flag goes off and its movie waits
  // for JOIN GAME or Escape, as the original's does. A scripted run (`--frames`)
  // cannot press either, so it leaves after three seconds — ours, for runs only.
  if (loadingScreen.isOpen()) {
    loadingScreen.finish(*device, *renderer, args.frames > 0 ? 3.0 : 0.0);
    loadingScreen.release(*renderer);
    if (loadingScreen.closed()) return 0;
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
  // Whether the squad name box holds the keyboard (the frame loop hands it over).
  bool squadNameEditing = false;
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
    // What the run gives itself: a turn, a run and a jump on named frames
    // (`obf2/app/scripted_input.h`).
    {
      const obf2::app::ScriptedFrame scripted = obf2::app::scriptedInput(args, frame);
      frameInput.mouseDeltaX += scripted.mouseDeltaX;
      frameInput.mouseDeltaY += scripted.mouseDeltaY;
      if (scripted.hasMove) {
        frameInput.moveForward = scripted.moveForward;
        frameInput.moveRight = scripted.moveRight;
        frameInput.sprint = scripted.sprint;
      }
      if (scripted.jump) frameInput.jump = true;
      if (scripted.fire) frameInput.fire = true;
      if (scripted.reload) frameInput.reload = true;
      if (scripted.altFire) frameInput.altFire = true;
      if (scripted.crouch) frameInput.crouch = true;
      if (scripted.prone) frameInput.prone = true;
      frameInput.weaponKeys |= scripted.weaponKeys;
      if (scripted.use) frameInput.use = true;
      frameInput.seatKeys |= scripted.seatKeys;
      if (scripted.camera) frameInput.vehicleCameraKey = SDL_SCANCODE_C;
    }
    // The squad name box takes the keyboard once focused
    // (hud::SquadList::setSquadNameSelect); Return or Escape give it back, and
    // CREATE or closing the panel drop the focus. While it types, no key moves
    // the soldier.
    {
      auto& squads = hudManager.squadList();
      if (squadNameEditing && !squads.nameFocused()) {
        device->endTextField();
        squadNameEditing = false;
      } else if (!squadNameEditing && squads.nameFocused()) {
        device->beginTextField(squads.nameBox(), obf2::hud::SquadList::kNameLength);
        squadNameEditing = device->editingTextField();
        if (!squadNameEditing) squads.endNameEdit();
      } else if (squadNameEditing && device->editingTextField()) {
        if (squads.nameBox() != device->textField()) {
          squads.setNameBox(device->textField());
          hudManager.markSpawnDirty();
        }
      } else if (squadNameEditing) {
        squads.endNameEdit();
        squadNameEditing = false;
      }
      if (squadNameEditing) {
        frameInput.moveForward = frameInput.moveRight = 0.0f;
        frameInput.sprint = frameInput.jump = frameInput.crouch = frameInput.prone = false;
        frameInput.reload = false;
        frameInput.weaponKeys = 0;
      }
    }
    // The communication roses (field_ui.h, docs/functions/command-rose.md): Q
    // (`c_GIRadioComm`) or T (`c_GITacticalComm`) held opens the rose `roseFor`
    // picks for our role, the mouse moves its pointer instead of the view, and a
    // release over an entry runs the table's console command, as the button's
    // `selectOrder` does. While it is open the mouse neither turns nor fires.
    if (remote != nullptr && hudManager.ready() && remote->combatAlive()) {
      const std::string_view radioKey = hudManager.controls().key("c_GIRadioComm");
      const std::string_view tacticalKey = hudManager.controls().key("c_GITacticalComm");
      const bool scripted = roseHold.frames > 0;
      if (scripted) --roseHold.frames;
      const bool typing = device->editingTextField();
      const bool tactical =
          (!tacticalKey.empty() && device->isKeyDown(tacticalKey)) || (scripted && roseHold.tactical);
      const bool held = !typing && (tactical || scripted || (!radioKey.empty() && device->isKeyDown(radioKey)));
      if (scripted && fieldUi.radioOpen() && !roseHold.moved) {
        frameInput.mouseDeltaX += roseHold.dx;
        frameInput.mouseDeltaY += roseHold.dy;
        roseHold.moved = true;
      }
      const bool consumed = held || fieldUi.radioOpen();
      if (held && !fieldUi.radioOpen()) {
        fieldUi.prepareRadio(files);
        fieldUi.setLocalize(
            [&engine](std::string_view key) { return std::string(engine.lexicon().text(key)); });
      }
      const auto self = remote->world.players().find(static_cast<std::uint32_t>(remote->world.ownPlayer()));
      const unsigned squad = self != remote->world.players().end() ? self->second.squad.value_or(0) : 0;
      const bool leader = squad != 0 && self->second.squadLeader;
      const auto rose = obf2::app::roseFor(tactical, leader, remote->isCommander(), squad != 0,
                                           remote->vehicleRootOf(remote->ourSoldier) != 0);
      const float pixelsPerHudUnit = static_cast<float>(hudManager.screen().height) / 600.0f;
      if (const auto retail = fieldUi.radioRelative(held, frameInput.mouseDeltaX, frameInput.mouseDeltaY,
                                                    frameInput.clicked, pixelsPerHudUnit, rose)) {
        std::printf("  rose: %s\n", retail->c_str());
        if (!engine.console().executeLine(*retail)) {
          std::printf("  rose: the command was not recognised: %s\n", retail->c_str());
        }
      }
      if (consumed) {
        frameInput.mouseDeltaX = frameInput.mouseDeltaY = 0.0f;
        frameInput.clicked = false;
        frameInput.fire = frameInput.altFire = false;
      }
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
      // The round's voice: B and V held (match_voice.h), only while the window
      // has the keyboard and no text is being typed.
      matchVoice.update(*remote, device->isKeyDown("IDKey_B"), device->isKeyDown("IDKey_V"),
                        !device->editingTextField() &&
                            (SDL_GetWindowFlags(device->window()) & SDL_WINDOW_INPUT_FOCUS) != 0);
      // `--group <n>`: spawn without the screen. The spawn screen is a debt of its
      // own (it builds empty on some rounds), and without a soldier the client
      // sends the server nothing at all — which is a different measurement from
      // the one we want.
      if (args.spawnGroupGiven && args.spawnGroup != 0 && !askedForGroup &&
          !remote->world.spawnGroups().empty()) {
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
    // Where the frame is seen from (`obf2/app/camera.h`). The soldier's own eye is
    // filled in first, because that is also where his input is queued and where
    // his tick is run.
    obf2::app::CameraInputs cameraInputs;
    cameraInputs.fixed = args.camera;
    cameraInputs.fixedYaw = args.cameraYaw;
    cameraInputs.fixedPitch = args.cameraPitch;
    cameraInputs.topDown = args.topDown;
    cameraInputs.sceneCentre = scene.center;
    cameraInputs.distance = distance;
    cameraInputs.eyeHeight = eyeHeight;
    cameraInputs.frame = frame;
    if (level && level->hasBeforeSpawnCamera) {
      cameraInputs.beforeSpawnPosition = level->beforeSpawnCameraPos;
      cameraInputs.beforeSpawnRotation = level->beforeSpawnCameraRot;
    }

    // Until the player has spawned there is nothing to look from in first person:
    // there is no soldier yet. The spawn screen's camera from Init.con works then.
    // The field of view of the soldier's camera, when the frame is seen through it.
    std::optional<float> soldierFov;
    // The seat's camera's, when the frame is seen from a vehicle.
    std::optional<float> vehicleFov;
    std::optional<obf2::Vec3f> vehicleUp;
    const auto remoteSoldier =
        remote != nullptr ? remote->soldierPosition() : std::optional<obf2::Vec3f>{};
    // In a vehicle there is no soldier position: the hull we sit in stands for it.
    const auto vehiclePose = remote ? remote->occupiedVehiclePose() : std::nullopt;
    if ((hostedServer && hostedClient && localSoldierId != 0) || remoteSoldier || vehiclePose) {
      // The soldier's position comes from whoever owns him: our server in our own
      // game, that server on a real one.
      obf2::Vec3f soldierEye =
          remoteSoldier ? *remoteSoldier : vehiclePose ? vehiclePose->position
                                                       : hostedClient->interpolatedPosition(localSoldierId);
      // In our own game the eye over the local server's position is **not
      // measured**: that server is not the engine's tick (CLAUDE.md, "Physics").
      // A real server's is the engine's (`soldierEye`, below).
      soldierEye.y += 1.7f;

      // On a real server the input goes to the same place as in our own game —
      // only in another form: as the player action stream.
      if ((remoteSoldier || vehiclePose) && remote != nullptr) {
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
        // The throttle on the wire (bf2_protocol.h, throttleToWire): ±100 in a
        // vehicle and forward on foot, the legacy 99 for a soldier's reverse.
        out.axes[obf2::net::bf2::kAxisThrottle] =
            obf2::net::bf2::throttleToWire(raw.moveForward, remoteSoldier.has_value());
        // Strafing is the yaw axis: in Controls.con D/A hang on exactly `c_PIYaw`,
        // and the engine has no separate strafe axis. In a vehicle it steers, at
        // the vehicle's full scale (kVehicleAxisFull).
        out.axes[obf2::net::bf2::kAxisYaw] = static_cast<std::int16_t>(
            raw.moveRight * (remoteSoldier ? obf2::net::bf2::kAxisFull : obf2::net::bf2::kVehicleAxisFull));
        if (raw.sprint) out.buttons |= obf2::net::bf2::kButtonSprint;
        // E enters the vehicle in front of us and leaves the one we sit in; F1..F8
        // take that seat (`c_PIPositionSelectN`). The server decides both.
        if (raw.use) out.buttons |= obf2::net::bf2::kButtonUse;
        for (int seat = 1; seat <= 8; ++seat) {
          if (raw.seatKeys & (1u << (seat - 1))) {
            out.buttons |= obf2::net::bf2::kButtonPositionSelect(seat);
          }
        }
        if (raw.jump) out.buttons |= obf2::net::bf2::kButtonAction;
        if (raw.fire) out.buttons |= obf2::net::bf2::kButtonFire;
        if (raw.reload) out.buttons |= obf2::net::bf2::kButtonReload;
        if (raw.altFire) out.buttons |= obf2::net::bf2::kButtonAltFire;
        if (raw.crouch) out.buttons |= obf2::net::bf2::kButtonCrouch;
        if (raw.prone) out.buttons |= obf2::net::bf2::kButtonLie;
        for (int key = 1; key <= 9; ++key) {
          if ((raw.weaponKeys & (1u << (key - 1))) != 0) {
            out.buttons |= obf2::net::bf2::kButtonWeaponSelect(key);
          }
        }
        // Zoomed, the look is slower — but the engine does that itself, inside the
        // soldier's input, after the axes come off the wire (`getMouseSpeedMod`,
        // `BF2.exe` 0x5a61a0, called at 0x5ae27b). So the action carries the axes
        // unscaled, and our prediction applies the same mod: the field of view the
        // player has (the zoom level's, else the camera's) over the camera's, times
        // the weapon's `mouseSpeedMod`. We used to scale the axes here, which the
        // server then scaled a second time.
        {
          const auto own = remote->world.objects().find(remote->ourSoldier);
          const std::string* ownName =
              own != remote->world.objects().end()
                  ? remote->templateNumbers.nameOf(own->second.templateId)
                  : nullptr;
          const float cameraFov =
              ownName != nullptr
                  ? obf2::game::soldierCameraFov(remote->registry, *ownName).value_or(0.0f)
                  : 0.0f;
          const auto& zoom = firstPersonView.zoom();
          const float playerFov = zoom.fov() > 0.0f ? zoom.fov() : cameraFov;
          remote->actionContext.zoomed = zoom.zoomed();
          remote->actionContext.mouseSpeedMod =
              obf2::server::soldierMouseSpeedMod(playerFov, cameraFov, zoom.mouseSpeedMod());
        }
        remote->queueInput(raw.mouseDeltaX * args.mouseScale, raw.mouseDeltaY * args.mouseScale,
                           out);
        // And we compute our own movement at once: waiting for the server's
        // correction would mean jerks a few tenths of a second apart.
        remote->predict(frameStep);
        // Drawn between ticks, as `BF2FrameInterpolator` does (RemoteWorld).
        yaw = remote->cameraYaw();
        pitch = remote->cameraPitch();
        // The eye: the template's `poseCameraPosition` for his pose from his pivot,
        // turned by the body's yaw (obf2/app/first_person.h), approached a share per
        // tick as the pose changes (RemoteWorld::advanceEye).
        const auto ownObject = remote->world.objects().find(remote->ourSoldier);
        const std::string* ownTemplate =
            ownObject != remote->world.objects().end()
                ? remote->templateNumbers.nameOf(ownObject->second.templateId)
                : nullptr;
        // Seated, our object is the seat: its template has no pose cameras.
        if (ownTemplate != nullptr && !remote->poseCameraKnown && !vehiclePose) {
          for (int pose = 0; pose < 3; ++pose) {
            if (const auto at = obf2::game::poseCameraPosition(remote->registry, *ownTemplate, pose)) {
              remote->poseCamera[static_cast<std::size_t>(pose)] = obf2::Vec3f{at->x, at->y, at->z};
            }
          }
          remote->poseCameraKnown = true;
        }
        // Seated, the seat's camera (WorldView::occupiedCamera) is the frame's.
        if (const auto vehicle = worldView.occupiedCamera(raw.mouseDeltaX * args.mouseScale,
                                                          raw.mouseDeltaY * args.mouseScale,
                                                          raw.vehicleCameraKey)) {
          cameraInputs.vehicle = vehicle->camera;
          vehicleFov = vehicle->fieldOfView;
          vehicleUp = vehicle->camera.up;
          yaw = vehicle->yaw;
          pitch = vehicle->pitch;
          soldierEye = vehicle->camera.eye;
        } else if (const auto drawn = remote->drawnSoldierPosition()) {
          const obf2::Vec3f offset = remote->eyeKnown ? remote->eyeOffset : remote->poseCamera[0];
          soldierEye = obf2::app::soldierEye(*drawn, remote->physics.pivotHeight,
                                             remote->look.bodyYaw, offset);
        }
        if (ownTemplate != nullptr && !vehiclePose) {
          soldierFov = obf2::game::soldierCameraFov(remote->registry, *ownTemplate);
          // Zoomed, the player's field of view is the weapon's level (`Player::setFov`
          // by `DefaultZoomComp::updateComponent`, Linux 0x6298e0); 0 is the
          // camera's own.
          if (const float zoomFov = firstPersonView.zoom().fov(); zoomFov > 0.0f) {
            soldierFov = zoomFov;
          }
        }
      }
      cameraInputs.soldierEye = soldierEye;
      cameraInputs.soldierYaw = yaw;
      cameraInputs.soldierPitch = pitch;
      if (remote != nullptr && frame >= args.traceFrom &&
          frame < args.traceFrom + args.traceFrames) {
        std::printf("  trace frame %d: step %.4f, eye %.4f %.4f %.4f, yaw %.3f pitch %.3f, "
                    "game tick %u fraction %.3f, unanswered %zu\n",
                    frame, frameStep, soldierEye.x, soldierEye.y, soldierEye.z, yaw, pitch,
                    remote->world.gameTick(),
                    remote->tick.pending / obf2::server::kTickTime, remote->sent.size());
      }
    }

    const obf2::app::CameraView camera = obf2::app::chooseCamera(cameraInputs);
    obf2::Vec3f eye = camera.eye;
    obf2::Vec3f lookTarget = camera.target;

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
        const auto seated = worldView.seatedAt(watched);
        const obf2::Vec3f at = seated ? *seated : pose ? pose->position : object.position;
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
                      "speed %.2f, %.0f m from our body, at %.2f %.2f %.2f, pose %u, "
                      "weapon out %d, ragdoll particles %zu\n",
                      watched, object.updates, object.track.count(),
                      newest != nullptr ? nowMs - newest->timeMs : -1.0f,
                      pose ? static_cast<int>(pose->mode) : -1,
                      newest != nullptr && newest->velocity ? obf2::length(*newest->velocity)
                                                            : -1.0f,
                      fromUs, at.x, at.y, at.z, object.lastPose, object.lastWeaponIndex,
                      object.ragdollParticles.size());
        }
      }
    }

    const float aspect =
        acquired->height == 0
            ? 1.0f
            : static_cast<float>(acquired->width) / static_cast<float>(acquired->height);
    // The near plane. A level is drawn with the renderer's own: `RendDX9.dll` sets
    // the world camera's near plane to its +0x590 at the top of every frame
    // (0x1002c2e0), 0.041 from the constructor (0x1002dd16, `0x3d27ef9e`;
    // console `renderer.tweakNearPlane`, which no game file sets). The roads'
    // depth bias is measured against it (docs/functions/render-camera.md). The
    // model viewer is not the game: there the camera is far away, and a near
    // plane proportional to the model gives better depth precision.
    const bool firstPerson = hostedServer != nullptr || remoteSoldier.has_value();
    const float nearPlane = level.has_value() ? 0.041f : scene.radius * 0.002f + 0.05f;

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
    // The original sets this far plane on every frame of a level, with or
    // without a local player (`BF2.exe` 0x453270, docs/functions/render-camera.md),
    // so a free `--camera` view uses it too. The top-down view keeps the scene's
    // reach.
    const float levelFar = viewDistance > 1.0f ? viewDistance : fogFar;
    const bool levelView3d = level.has_value() && !args.topDown;
    const float farPlane =
        (firstPerson || levelView3d) && levelFar > 1.0f ? levelFar : scene.radius * 40.0f;

    // The world's vertical field of view. Through the soldier's eye it is his
    // camera's (`soldierCameraFov`): a battle hands the renderer the player's
    // `getFov` (`BF2.exe` 0x453270), `Player::setFov(0, …)` takes the camera's own
    // (Linux 0x495e32 → `getActiveFOV` 0x5773c0) — which call sets it on foot is not
    // read — and the renderer's setter fits it to the aspect (`aspectFieldOfView`).
    // Anywhere else — the spawn screen's camera, the debugging cameras, our own
    // game — 1.05 is **not measured**. From a vehicle it is the seat camera's
    // (`insideFOV`, or the external camera's `FOV`; vehicle_seats.h), fitted the
    // same way.
    const bool throughSoldier = soldierFov.has_value() && !args.camera && !args.watchSoldier;
    const bool throughVehicle = vehicleFov.has_value() && !args.camera && !args.watchSoldier;
    const float fieldOfView =
        throughVehicle ? obf2::app::aspectFieldOfView(*vehicleFov, 1.0f / aspect)
        : throughSoldier ? obf2::app::aspectFieldOfView(*soldierFov, 1.0f / aspect) : 1.05f;
    const obf2::Mat4 projection = obf2::perspective(fieldOfView, aspect, nearPlane, farPlane);
    // Each placed mesh's LOD by its distance from the camera, every frame
    // (`RendDX9.dll` 0x100fe5d0; `game::pickLod`). The distance is to the
    // object's origin, as the draw measures it.
    if (firstFieldOfView <= 0.0f) firstFieldOfView = fieldOfView;
    const float fovFactor = obf2::game::lodFovFactor(fieldOfView, firstFieldOfView);
    worldView.setLodFovFactor(fovFactor);
    if (!lodItems.empty()) {
      const float scale = obf2::game::lodDistanceScale(engine.settings().video.geometryQuality);
      for (const LodItem& entry : lodItems) {
        const auto& chain = scene.lodChains[static_cast<std::size_t>(entry.chain)];
        auto& item = items[entry.item];
        const obf2::Vec3f at{item.transform.m[12], item.transform.m[13], item.transform.m[14]};
        const std::size_t lod = obf2::game::pickLod(chain.switchDistances, scale, fovFactor,
                                                    obf2::length(at - eye), chain.baseRadius);
        const auto mesh = static_cast<std::size_t>(chain.meshes[lod]);
        if (!uploadedOk[mesh]) continue;
        item.mesh = &gpuMeshes[mesh];
        // The LOD drawn brings its own light map.
        item.lightmap = nullptr;
        if (lod < entry.instance->lodLightmaps.size()) {
          const auto& light = entry.instance->lodLightmaps[lod];
          if (light.atlas >= 0 && static_cast<std::size_t>(light.atlas) < lightmapPages.size()) {
            item.lightmap = lightmapPages[static_cast<std::size_t>(light.atlas)];
            std::memcpy(item.lightmapOffset, light.offset, sizeof(item.lightmapOffset));
          }
        }
      }
    }
    // From above "screen up" has to be Z rather than Y (which would coincide with the
    // view). In a left-handed system right is cross(up, forward), so with up = +Z
    // right ends up as +X — exactly as on the level's own minimap.
    // Inside a vehicle the seat's camera rolls with the hull.
    const obf2::Vec3f up =
        args.topDown && !bootMode ? obf2::Vec3f{0.0f, 0.0f, 1.0f}
        : throughVehicle && vehicleUp ? *vehicleUp : obf2::Vec3f{0.0f, 1.0f, 0.0f};
    const obf2::Mat4 view = obf2::lookAt(eye, lookTarget, up);
    lastCommEye = eye;
    lastCommDirection = obf2::normalize(lookTarget - eye);

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
        worldView.collect(withOthers, frame, frameStep, eye, view);
        toDraw = &withOthers;
      }
      if (level) {
        if (withOthers.empty()) withOthers = items;
        ambientEffects.advance(frameStep);
        ambientEffects.listen(eye);
        ambientEffects.draw(withOthers, view);
        toDraw = &withOthers;
      }
      if (remote != nullptr) {
        if (withOthers.empty()) withOthers = items;
        ownImpacts.draw(*remote, eye, view, frameStep, withOthers);
        toDraw = &withOthers;
      }
      // The specular is the one thing that depends on where the eye is.
      renderer->setCameraPosition(eye);
      // The water's clock: our seconds, wrapped at 1000 s (docs/functions/water.md,
      // "Ours" — the original's global clock at 0x100dc130 is not ours).
      renderer->setWaterTime(static_cast<float>(SDL_GetTicks() % 1000000) * 0.001f);
      renderer->renderScene(*acquired, *toDraw, projection * view,
                            obf2::gfx::Color{0.42f, 0.55f, 0.68f, 1.0f});

      // Our arms and weapon, when the camera is our soldier's eye: the weapon his
      // ghost says is out, or what `pickupKit` makes active after a spawn
      // (crosshair.h), on the first-person skeleton, through the first-person
      // camera — the same view, its own projection (`RendDX9.dll` 0x1002c35d).
      firstPersonItems.clear();
      if (remote != nullptr && remote->playerSpawned && remoteSoldier && !args.watchSoldier) {
        const auto soldier = remote->world.objects().find(remote->ourSoldier);
        const std::string* soldierName =
            soldier != remote->world.objects().end()
                ? remote->templateNumbers.nameOf(soldier->second.templateId)
                : nullptr;
        if (soldierName != nullptr) {
          const std::uint32_t kit = remote->world.kitTemplateOf(remote->ourSoldier);
          const std::string* kitName = kit != 0 ? remote->templateNumbers.nameOf(kit) : nullptr;
          const std::string kitText = kitName != nullptr ? *kitName : std::string();
          // The item our own number key picked, for this soldier only.
          if (ownItemSoldier != remote->ourSoldier) {
            ownItem.reset();
            ownItemSoldier = remote->ourSoldier;
            firstPersonView.newSoldier();
          }
          const int item = ownItem.has_value()
                               ? *ownItem
                               : soldier->second.lastWeaponIndex >= 0
                                     ? soldier->second.lastWeaponIndex
                                     : obf2::app::spawnItemIndex(remote->registry, kitText);
          if (firstPersonView.setWeapon(*soldierName, kitText, item)) {
            // The trigger, once per action we played — the weapon's update runs
            // in the same tick as the input (`GenericFireArm::handleUpdate`,
            // `BF2.exe` 0x5e3ed0).
            for (const auto& [number, action] : remote->sent) {
              if (firstPersonAction && number <= *firstPersonAction) continue;
              // `c_PIWeaponSelectN` picks the kit's item with `itemIndex` N (the
              // keys and the data agree: 1 knife, 2 pistol, 3 rifle, 4 grenade; the
              // engine's own mapping is **not read**). A key for an item the kit
              // lacks does nothing.
              for (int key = 1; key <= 9; ++key) {
                if ((action.buttons & obf2::net::bf2::kButtonWeaponSelect(key)) == 0) continue;
                if (key == ownItem.value_or(item)) continue;
                if (obf2::app::weaponOut(remote->registry, kitText, key) == nullptr) continue;
                ownItem = key;
                firstPersonView.setWeapon(*soldierName, kitText, key);
              }
              const bool fireHeld = (action.buttons & obf2::net::bf2::kButtonFire) != 0;
              if (fireHeld) ++firstPersonFireActions;
              const bool fired = firstPersonView.onAction(
                  number, fireHeld, (action.buttons & obf2::net::bf2::kButtonReload) != 0,
                  (action.buttons & obf2::net::bf2::kButtonAltFire) != 0);
              // For the crosshair's deviation, by action tick: bit 0 a round left,
              // bit 1 the weapon's fire state. The fire state as "the trigger held
              // on a weapon that can fire" is **inferred** — `getFireState` itself
              // is not read.
              weaponFireAt[number % weaponFireAt.size()] = static_cast<std::uint8_t>(
                  (fired ? 1u : 0u) | (fireHeld && firstPersonView.ammo().canFire() ? 2u : 0u));
              weaponFireTick[number % weaponFireTick.size()] = number;
              firstPersonAction = number;
              // A round out: the weapon's first-person fire sound.
              if (fired) {
                const auto* weapon = obf2::app::weaponOut(remote->registry, kitText, ownItem.value_or(item));
                weaponSounds.shot(files, remote->registry, weapon, true);
                // Where it lands (own_impacts.h), from this action's eye and view.
                if (remote->bodyReady) {
                  const obf2::Vec3f offset = remote->eyeKnown ? remote->eyeOffset : remote->poseCamera[0];
                  ownImpacts.shot(*remote, weapon,
                                  obf2::app::soldierEye(remote->body.position, remote->physics.pivotHeight,
                                                        remote->look.bodyYaw, offset),
                                  obf2::app::lookDirection(remote->yaw(), remote->pitch()));
                }
              }
            }
            weaponSounds.releaseFinished();
            // The movement in the camera's own frame, as `updateAnimationSystems`
            // measures it against the movement matrix (Linux 0x554bde): x the side,
            // z forward, the vertical left out.
            obf2::anim::State state;
            const obf2::Vec3f velocity = remote->body.velocity;
            const float planar = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
            if (planar > 0.001f) {
              const float yawRadians = yaw * 3.14159265358979323846f / 180.0f;
              state.speed = planar;
              state.direction[0] =
                  (velocity.x * std::cos(yawRadians) - velocity.z * std::sin(yawRadians)) / planar;
              state.direction[1] = 0.0f;
              state.direction[2] =
                  (velocity.x * std::sin(yawRadians) + velocity.z * std::cos(yawRadians)) / planar;
            }
            // The spring's input: the last action's turn, the velocity, and the
            // movement matrix — the soldier camera's own (`getMovementMatrix`,
            // Linux 0x54ac70).
            const obf2::Mat4 camera = obf2::app::firstPersonCamera(eye, yaw, pitch);
            obf2::anim::CameraSpring::Input spring;
            spring.step = frameStep;
            spring.turn[0] = remote->lastTurn[0];
            spring.turn[1] = remote->lastTurn[1];
            spring.velocity = velocity;
            spring.right = obf2::transformDirection(camera, obf2::Vec3f{1.0f, 0.0f, 0.0f});
            spring.up = obf2::transformDirection(camera, obf2::Vec3f{0.0f, 1.0f, 0.0f});
            spring.forward = obf2::transformDirection(camera, obf2::Vec3f{0.0f, 0.0f, 1.0f});
            firstPersonView.update(state, frameStep, spring);
            firstPersonView.collect(firstPersonItems, camera);
          }
        }
      }
      if (!firstPersonItems.empty()) {
        renderer->renderScene(*acquired, firstPersonItems,
                              obf2::app::firstPersonProjection(aspect) * view,
                              obf2::gfx::Color{}, obf2::gfx::MeshRenderer::ScenePass::FirstPerson);
      }

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
        //
        // On `--connect` that is the session's own flag: raised when the server
        // spawns our player and dropped when it says he died. "DONE was pressed"
        // stood in for it, and it never fell again — after a death the spawn screen
        // did not come back, so there was nothing to respawn with. Between DONE
        // and the soldier the screen stays, as the original's does, with the
        // countdown on its bar.
        const bool spawned = hostedServer != nullptr ? localSoldierId != 0
                             : remote != nullptr     ? remote->playerSpawned
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
        hudManager.gameState(spawned ? (hudManager.bigMap() ? 2 : 0) : 1);
        // Tab, held: the scoreboard over whatever the HUD is on, and back to it on
        // release (the game-input handler 0x758770, docs/functions/hud-states.md).
        // `--hud-screen Scoreboard` holds it for a screenshot.
        {
          const std::string_view tabKey = hudManager.controls().key("c_GIShowScoreboard");
          const bool forced = args.hudScreenName == "Scoreboard";
          // A spawn takes the HUD out of the scoreboard, as the engine's spawn
          // event does, and a key that stays down never presses again. The
          // screenshot switch presses it anew whenever that has happened.
          if (forced && hudManager.state() != 9) hudManager.scoreboardKey(false);
          hudManager.scoreboardKey(forced || (!tabKey.empty() && device->isKeyDown(tabKey)));
        }
        // The spawn screen's circles: the server's groups, once it has named any.
        if (remote != nullptr) {
          hudManager.useServerGroupsOnly();
          remote->spawnWorldSize = hudManager.mapWorldSize();
          if (remote->world.spawnGroupRevision() != shownSpawnGroups) {
            shownSpawnGroups = remote->world.spawnGroupRevision();
            hudManager.serverSpawnGroups(
                obf2::app::spawnCircles(remote->world.spawnGroups(), hudManager.mapWorldSize()));
          }
          // A squad's group is for its squad only (`getGroupsForPlayer`): our squad
          // is our own player's state (0x2000, `setSquadId`).
          const auto self = remote->world.players().find(
              static_cast<std::uint32_t>(remote->world.ownPlayer()));
          if (self != remote->world.players().end()) {
            hudManager.localSquad(static_cast<int>(self->second.squad.value_or(0)),
                                  self->second.squadLeader);
          }
          // The map's live icons (map_contacts.h): our team's players, the
          // server's vehicles, marked and spotted enemies, our orders and the
          // commander's assets. A teammate the map puts in the sea is a position
          // gone stale: said once per player, so it can be chased.
          {
            static std::set<std::uint32_t> reportedOff;
            obf2::app::visitMapContacts(*remote, [&](std::string_view name,
                                                     const obf2::hud::MapContact& contact) {
              if (contact.kind != obf2::hud::MapContact::Kind::Team &&
                  contact.kind != obf2::hud::MapContact::Kind::Squad)
                return;
              if (contact.object == 0 || !hudManager.outsideCombatArea(contact.worldX, contact.worldZ))
                return;
              if (reportedOff.insert(contact.object).second) {
                std::printf("  map: %.*s (object %u) drawn outside the combat area at %.0f %.0f\n",
                            static_cast<int>(name.size()), name.data(), contact.object,
                            contact.worldX, contact.worldZ);
              }
            });
            hudManager.setMinimapContacts(obf2::app::minimapContacts(*remote));
          }
          hudManager.serverInfo(remote->serverName, remote->serverHost, remote->serverPort,
                                remote->levelName, remote->votingEnabled);
          // The squad tab's world (`SquadMenuNew::update`, 0x792100): our player
          // and our team's squads, the leader first (squad_roster.h).
          hudManager.squadWorld(obf2::app::squadWorld(*remote, spawned));

          // The crosshair. The weapon our soldier has out is what his own ghost's
          // state says (0x1000) against his kit; every action we played since the
          // last frame is one `updateDeviation` (BF2.exe 0x5af4ea).
          const std::uint16_t ours = remote->ourSoldier;
          const auto soldier = remote->world.objects().find(ours);
          const obf2::game::ObjectTemplate* weapon = nullptr;
          int pose = 0;
          std::string kitText;
          int item = -1;
          if (spawned && ours != 0 && soldier != remote->world.objects().end()) {
            const std::uint32_t kit = remote->world.kitTemplateOf(ours);
            const std::string* kitName = kit != 0 ? remote->templateNumbers.nameOf(kit) : nullptr;
            if (kitName != nullptr) kitText = *kitName;
            // His own state's weapon (0x1000) when the server sends it; it does not
            // for our own soldier, and then what `pickupKit` makes active.
            item = ownItem.has_value() && ownItemSoldier == ours
                       ? *ownItem
                       : soldier->second.lastWeaponIndex >= 0
                             ? soldier->second.lastWeaponIndex
                             : obf2::app::spawnItemIndex(remote->registry, kitText);
            weapon = obf2::app::weaponOut(remote->registry, kitText, item);
            pose = static_cast<int>(soldier->second.lastPose);
          }
          // Which link is missing when there is no crosshair: the kit, the item, or
          // the kit's child with that item.
          if (std::string key = kitText + "|" + std::to_string(item); key != crosshairKey) {
            const bool known = soldier != remote->world.objects().end();
            std::printf("  crosshair: soldier %u, kit %s, item %d -> %s (his records %d, "
                        "not read for want of the inventory size %d)\n",
                        ours, kitText.empty() ? "(unknown)" : kitText.c_str(), item,
                        weapon != nullptr ? weapon->name.c_str() : "no weapon",
                        known ? soldier->second.soldierRecords : -1,
                        known ? soldier->second.withoutInventory : -1);
            crosshairKey = std::move(key);
          }
          crosshair.setWeapon(weapon);
          // Our own soldier's pose is our prediction's (the server's state corrects
          // it); his ghost records, which `pose` above reads, are not sent for him.
          pose = remote->body.pose;
          for (const auto& [number, action] : remote->sent) {
            if (crosshairAction && number <= *crosshairAction) continue;
            const std::size_t slot = number % weaponFireAt.size();
            const std::uint8_t fire = weaponFireTick[slot] == number ? weaponFireAt[slot] : 0;
            crosshair.onAction(action, pose, false, firstPersonView.zoom().zoomed(),
                               (fire & 2u) != 0, (fire & 1u) != 0);
            crosshairAction = number;
          }
          std::map<std::string, float> shown;
          crosshair.apply(shown);
          if (shown != crosshairShown) {
            for (const char* name :
                 {"CrosshairUpPos", "CrosshairDownPos", "CrosshairLeftPos", "CrosshairRightPos"}) {
              hudManager.values().erase(name);
            }
            for (const auto& [name, value] : shown) hudManager.values()[name] = value;
            crosshairShown = std::move(shown);
            hudManager.markDirty();
          }
          // The ammunition: only while a weapon is out and its first-person model
          // runs (`FirstPersonView::onAction` plays the component).
          if (weapon != nullptr && firstPersonView.ready()) {
            obf2::app::AmmoHud ammoNow = obf2::app::ammoHud(
                firstPersonView.ammo(), firstPersonView.lastTick(), ammoReloadProgress);
            // A skipped block keeps what the layer already holds.
            for (const auto& [name, value] : ammoShown.values) {
              ammoNow.values.emplace(name, value);
            }
            for (const auto& [name, value] : ammoShown.strings) {
              ammoNow.strings.emplace(name, value);
            }
            for (const auto& [name, value] : ammoShown.flags) {
              ammoNow.flags.emplace(name, value);
            }
            if (ammoNow.values != ammoShown.values || ammoNow.strings != ammoShown.strings ||
                ammoNow.flags != ammoShown.flags) {
              for (const auto& [name, value] : ammoNow.values) hudManager.values()[name] = value;
              for (const auto& [name, value] : ammoNow.strings) hudManager.strings()[name] = value;
              for (const auto& [name, value] : ammoNow.flags) hudManager.variables()[name] = value;
              hudManager.markDirty();
              ammoShown = std::move(ammoNow);
            }
            ammoReloadProgress = ammoShown.values["PrimaryReloadTime"];
          }
        }
        // The spawn bar while dead: the player's own state says which group the
        // server holds for him and the tick he may spawn at (player_state.h, bit
        // 0x20); `getTimeToSpawn` is that tick less the game tick, over 30.
        if (remote != nullptr) {
          const auto own = remote->world.players().find(
              static_cast<std::uint32_t>(remote->world.ownPlayer()));
          if (own != remote->world.players().end()) {
            const bool alive = own->second.alive ? *own->second.alive : spawned;
            if (!alive) {
              float wait = 0.0f;
              if (own->second.spawnAtTick) {
                const auto left = static_cast<std::int64_t>(*own->second.spawnAtTick) -
                                  static_cast<std::int64_t>(remote->world.gameTick());
                wait = left > 0 ? static_cast<float>(left) / 30.0f : 0.0f;
              }
              hudManager.deadSpawnInfo(static_cast<int>(own->second.spawnGroup.value_or(0)), wait);
            }
            // The bottom-left bars and the killed state (obf2/app/player_hud.h).
            obf2::app::PlayerHudInput playerInput;
            playerInput.health = remote->ownHealth;
            playerInput.stamina = remote->sprint.stamina;
            playerInput.sprinting = remote->sprint.sprinting;
            playerInput.alive = alive;
            playerInput.manDown = own->second.manDown.value_or(false);
            const obf2::app::AmmoHud playerNow = obf2::app::playerHud(playerInput);
            if (playerNow.values != playerShown.values || playerNow.strings != playerShown.strings ||
                playerNow.flags != playerShown.flags) {
              for (const auto& [name, value] : playerNow.values) hudManager.values()[name] = value;
              for (const auto& [name, value] : playerNow.strings) hudManager.strings()[name] = value;
              for (const auto& [name, value] : playerNow.flags) hudManager.variables()[name] = value;
              hudManager.markDirty();
              playerShown = playerNow;
            }
          }
        }
        // The scoreboard's rows, from whoever keeps the players in this mode.
        if (remote != nullptr) {
          auto rows = obf2::app::scoreboardPlayers(remote->world.players(), remote->world.ownPlayer(),
                                                   spawned);
          for (auto& row : rows) row.muted = remote->muted(static_cast<std::uint32_t>(row.index));
          // The kick-vote columns: the server's `voting` and `teamVoteOnly` match
          // options, and the player an open kick vote is about.
          const auto& options = remote->serverMatchOptions;
          hudManager.scoreboardVoting(options && options->voting && !remote->endOfRound,
                                      !options || options->teamVoteOnly,
                                      remote->kickVote ? static_cast<int>(remote->kickVote->item) : -1);
          hudManager.scoreboardPlayers(rows, remote->world.ownPlayer());
        } else if (hostedServer != nullptr && hostedClient != nullptr) {
          hudManager.scoreboardPlayers(obf2::app::scoreboardPlayers(hostedServer->players()),
                                       static_cast<int>(hostedClient->playerId()));
        }
        const int hudState = hudManager.state();
        const bool spawnVisible = hudState == 1 || args.hudScreenName == "SpawnMenu";

        // The mouse: in combat the window captures it (otherwise the cursor runs
        // into the screen's edge and looking simply stops), while on the spawn
        // screen it is free, because there it presses buttons.
        // The scoreboard's mouse mode: a right click with the scoreboard up
        // (OpenBF2 `BF2HudManager`, HUD mode 12), the cursor free until the key
        // is let go.
        if (hudState == 9 && (frameInput.rightClicked || args.scoreboardMouse)) {
          hudManager.scoreboardRightClick();
        }
        const bool wantRelativeMouse = spawned && !spawnVisible && !hudManager.scoreboardPointer();
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
            (!hudManager.spawnScreen().teamFromServer() ||
             hudManager.spawnScreen().choice().team != remote->world.ownTeam())) {
          hudManager.spawnScreen().setTeamFromServer(remote->world.ownTeam());
          hudManager.markSpawnDirty();
          std::printf("  spawn screen: the server gave team %d\n",
                      hudManager.spawnScreen().choice().team);
        }
        // `MapFullSize` is not "we are on the spawn screen" but "the map's size has
        // reached the big one" — that is how 0x77d3f8 derives it.
        hudManager.updateVariables(spawned, hudManager.map().fullSize());

        // The first-person models of our team's kits, built ahead while the spawn
        // screen is up — one a frame, the chosen kit first — so the spawn itself
        // finds them made (`FirstPersonView::prepare`).
        if (remote != nullptr && !spawned && level && hudManager.spawnScreen().teamFromServer()) {
          const int team = hudManager.spawnScreen().choice().team;
          const int chosen = hudManager.spawnScreen().choice().kit;
          if (team > 0 && team < 3) {
            for (int n = 0; n < obf2::level::Level::kKitsPerTeam; ++n) {
              const int slot = n == 0 ? chosen : (n <= chosen ? n - 1 : n);
              if (slot < 0 || slot >= obf2::level::Level::kKitsPerTeam) continue;
              const std::string& kit = level->kits[team][slot];
              const std::string& soldier = level->kitSoldiers[team][slot];
              if (kit.empty() || soldier.empty()) continue;
              const int item = obf2::app::spawnItemIndex(remote->registry, kit);
              const auto began = std::chrono::steady_clock::now();
              if (firstPersonView.prepare(soldier, kit, item)) {
                std::printf("  first person prepared ahead: %s|%s|%d in %.0f ms\n", soldier.c_str(),
                            kit.c_str(), item,
                            std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - began)
                                .count());
                break;
              }
            }
          }
        }

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
        if (hudManager.scoreboardPointer()) {
          bool clicked = frameInput.clicked;
          float clickX = frameInput.mouseX;
          float clickY = frameInput.mouseY;
          for (const auto& scheduled : args.clicks) {
            if (frame != scheduled.frame) continue;
            clicked = true;
            clickX = scheduled.x;
            clickY = scheduled.y;
          }
          if (clicked) hudManager.clickScoreboard(clickX, clickY, engine.console());
        }

        // While at least one node is travelling or fading, the screen has to be
        // rebaked every frame: our geometry lives in meshes on the graphics card.
        {
          const auto now = std::chrono::steady_clock::now();
          const float dt = std::chrono::duration<float>(now - lastAnimationTick).count();
          lastAnimationTick = now;
          // A live server's tickets, kills and chat (live_hud.h).
          if (remote != nullptr) {
            obf2::app::updateLiveHud(*remote, hudManager, dt);
            // The radio's voice (docs/functions/radio-commander-votes.md): the
            // newest sample of the frame, unless the profile ignores radio audio
            // (`chat.ignoreRadioAudio`). Its own volume in the sound's template is
            // not read: it plays at the effects volume.
            if (!remote->radioSamples.empty() && !engine.settings().chat.ignoreRadioAudio) {
              radioAudio.open(files, remote->radioSamples.back(), engine.settings().audio.effectsVolume,
                              false);
            }
            remote->radioSamples.clear();
            // The map's timed marks, one map frame, and the satellite's sound
            // (map_hit_sound.h).
            obf2::app::stepMap(*remote, hudManager, mapHitAudio,
                               engine.settings().audio.effectsVolume, dt);
          }
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
        // The map menu goes over everything while its key is held. The
        // scoreboard is not here: it is part of the combat HUD's own walk now, a
        // state rather than a baked screen.
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
        // The open communication rose, over the HUD (field_ui.h).
        if (fieldUi.revision() != roseShown) {
          roseShown = fieldUi.revision();
          auto built = fieldUi.build(hudManager);
          putOnCard(built, rosePieces);
        }
        if (fieldUi.radioOpen()) {
          for (const OwnedPiece& piece : rosePieces) pushPiece(piece);
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
    // The server moved to another map: this session's level is gone (the remote
    // world dropped every pointer into it), and the next session opens the new one.
    if (remote != nullptr && remote->levelChanged) {
      std::printf("  the server changed the map at frame %d: this level's session ends\n", frame);
      break;
    }

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
  for (OwnedPiece& piece : rosePieces) renderer->release(piece.mesh);
  // --hud-vars: every variable the tree asks for, and whether anyone fills it.
  if (args.hudVars && hudManager.ready()) hudManager.reportVariables();

  for (OwnedPiece& piece : spawnPieces) renderer->release(piece.mesh);
  for (auto& gpuMesh : gpuMeshes) renderer->release(gpuMesh);
  ambientEffects.release(*renderer);
  ownImpacts.release(*renderer);
  worldView.release(*renderer);
  if (remote != nullptr) {
    std::printf("  our rounds' impacts: %d, marks made %d\n", ownImpacts.impacts(), ownImpacts.marks());
    std::printf("  weapon sounds played: %d (reverb %s)\n", weaponSounds.played(),
                environmentAudio.ready() ? "on" : "off");
    std::printf("  first person: rounds fired %u, actions with the fire button %d, "
                "frames with the fire clip %d\n",
                firstPersonView.roundsFired(), firstPersonFireActions,
                firstPersonView.fireClipFrames());
    std::printf("  first-person bundles (frames on top):");
    for (const auto& [bundle, frames] : firstPersonView.bundleFrames()) {
      std::printf(" %s %d", bundle.c_str(), frames);
    }
    std::printf("\n");
    std::printf("  ammunition: %d in the magazine, %d spare, reloads started %d, "
                "frames with the reload clip %d\n",
                firstPersonView.ammo().roundsInMag(), firstPersonView.ammo().spareMags(),
                firstPersonView.reloadsStarted(), firstPersonView.reloadClipFrames());
    std::printf("  bolt action: %d shifts, frames with the shift clip %d\n",
                firstPersonView.shift().shifts(), firstPersonView.shiftClipFrames());
    std::printf("  zoom: level %d of the weapon's %zu, field of view %.3f, changes %d\n",
                firstPersonView.zoom().index(),
                firstPersonView.zoom().levels(),
                firstPersonView.zoom().fov(), firstPersonView.zoomChanges());
  }
  firstPersonView.release(*renderer);
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
    matchVoice.stop(*remote);
  }
  worldView.report();
  menu.release(*renderer);
  if (next != nullptr && !menu.requestedLevel().empty()) {
    *next = args;
    next->levelName = menu.requestedLevel();
    next->loadingMode = menu.requestedMode();
    next->loadingSize = menu.requestedSize();
    next->loadingScreen = true;
    next->hosted = true;
    next->screen.clear();
  }
  return 0;
}


}  // namespace obf2::app
