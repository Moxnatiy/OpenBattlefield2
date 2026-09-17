#include "obf2/hud/manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "obf2/con/interpreter.h"
#include "obf2/core/path.h"
#include "obf2/hud/combat_area.h"
#include "obf2/hud/ingame.h"
#include "obf2/hud/kit_list.h"
#include "obf2/hud/spawn.h"

namespace obf2::hud {
namespace {

// The map's window is a square around the combat area, and the margin is a
// fraction of the picture rather than of the area: 40 texels of the 512-wide map
// on each side. Measured from frame dumps of the original on two levels whose
// terrains differ in size (docs/research/03-frame-dump.md) — "times 1.2", which
// we used before, puts half of Karkand's map outside the node.
constexpr float kMapMargin = 40.0f / 512.0f;

}  // namespace

bool Manager::load(FileSystem& files, engine::Engine& engine, const level::Level* level,
                   const game::Registry& registry, const Setup& setup, Hooks hooks) {
  files_ = &files;
  engine_ = &engine;
  level_ = level;
  registry_ = &registry;
  setup_ = setup;
  hooks_ = std::move(hooks);
  screen_.width = setup.screenWidth;
  screen_.height = setup.screenHeight;

  font_ = font::loadFont(files, "Fonts/800/dynamicText_13");
  if (!font_.valid) return false;

  // The dictionary: without it the HUD shows keys ("HUD_TEXT_MENU_SCORE_ROUNDSWON")
  // instead of text. In the menu boot loads it, while in combat nobody loaded it —
  // hence the keys on screen.
  engine.loadLexicon(files);

  con::Interpreter hudInterpreter(files,
                                  [&](const con::Command& command) { tree_.feed(command); });
  hudInterpreter.runFile("Menu/HUD/HudSetup/HudSetupMain.con");

  // The HUD's animation system is the file `Menu/Ingame`, not code: it holds the
  // corner regions with their variable bindings, the show conditions and the
  // actions that move those variables (obf2/meme/graph.h).
  if (const auto memeData = files.read("Menu/Ingame")) {
    std::string memeError;
    if (graph_.load(*memeData, &memeError)) {
      std::printf("  the Ingame graph: nodes %zu, variables %zu\n", graph_.file().objects().size(),
                  graph_.variables().all().size());
    } else {
      std::printf("  the Ingame graph was not read: %s\n", memeError.c_str());
    }
  }
  // Link the tree: until then the nodes' coordinates stay relative to their parent
  // and the HUD scatters over the screen.
  tree_.finish();

  // The control layout comes from the game's data. We ask about an action, and
  // which key it is `Settings/Controls.con` decides.
  con::Interpreter controlInterpreter(
      files, [&](const con::Command& command) { controls_.feed(command); });
  controlInterpreter.runFile("Settings/Controls.con");

  // The map's rectangles come from the data (`setMiniPos`/`setMaxiSize` and the
  // rest); the animation takes them from the assembled tree so as not to parse twice.
  for (const Node& node : tree_.nodes()) {
    if (node.type == NodeType::Map || node.type == NodeType::MiniMap) {
      map_.takeRects(node);
      break;
    }
  }

  // The combat HUD is state 0.
  applyState(0);

  // ToggleScore is the scoreboard's "Players" tab. That it is the default is
  // visible in the binary: the flag field (Scoreboard+0x365) has exactly one
  // constant store, `movb $0x1, 0x365(%esi)` at 0x7a48f7.
  variables_["ToggleScore"] = true;
  variables_["ToggleSquads"] = false;
  variables_["ToggleManage"] = false;

  // Source not found: CPInterfaceEnabled (the HUD object's field 0xa8) is written
  // by many places in the game, and which of them is ours is not established.
  // Without it the capture points' bars are not visible. Debt.
  variables_["CPInterfaceEnabled"] = true;

  // The bar across the top of the spawn screen — `SpawnInfo` in
  // `HudElementsSpawn.con`: the plate `TopMiddleBar` at 250,0 270x19 and the
  // caption `TimeToSpawn` on `SpawnInfoString` over it. The caption is chosen by
  // `HudInformationLayer`'s per-frame update (`BF2.exe` 0x4668d0) out of four keys
  // by the HUD's state and a list it keeps: the spawn screen with nothing chosen
  // takes `selectspawnpoint`, which is what the original draws; the other three
  // are debt, because what that list holds is not established. `SpawnInfoShow` is
  // set per frame, see `updateVariables`.
  strings_["SpawnInfoString"] = "HUD_CENTERINFOBOX_selectspawnpoint";

  spawnScreen_.mutableChoice().kit = setup.kit;
  spawnScreen_.mutableChoice().team = setup.team == 2 ? 2 : 1;
  bindCommands(engine.console());

  // The plates' alpha. This is not our invention and not zero: BF2.exe takes the
  // alpha from the player's profile (GeneralSettings.setHUDTransparency /
  // setMinimapTransparency, 204 by default), multiplies it by 1/255 — the constant
  // at 0x8a4a64 — and puts it into the variables MenuBackgroundAlpha and
  // MenuMapAlpha (0x4b68d3 and 0x4b6907).
  backgroundAlpha_ = static_cast<float>(engine.settings().general.hudTransparency) / 255.0f;
  alpha_["MenuBackgroundAlpha"] = backgroundAlpha_;
  alpha_["MenuMapAlpha"] =
      static_cast<float>(engine.settings().general.minimapTransparency) / 255.0f;

  buildContexts(engine);
  buildMapWindow(files, level, registry);
  applySpawnState();
  bakeKeyScreens();

  // The live nodes: everything that takes its value from a variable we can fill in.
  for (const Node& node : tree_.nodes()) {
    const bool ticketText = node.group == "TicketInfo" && (node.textVariable ==
                                                               "FriendlyTicketsString" ||
                                                           node.textVariable ==
                                                               "EnemyTicketsString");
    const bool cpBar = node.type == NodeType::Bar && node.group == "CPInformationItems" &&
                       !node.valueVariable.empty();
    // The caption in the middle of the screen: while the round waits for players,
    // the text in it appears and disappears, so it must not be baked in advance.
    const bool centreMessage = node.textVariable == "DisconnectMessage";
    // The compass: the only node in the data with `setPictureNodeRotateVariable`.
    const bool rotating = !node.rotateVariable.empty();
    // The map: its window follows the player and the zoom, so it must not be baked
    // in advance either.
    const bool mapNodeItself = node.type == NodeType::Map || node.type == NodeType::MiniMap;
    if (!ticketText && !cpBar && !centreMessage && !rotating && !mapNodeItself) continue;
    dynamic_.push_back(DynamicNode{&node, {}, -1.0f, 0.0f, false});
  }

  // The live nodes are not baked into the shared geometry: otherwise a second,
  // frozen one would remain under the turning compass.
  context_.skipNode = [this](const Node& node) {
    for (const DynamicNode& live : dynamic_) {
      if (live.node == &node) return true;
    }
    return false;
  };

  std::printf("  HUD: %zu nodes in the tree, live captions %zu\n", tree_.nodes().size(),
              dynamic_.size());

  // The commands we cannot do yet. The game is a stream of commands, so the most
  // useful thing is to see exactly what arrived and was left without a handler.
  if (tree_.unknownCommands() > 0) {
    std::printf("  HUD: unimplemented %lld commands, unique %zu\n", tree_.unknownCommands(),
                tree_.unknownByName().size());
    int shown = 0;
    for (const auto& [name, count] : tree_.unknownByName()) {
      if (shown++ >= 8) break;
      std::printf("    no handler: %-40s x%d\n", name.c_str(), count);
    }
    if (tree_.unknownByName().size() > 8) std::printf("    ... the rest is command_audit\n");
  }

  ready_ = true;
  return true;
}

void Manager::bindCommands(engine::Console& console) {
  // A button in the HUD has no logic of its own: it runs a console command from
  // `setButtonNodeConCmd` (docs/functions/hud-commands.md). The screen's own
  // commands are the module's, the one the test covers (spawn_interface.h).
  spawnScreen_.bind(
      console,
      [this](int team, int kit, int group) {
        return hooks_.requestSpawn ? hooks_.requestSpawn(team, kit, group) : false;
      },
      [this]() {
        if (hooks_.commitSuicide) hooks_.commitSuicide();
      });
  console.bind("hudItems.setBool", [this](const con::Command& command) {
    // `hudItems.setBool <name> <0|1>` — this is how the interface turns its own
    // flags on, SetSpawnPoint among them.
    if (command.args.size() >= 2) {
      variables_[std::string(command.argStr(0))] = command.argInt(1).value_or(0) != 0;
      spawnDirty_ = true;
    }
  });
  // `openbf2.spawnAt <group number>` — ask to spawn straight by the group number
  // the server named (`CreateSpawnGroupEvent`, type 57). Ours, not the engine's:
  // the spawn circles have no nodes in the data, the map catches them itself, so
  // the game has no console name for them. It is needed for the checks.
  console.bind("openbf2.spawnAt", [this](const con::Command& command) {
    const int group = command.argInt(0).value_or(0);
    if (!hooks_.spawnAtGroup || group <= 0) return;
    std::printf("  spawn screen: a direct spawn in group %d\n", group);
    hooks_.spawnAtGroup(spawnScreen_.choice().team, spawnScreen_.choice().kit, group);
    spawnScreen_.setRequested(true);
  });
  // `openbf2.toggleMap [0|1]` — the same as the map key. Ours too: in the original
  // the big map is governed only by the `c_GIMapSize` action from the layout, which
  // has no console name.
  console.bind("openbf2.toggleMap", [this](const con::Command& command) {
    bigMap_ = command.args.empty() ? !bigMap_ : command.argInt(0).value_or(0) != 0;
    std::printf("  map: the big presentation %s\n", bigMap_ ? "on" : "off");
  });
  // `MiniMap.setZoom <index>` — the engine's command: in the data it hangs on the
  // `MapZoom` button (HudElementsMapMenu.con), and in the client 0x57a97e writes
  // the number straight into the map node's field +0x6d0. With no argument — the
  // next zoom in a cycle, because that is how the button behaves.
  console.bind("MiniMap.setZoom", [this](const con::Command& command) {
    const int next = command.args.empty() ? (map_.zoomIndex() + 1) % kMapZoomLevels
                                          : command.argInt(0).value_or(0);
    map_.setZoomIndex(next);
    std::printf("  map: zoom %d\n", map_.zoomIndex());
  });
  // The scoreboard's three tabs. The buttons in the data run
  // `scoreboard.setToggleShow 0|1|2` (`HudElementsScoreboard.con`), and the three
  // flags they pick between are neighbours in the Scoreboard object —
  // `ToggleSquads` +0x364, `ToggleScore` +0x365, `ToggleManage` +0x366
  // (docs/functions/hud-variables.md). Exactly one is on at a time.
  console.bind("scoreboard.setToggleShow", [this](const con::Command& command) {
    const int tab = command.argInt(0).value_or(0);
    variables_["ToggleScore"] = tab == 0;
    variables_["ToggleSquads"] = tab == 1;
    variables_["ToggleManage"] = tab == 2;
    dirty_ = true;
    std::printf("  scoreboard: tab %d\n", tab);
  });
  console.bind("sound.playSound", [](const con::Command&) {});
}

FontRef Manager::fontFor(std::string_view style) {
  // Every node's font is the one named in setTextNodeStyle. In the data the path
  // is written as "Fonts/hudFontLocalBold_9.dif" (sometimes with a backslash).
  std::string key(style);
  for (char& c : key) {
    if (c == '\\') c = '/';
  }
  if (key.size() > 4 && key.compare(key.size() - 4, 4, ".dif") == 0) key.resize(key.size() - 4);
  const auto cached = fonts_.find(key);
  if (cached != fonts_.end()) {
    return FontRef{cached->second.valid ? &cached->second.font : nullptr, cached->second.atlasPath};
  }
  const std::size_t slash = key.rfind('/');
  const std::string dir = slash == std::string::npos ? std::string() : key.substr(0, slash + 1);
  const std::string name = slash == std::string::npos ? key : key.substr(slash + 1);
  // Some of the fonts lie in language directories rather than in the root: for
  // instance StandardTextBold_15 exists only as English/StandardTextBold_15.
  //
  // The order is exactly this: the language directory first, and **without** the
  // `800` subdirectory. The original's frame dump at 800x600 shows a kit's caption
  // 79.2 wide at a height of 11, while the `800` set would give 60.3 by 9 — because
  // there the point size is 13 against 16 in the language directory's root. So
  // `800` is not meant for 800x600, as the name suggested.
  font::LoadedFont loaded = font::loadFont(*files_, dir + "English/" + name);
  if (!loaded.valid) loaded = font::loadFont(*files_, dir + "English/800/" + name);
  if (!loaded.valid) loaded = font::loadFont(*files_, key);
  if (!loaded.valid) loaded = font::loadFont(*files_, dir + "800/" + name);
  const auto placed = fonts_.emplace(key, std::move(loaded)).first;
  return FontRef{placed->second.valid ? &placed->second.font : nullptr, placed->second.atlasPath};
}

void Manager::buildContexts(engine::Engine& engine) {
  context_.localize = [&engine](std::string_view key) { return engine.lexicon().text(key); };
  context_.fontFor = [this](std::string_view style) { return fontFor(style); };
  context_.isVisible = [this](std::string_view variable) {
    if (variable == "1") return true;
    const auto found = variables_.find(std::string(variable));
    return found != variables_.end() && found->second;
  };
  context_.variableValue = [this](std::string_view variable) -> float {
    // Numeric and boolean variables live in different dictionaries, while a show
    // condition asks for both — see obf2/hud/states.h.
    return showValue(variables_, values_, variable);
  };
  // The alpha: the wide plates under the health and ammo bars (400x39,
  // healthBackground.tga and ammoBackground.tga) hang on exactly it, and in combat
  // it is zero — in the game only a narrow squad strip, 142 units, is visible.
  context_.variableAlpha = [this](std::string_view variable) -> std::optional<float> {
    // The left region's four alphas are driven by its state machine
    // (obf2/hud/bottom_left.h, from BF2.exe 0x78b600). They are what separates the
    // region's two halves: on foot the health bars are visible, in a vehicle the
    // vehicle bars.
    if (variable == "BottomLeftHealthAlpha") return bottomLeft_.healthAlpha;
    if (variable == "BottomLeftVehicleAlpha") return bottomLeft_.vehicleAlpha;
    if (variable == "BottomLeftHealthFadedAlpha") return bottomLeft_.healthFadedAlpha;
    if (variable == "BottomLeftVehicleFadedAlpha") return bottomLeft_.vehicleFadedAlpha;
    // On the right there is one alpha, also from the graph. Its pair
    // `BottomRightFadedAlpha` (field +0x14) we do not compute: on the left the
    // formula is visible at the end of 0x78b600, while on the right the place that
    // computes it is **a source not found**.
    if (variable == "BottomRightAlpha") return bottomRightAlpha_;
    const auto found = alpha_.find(std::string(variable));
    if (found == alpha_.end()) return std::nullopt;
    return found->second;
  };
  context_.variableText = [this](std::string_view variable) -> std::string_view {
    const auto found = strings_.find(std::string(variable));
    return found == strings_.end() ? std::string_view{} : std::string_view(found->second);
  };
  // The show effects are seen by the geometry builder itself: alpha multiplies the
  // alpha, move shifts the rectangle.
  context_.showState = [this](const Node& node) { return animator_.state(node); };

  dynamicContext_ = context_;
  spawnContext_ = context_;
  spawnContext_.isVisible = [this](std::string_view variable) {
    if (variable == "1" || variable == "SpawnShow") return true;
    const auto found = variables_.find(std::string(variable));
    return found != variables_.end() && found->second;
  };
  spawnContext_.showState = [this](const Node& node) { return animator_.state(node); };
}

void Manager::buildMapWindow(FileSystem& files, const level::Level* level,
                             const game::Registry& registry) {
  // The level's map picture is not set by the HUD: BF2.exe has a template for it
  // `Levels/%s/Hud/Minimap/ingameMap.tga`.
  if (!setup_.levelName.empty()) {
    context_.mapTexture = "Levels/" + setup_.levelName + "/Hud/Minimap/ingameMap.tga";
  }
  const auto gameplay = level != nullptr
                            ? level::loadGameplayObjects(files, level->name, "gpm_cq", 16)
                            : std::optional<level::GameplayObjects>{};
  if (!gameplay || gameplay->combatArea.empty()) {
    spawnContext_ = context_;
    return;
  }

  float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
  gameplay->combatArea.bounds(minX, maxX, minZ, maxZ);
  const float world = static_cast<float>(level != nullptr ? level->primary.size - 1 : 1024) *
                      (level != nullptr ? level->primary.scale.x : 2.0f);
  const float half = (std::max(maxX - minX, maxZ - minZ) + world * kMapMargin) * 0.5f;
  const float centerX = (minX + maxX) * 0.5f;
  const float centerZ = (minZ + maxZ) * 0.5f;
  // The picture is oriented so that z grows upwards: v = (1024 - z) / 2048.
  const auto toU = [&](float x) { return (x + world * 0.5f) / world; };
  const auto toV = [&](float z) { return (world * 0.5f - z) / world; };
  context_.mapU0 = toU(centerX - half);
  context_.mapU1 = toU(centerX + half);
  context_.mapV0 = toV(centerZ + half);
  context_.mapV1 = toV(centerZ - half);
  context_.mapWorldSize = world;
  mapBaseCentreU_ = (context_.mapU0 + context_.mapU1) * 0.5f;
  mapBaseCentreV_ = (context_.mapV0 + context_.mapV1) * 0.5f;
  mapBaseHalfU_ = (context_.mapU1 - context_.mapU0) * 0.5f;
  mapBaseHalfV_ = (context_.mapV1 - context_.mapV0) * 0.5f;
  std::printf("  map: combat area %.0f..%.0f / %.0f..%.0f, visible u %.3f..%.3f v %.3f..%.3f\n",
              minX, maxX, minZ, maxZ, context_.mapU0, context_.mapU1, context_.mapV0,
              context_.mapV1);

  // The red hatch over everything outside the combat area. The square it covers is
  // the **uncut** one — the same square the crop is built from, before the map's
  // edge takes a bite out of it — because the original's overlay covers the map
  // node whole while its picture does not.
  if (const auto bytes = files.read(normalizeAssetPath(kCombatAreaSource))) {
    std::string textureError;
    if (const auto hatch = texture::loadImage(*bytes, &textureError)) {
      const WorldSquare square{centerX - half, centerZ - half, centerX + half, centerZ + half};
      combatArea_ = buildCombatAreaOverlay(*hatch, gameplay->combatArea.points, square);
      context_.combatAreaTexture = std::string(kCombatAreaTexture);
      std::printf("  map: combat-area hatch over %.0f..%.0f / %.0f..%.0f, %zu points\n", square.minX,
                  square.maxX, square.minZ, square.maxZ, gameplay->combatArea.points.size());
    } else {
      std::printf("  map: the combat-area hatch did not read: %s\n", textureError.c_str());
    }
  }

  // The capture points: the position, the team and the name's key come from the
  // level — exactly what the server sees. The markers themselves are assembled by
  // `applySpawnState`, because they depend on the player's team.
  controlPoints_ = gameplay->controlPoints;
  // The main HUD is baked once, so the flags for its minimap are assembled right
  // here. There are no selection circles there — they exist only on the spawn screen.
  for (const auto& point : controlPoints_) {
    Context::MapMarker marker;
    marker.worldX = point.position.x;
    marker.worldZ = point.position.z;
    marker.label = point.nameKey;
    marker.texture =
        controlPointIcon(point.team == 0 ? "" : (level != nullptr ? level->teamNames[point.team]
                                                                 : std::string{}),
                         point.unableToChangeTeam);
    context_.mapMarkers.push_back(std::move(marker));
  }
  std::printf("  map: capture points %zu\n", controlPoints_.size());

  // What stands on the map besides the flags. Both kinds come out of the objects'
  // own templates rather than out of the HUD's data:
  //
  //   * a vehicle spawner shows the vehicle it issues, 16x16, from that template's
  //     `vehicleHud.miniMapIcon`;
  //   * anything with a `StrategicObject` component shows 19x19, from its
  //     `StrategicObject.intactIcon` — bridges, mobile radars, the air control
  //     tower (the UAV), the artillery pieces. The component also carries a
  //     `destroyedIcon`, which we do not use yet: nothing tells us an object has
  //     been destroyed.
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

  for (const auto& spawner : gameplay->spawners) {
    // Which side's vehicle stands there is the point's business; where the point
    // is neutral or unknown we take whichever template the spawner lists first,
    // because the icon is the same shape either way.
    const auto* point = gameplay->controlPoint(spawner.controlPointId);
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
    // A spawner can put a strategic object on the field rather than a vehicle —
    // the mobile radars, the artillery pieces. Those show the strategic icon at
    // its own size, which is why the original's map has a `Radar` and two `AirDef`
    // on it and no vehicle icon in their place.
    std::string icon = iconOf(vehicle, "StrategicObject", "intacticon");
    float size = 19.0f;
    if (icon.empty()) {
      icon = iconOf(vehicle, "VehicleHud", "minimapicon");
      size = 16.0f;
    }
    if (icon.empty()) continue;
    assetMarkers_.push_back(
        Context::MapMarker{spawner.position.x, spawner.position.z, std::move(icon), {}, size});
  }
  if (level != nullptr) {
    for (const auto& object : level->objects) {
      std::string icon = iconOf(object.templateName, "StrategicObject", "intacticon");
      if (icon.empty()) continue;
      assetMarkers_.push_back(
          Context::MapMarker{object.position.x, object.position.z, std::move(icon), {}, 19.0f});
    }
  }
  context_.mapMarkers.insert(context_.mapMarkers.end(), assetMarkers_.begin(), assetMarkers_.end());
  std::printf("  map: vehicles and assets %zu\n", assetMarkers_.size());

  // The spawn screen's context is the general one plus its own gate; it is copied
  // after the map's window and markers are known.
  Context spawn = context_;
  spawn.isVisible = spawnContext_.isVisible;
  spawn.showState = spawnContext_.showState;
  spawnContext_ = std::move(spawn);
  dynamicContext_ = context_;
}

void Manager::applyState(int state) {
  // The map changes its target by the state number — verbatim 0x777dc0.
  map_.applyState(state);
  // A transition rather than "set the state": in the game the first switch goes on
  // the old state, the second on the new one (0x786260).
  if (hud::applyState(variables_, statePrevious_, state)) dirty_ = true;
  // The spawn screen is baked geometry we keep, and its nodes hang on the same
  // variables: a state that changes them changes what it should be showing. The
  // engine has no such problem — it walks the tree every frame — so this is our
  // cache to invalidate, and forgetting it is what left the screen empty.
  if (statePrevious_ != state) spawnDirty_ = true;
  statePrevious_ = state;
}

void Manager::updateVariables(bool hasPlayer, bool mapFullSize) {
  // The HUD's derived variables. In the game they are written not by a list at
  // startup but by two per-frame functions: 0x466930 (the map's size and what
  // follows from it) and 0x78d0f0 (the combat set by the current player). That is
  // exactly why in the original no health or ammo bars are visible behind the
  // spawn screen: there is no player yet, and 0x78d2d9 clears the whole set.
  if (applyDerived(variables_, WorldView{hasPlayer, mapFullSize})) dirty_ = true;
  // The left region's mode is set by 0x78b870: "health" when there is a player and
  // "hide" when there is none. "Vehicle" is when the controlled object is not a
  // soldier; we are always a soldier for now, so we do not turn it on.
  bottomLeftMode_ = hasPlayer ? BottomLeftMode::Health : BottomLeftMode::Hidden;
  // On the right the switch is the same as on the left: there is a player — show.
  bottomRightShow_ = hasPlayer;
  bottomRightShownX_ = kBottomRightShownX;
  // The bar across the top belongs to the spawn flow. `SpawnInfoShow` (+0x1d3) is
  // written by 0x4668d0 as `state != 11 && state != 12` — but the whole block sits
  // behind an outer branch at 0x467278 that we have not read, so in combat the
  // condition is **not measured**. What is measured is the spawn screen: the
  // original draws the bar there (docs/research/spawn-screen-named.md).
  variables_["SpawnInfoShow"] = !hasPlayer;
}

void Manager::applySpawnState() {
  const int selectedTeam = spawnScreen_.choice().team;
  const int selectedKit = spawnScreen_.choice().kit;
  const bool membersTab = spawnScreen_.choice().membersTab;
  const auto teamName = [this](int team) -> std::string {
    // The team's side name comes from the level itself: `gameLogic.setTeamName 1
    // "CH"`. Across all 22 levels the set is exactly CH, EU, MEC, US, and the icon
    // directories in Menu_client.zip are called the same.
    if (level_ == nullptr || team < 0 || team > 2) return {};
    return level_->teamNames[team];
  };
  // The conversions from a side's name into keys and paths live in
  // `obf2/hud/spawn.h` together with their test.
  const auto teamLabel = [&](int team) { return armyLabelKey(teamName(team)); };
  const auto teamFlag = [&](int team) { return teamFlagIcon(teamName(team)); };

  // The map's markers depend on the team, so we assemble them every time. A flag
  // stands on every point, while a spawn selection circle stands only where the
  // point is held by **our** team: spawning at another team's or a neutral one is
  // not allowed.
  spawnContext_.mapMarkers.clear();
  spawnContext_.spawnMarkers.clear();
  std::vector<int> markerPoints;
  for (const auto& point : controlPoints_) {
    Context::MapMarker marker;
    marker.worldX = point.position.x;
    marker.worldZ = point.position.z;
    marker.label = point.nameKey;
    marker.texture = controlPointIcon(point.team == 0 ? "" : teamName(point.team),
                                      point.unableToChangeTeam);
    spawnContext_.mapMarkers.push_back(std::move(marker));
    if (point.team == selectedTeam) {
      const bool chosen = static_cast<int>(spawnContext_.spawnMarkers.size()) ==
                          spawnScreen_.choice().marker;
      spawnContext_.spawnMarkers.push_back(
          Context::SpawnMarker{point.position.x, point.position.z, chosen});
      markerPoints.push_back(point.id);
    }
  }
  spawnScreen_.setMarkerPoints(std::move(markerPoints));
  // The vehicles and the strategic objects come after the flags, the way the
  // original's batch has them.
  spawnContext_.mapMarkers.insert(spawnContext_.mapMarkers.end(), assetMarkers_.begin(),
                                  assetMarkers_.end());
  variables_["Team1Selected"] = selectedTeam != 2;
  variables_["Team2Selected"] = selectedTeam == 2;
  // The tabs' captions and flags. In the data they are on the variables
  // Team1NameString / Team1FlagIconPathString (HudElementsSpawn.con), while in the
  // binary one and the same 0x787260 fills them in.
  for (int team = 1; team <= 2; ++team) {
    const std::string index = std::to_string(team);
    strings_["Team" + index + "NameString"] =
        std::string(engine_->lexicon().text(teamLabel(team)));
    strings_["Team" + index + "FlagIconPathString"] = teamFlag(team);
  }
  // The same function also sets the "ours/theirs" pair: its first argument is the
  // player's team, the second the opposite one.
  strings_["FriendlyFlagIconPathString"] = teamFlag(selectedTeam);
  strings_["EnemyFlagIconPathString"] = teamFlag(selectedTeam == 2 ? 1 : 2);
  // The scoreboard's two headers name the sides with the same pair
  // (`FriendlyTeamNameString` +0x52c, `EnemyTeamNameString` +0x530). The rounds
  // won (`FriendlyTeamWinsString`) we do not count — debt.
  strings_["FriendlyTeamNameString"] = std::string(engine_->lexicon().text(teamLabel(selectedTeam)));
  strings_["EnemyTeamNameString"] =
      std::string(engine_->lexicon().text(teamLabel(selectedTeam == 2 ? 1 : 2)));
  // KitsShow / MembersShow is switched by SpawnManager.toggleMembers — a reversed
  // command from hud-commands.md.
  variables_["KitsShow"] = !membersTab;
  variables_["MembersShow"] = membersTab;
  // The chosen kit is a consequence of spawnManager.setPlayerKit, also reversed.
  for (int slot = 0; slot < 7; ++slot) {
    variables_["PlayerKitIcon" + std::to_string(slot) + "SelectShow"] = slot == selectedKit;
  }

  // The seven kit rows. Everything a row shows the engine pours into these
  // variables every frame out of the kit's own ObjectTemplate —
  // `HudInformationLayer`, 0x468510 (docs/functions/hud-kits.md). The kit of a row
  // is named by the level: `gameLogic.setKit <team> <row> <kit> <soldier>`.
  for (int slot = 0; slot < level::Level::kKitsPerTeam; ++slot) {
    const std::string index = std::to_string(slot);
    const std::string& kitName = level_ != nullptr ? level_->kits[selectedTeam][slot]
                                                   : emptyKitName_;
    const KitRow row = registry_ != nullptr ? buildKitRow(*registry_, kitName) : KitRow{};
    // `Kit<N>Show` is `KitsShow` for every row the kit manager answers for — the
    // engine writes the layer's own flag into the row's flag at the end of each
    // pass (0x468510). A row the level named no kit for stays off.
    variables_["Kit" + index + "Show"] = !membersTab && !row.kitTemplate.empty();
    strings_["KitName" + index + "String"] = row.nameKey;
    strings_["KitIcon" + index + "Path"] = row.icon;
    strings_["KitWeaponIcon" + index + "Path"] = row.weaponIcon;
    strings_["KitAltWeaponIcon" + index + "Path"] = row.altWeaponIcon;
    values_["Kit" + index + "SprintAbility"] = row.sprintAbility;
    // The unlock's picture is shown either way; the arrow says whether the player
    // owns it. We have no profile and no unlocks, so the arrow is off — which is
    // what the original drew for the profile the dump was taken with.
    variables_["KitUnlock" + index + "Show"] = row.unlock;
    variables_["KitUnlockArrow" + index + "Show"] = false;
    values_["Kit" + index + "UnlockBlinkAlpha"] = 0.0f;
    for (std::size_t icon = 0; icon < kMaxAbilityIcons; ++icon) {
      const std::string at = index + "AbilityIcon" + std::to_string(icon);
      const bool has = icon < row.abilityIcons.size();
      variables_["Kit" + at + "Show"] = has;
      strings_["Kit" + at + "PathString"] = has ? row.abilityIcons[icon] : std::string{};
    }
  }
}

std::vector<DrawPiece> Manager::buildIngame() {
  const auto layers = ingameLayers(graph_, bottomLeft_.x, bottomRightX_);
  for (const char* root : {"Global", "BottomLeftAnimate", "BottomLeftStatic", "BottomRightAnimate",
                           "BottomRightStatic"}) {
    updateAnimator(tree_, root, animator_, context_);
  }
  auto pieces = ::obf2::hud::buildIngame(
      tree_, layers, font_.font, font_.atlasPath, screen_, context_,
      [this](const IngameLayer& layer, const std::vector<DrawPiece>& made) {
        if (hooks_.reportRects) hooks_.reportRects(layer.group.c_str(), made);
        if (!ingameReported_) {
          std::printf("  HUD: layer %-20s corner %.0f %.0f, pieces %zu\n", layer.group.c_str(),
                      layer.x, layer.y, made.size());
        }
      });
  if (hooks_.reportRects) hooks_.reportRects("Global", pieces);
  ingameReported_ = true;
  return pieces;
}

std::vector<DrawPiece> Manager::buildSpawn() {
  applySpawnState();
  for (const char* root : {"SpawnMenu", "MapSplit", "TopLayer"}) {
    updateAnimator(tree_, root, animator_, spawnContext_);
  }
  tree_.setMapView(MapView::Maxi);
  auto built = buildTree(tree_, "SpawnMenu", font_.font, font_.atlasPath, screen_, spawnContext_);
  auto mapPieces = buildTree(tree_, "MapSplit", font_.font, font_.atlasPath, screen_, spawnContext_);
  const std::size_t mapCount = mapPieces.size();
  for (auto& piece : mapPieces) built.push_back(std::move(piece));
  // TopLayer is a real region from the data (`createSplitNode TopLayer TopLayerHud`
  // in GeneralHudSettings.con), and it is where the DONE and SUICIDE buttons live:
  // MapButtons -> DoneButton 666 539 124 17.
  for (auto& piece :
       buildTree(tree_, "TopLayer", font_.font, font_.atlasPath, screen_, spawnContext_)) {
    built.push_back(std::move(piece));
  }
  if (hooks_.reportRects) hooks_.reportRects("SpawnMenu", built);
  tree_.setMapView(MapView::Mini);
  // The map's rectangle was just moved — let the combat frame put its own back,
  // the one the animation computed.
  mapRectStale_ = true;
  std::printf("  HUD: the spawn screen rebuilt, pieces %zu (map %zu), circles %zu\n", built.size(),
              mapCount, spawnContext_.spawnMarkers.size());
  return built;
}

void Manager::bakeKeyScreens() {
  // Every key-held screen is unlocked by exactly **one** variable — the one that
  // stands on its root in the game's data:
  //
  //   Scoreboard -> ScoreboardShow      RadioRose -> RadioInterfaceShow
  //   SpawnMenu  -> SpawnShow           MapMenu   -> MapMenuShow
  //
  // Until now we treated everything unfamiliar as on for these screens — and on
  // the scoreboard both tabs, both sets of captions and the server data block came
  // out together, overlapping.
  struct KeyScreenSetup {
    const char* group;
    const char* action;
    const char* gate;
    // The second branch this screen adds to itself. The map lives in the main tree
    // (MapSplit under IngameHud) rather than inside the spawn screen — on the
    // screen it simply switches to the big presentation.
    const char* extraRoot = nullptr;
    MapView mapView = MapView::Mini;
    int state = -1;
    bool heldByKey = true;
  };
  for (const KeyScreenSetup& setup : {
           KeyScreenSetup{"Scoreboard", "c_GIShowScoreboard", "ScoreboardShow", nullptr,
                          MapView::Mini, 9, true},
           KeyScreenSetup{"RadioRose", "c_GIRadioComm", "RadioInterfaceShow"},
           KeyScreenSetup{"MapMenu", "c_GIMapSize", "MapMenuShow"},
       }) {
    Context keyContext = context_;
    const std::string gate = setup.gate;
    keyContext.isVisible = [this, gate](std::string_view variable) {
      if (variable == "1" || variable == gate) return true;
      const auto found = variables_.find(std::string(variable));
      return found != variables_.end() && found->second;
    };
    keyContext.variableValue = [this, gate](std::string_view variable) -> float {
      if (variable == gate) return 1.0f;
      const auto found = values_.find(std::string(variable));
      return found == values_.end() ? 0.0f : found->second;
    };
    if (setup.mapView != MapView::Mini) tree_.setMapView(setup.mapView);
    auto built = buildTree(tree_, setup.group, font_.font, font_.atlasPath, screen_, keyContext);
    if (setup.extraRoot != nullptr) {
      for (auto& piece :
           buildTree(tree_, setup.extraRoot, font_.font, font_.atlasPath, screen_, keyContext)) {
        built.push_back(std::move(piece));
      }
    }
    // We assemble the report BEFORE putting the presentation back: otherwise the
    // map node would already be measured as a thumbnail while the big one was
    // baked in.
    if (!built.empty() && hooks_.reportRects) hooks_.reportRects(setup.group, built);
    // We put the thumbnail back: the main HUD is measured with it.
    if (setup.mapView != MapView::Mini) {
      tree_.setMapView(MapView::Mini);
      mapRectStale_ = true;
    }
    if (built.empty()) continue;
    KeyScreen screen;
    screen.group = setup.group;
    screen.action = setup.action;
    screen.state = setup.state;
    screen.heldByKey = setup.heldByKey;
    screen.pieces = std::move(built);
    std::printf("  HUD: screen %-12s on %s (%s), pieces %zu\n", setup.group, setup.action,
                std::string(controls_.key(setup.action)).c_str(), screen.pieces.size());
    keyScreens_.push_back(std::move(screen));
  }
}

bool Manager::dynamicChanged(const DynamicNode& live) const {
  const Node& node = *live.node;
  const bool isMap = node.type == NodeType::Map || node.type == NodeType::MiniMap;
  const bool isRotating = !node.rotateVariable.empty();
  const bool isBar = node.type == NodeType::Bar;
  if (!live.built) return true;
  // A rotation step below which nothing is visible on screen any more: the compass
  // is 192 pixels, so 0.005 radians is half a pixel at the edge. The map is rebaked
  // when its window moved: the threshold is 0.0002 of the world's width, that is
  // less than a minimap pixel.
  if (isMap) {
    return std::abs(context_.mapU0 - live.shownValue) > 0.0002f ||
           std::abs(context_.mapV0 - live.shownAngle) > 0.0002f;
  }
  if (isRotating) {
    const auto found = values_.find(node.rotateVariable);
    const float angle = found == values_.end() ? 0.0f : found->second;
    return std::abs(angle - live.shownAngle) > 0.005f;
  }
  if (isBar) {
    const auto found = values_.find(node.valueVariable);
    const float value = found == values_.end() ? 0.0f : found->second;
    return std::abs(value - live.shownValue) > 0.001f;
  }
  const auto found = strings_.find(node.textVariable);
  return (found == strings_.end() ? std::string{} : found->second) != live.shownText;
}

std::vector<DrawPiece> Manager::buildDynamic(DynamicNode& live) {
  const Node& node = *live.node;
  const bool isMap = node.type == NodeType::Map || node.type == NodeType::MiniMap;
  const bool isRotating = !node.rotateVariable.empty();
  const bool isBar = node.type == NodeType::Bar;

  std::string text;
  float value = 0.0f;
  float angle = 0.0f;
  if (isRotating) {
    const auto found = values_.find(node.rotateVariable);
    angle = found == values_.end() ? 0.0f : found->second;
  } else if (isBar) {
    const auto found = values_.find(node.valueVariable);
    value = found == values_.end() ? 0.0f : found->second;
  } else if (!isMap) {
    const auto found = strings_.find(node.textVariable);
    if (found != strings_.end()) text = found->second;
  }

  live.built = true;
  live.shownValue = isMap ? context_.mapU0 : value;
  live.shownText = text;
  live.shownAngle = isMap ? context_.mapV0 : angle;

  Node copy = node;
  // We take the general context rather than an empty one: otherwise the live
  // captions are drawn with the default font instead of their own
  // (setTextNodeStyle) and without localisation.
  Context single = dynamicContext_;
  if (isMap) {
    // The map's window lives in the general context and changes every frame, while
    // the dynamic one is a snapshot; we carry it over by hand.
    single.mapU0 = context_.mapU0;
    single.mapU1 = context_.mapU1;
    single.mapV0 = context_.mapV0;
    single.mapV1 = context_.mapV1;
  } else if (isRotating) {
    single.variableValue = [angle](std::string_view) { return angle; };
  } else if (isBar) {
    single.variableValue = [value](std::string_view) { return value; };
  } else {
    copy.text = text;
    copy.textVariable.clear();
  }
  if (!isMap && !isRotating && !isBar && text.empty()) return {};
  return buildNode(copy, font_.font, font_.atlasPath, screen_, single);
}

void Manager::mapKey(bool down) {
  // The map key (`c_GIMapSize`, constant 0x23 in BF2.exe's control table at
  // 0x690244) is a toggle, not "hold". In combat it moves the HUD from state 0
  // into state 2, where the data leaves the map alone on screen.
  if (down && !mapKeyWasDown_) bigMap_ = !bigMap_;
  mapKeyWasDown_ = down;
}

void Manager::zoomKey(bool down) {
  // The map's zoom is its own action: `c_GIMapZoom`, control 0x24, right beside
  // `c_GIMapSize`'s 0x23 in the table the game builds at 0x690222.
  //
  // **The engine's handler for that action has not been found**, only the action
  // itself. Cycling through the three levels is ours, and it is debt.
  if (down && !zoomKeyWasDown_) {
    map_.setZoomIndex((map_.zoomIndex() + 1) % kMapZoomLevels);
    std::printf("  map: zoom %d\n", map_.zoomIndex());
  }
  zoomKeyWasDown_ = down;
}

void Manager::animate(float seconds, float yaw, const Vec3f& eye) {
  const float slice = seconds > 0.25f ? 0.25f : seconds;
  animator_.advance(slice);
  if (animator_.animating()) {
    spawnDirty_ = true;
    // **And the combat one too.** Until now only the spawn screen was rebaked, and
    // the combat HUD's nodes with `setNodeInTime` did not move at all.
    dirty_ = true;
  }

  // The corner regions. The division of labour here is exactly as in the game: the
  // client's state machine (0x78b600) reads the current values and writes the
  // **targets**, while the `Menu/Ingame` graph moves them. There is no speed in
  // our code at all — the speeds are in the file.
  const float wasX = bottomLeft_.x;
  const float wasHealth = bottomLeft_.healthAlpha;
  const float wasRightX = bottomRightX_;
  const float wasRightAlpha = bottomRightAlpha_;
  if (graph_.file().root() >= 0) {
    auto& variables = graph_.variables();
    bottomLeft_.x = variables.get("BottomLeft/BottomLeft_XPos");
    bottomLeft_.healthAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha1");
    bottomLeft_.vehicleAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha2");
    bottomLeft_.update(bottomLeftMode_, backgroundAlpha_);
    variables.set("BottomLeft/BottomLeft_nextXPos", bottomLeft_.targetX);
    variables.set("BottomLeft/Alpha/BottomLeft_nextAlpha1", bottomLeft_.targetHealthAlpha);
    variables.set("BottomLeft/Alpha/BottomLeft_nextAlpha2", bottomLeft_.targetVehicleAlpha);
    // The right region travels by the graph too, and exactly as in the game. The
    // machine in the file is five `CullVariableActionNode`
    // (docs/formats/hud-meme-graph.md), while the engine drives only three of its
    // variables, bound to the HUD object's fields (0x7a62c0): `oldXPos` (+0x28) the
    // shown position, `newXPos` (+0x2c) the hidden one, `direction` (+0x18) show or
    // not. The graph does the rest, the right region's alpha included.
    variables.set("BottomRight/BottomRight_oldXPos", bottomRightShownX_);
    variables.set("BottomRight/BottomRight_newXPos", kBottomRightHiddenX);
    variables.set("BottomRight/BottomRight_direction", bottomRightShow_ ? 1.0f : 0.0f);
    graph_.update(slice);
    bottomLeft_.x = variables.get("BottomLeft/BottomLeft_XPos");
    bottomLeft_.healthAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha1");
    bottomLeft_.vehicleAlpha = variables.get("BottomLeft/Alpha/BottomLeft_alpha2");
    bottomLeft_.recomputeFaded(backgroundAlpha_);
    bottomRightX_ = variables.get("BottomRight/BottomRight_XPos");
    bottomRightAlpha_ = variables.get("BottomRight/Alpha/BottomRight_alpha");
  }
  if (bottomLeft_.x != wasX || bottomLeft_.healthAlpha != wasHealth) dirty_ = true;

  // The minimap's compass. 0x751d8b computes the direction as
  // `atan2(direction.x, direction.z)`, and ours is exactly that: zero looks along
  // +Z, so this is simply the look angle. The angle goes into a variable — the
  // compass is drawn by a live node, and it must not be routed through `dirty_`:
  // rebaking the whole HUD every frame is exactly the frame drop one can see.
  mapAngle_.setTarget(yaw * 3.14159265358979323846f / 180.0f);
  mapAngle_.update(slice);
  values_["MinimapDelayedMapAngle"] = mapAngle_.delayed();

  // Where the map looks: the player's position in fractions of the world.
  // 0x751d55 computes it (`(sizeX/2 + playerX) / sizeX`) and hands it to 0x773630,
  // which writes the target +0x740/+0x744.
  if (context_.mapWorldSize > 1.0f) {
    const float world = context_.mapWorldSize;
    map_.setCentre((world * 0.5f + eye.x) / world, (world * 0.5f - eye.z) / world);
  }

  const auto wasSize = map_.size();
  const auto wasPosition = map_.position();
  map_.update(slice);
  const auto position = map_.position();
  const auto size = map_.size();
  const bool moved = size.x != wasSize.x || size.y != wasSize.y || position.x != wasPosition.x ||
                     position.y != wasPosition.y;
  // `setMapRect` drags a `finish()` over all 1659 nodes with it, so we call it only
  // when the rectangle really changed — or when somebody else managed to move it (a
  // spawn screen rebuild sets the big presentation itself, through `setMapView`).
  if (moved || mapRectStale_) {
    mapRectStale_ = false;
    tree_.setMapRect(position.x, position.y, size.x, size.y,
                     map_.minSize() ? MapView::Mini : MapView::Maxi);
    dirty_ = true;
  }

  // The map's window. The thumbnail in the corner follows the player and zooms in
  // by the zoom index; the big map shows the whole combat area. How exactly the
  // engine blends these two centres — 0x773870 takes the weight from +0x694 — has
  // **not been worked out**, so for now there are simply two branches.
  if (mapBaseHalfU_ > 0.0f) {
    const float scale = map_.minSize() ? map_.zoomScale() : 1.0f;
    const float halfU = mapBaseHalfU_ / scale;
    const float halfV = mapBaseHalfV_ / scale;
    const float cu = map_.minSize() ? map_.centre().x : mapBaseCentreU_;
    const float cv = map_.minSize() ? map_.centre().y : mapBaseCentreV_;
    context_.mapU0 = cu - halfU;
    context_.mapU1 = cu + halfU;
    context_.mapV0 = cv - halfV;
    context_.mapV1 = cv + halfV;
  }

  if (bottomRightX_ != wasRightX || bottomRightAlpha_ != wasRightAlpha) dirty_ = true;

  // A choice changed by the screen's own commands (the module marks itself).
  if (spawnScreen_.dirty()) {
    spawnDirty_ = true;
    spawnScreen_.clearDirty();
  }
}

void Manager::clickSpawn(float x, float y, engine::Console& console) {
  // The spawn circles first: the data has no nodes for them, the map catches the
  // mouse itself. On the spawn screen the map is in its big presentation, so the
  // mouse has to be caught in that one: in the thumbnail the node stands elsewhere.
  tree_.setMapView(MapView::Maxi);
  const auto hitSpawn = spawnMarkerAt(tree_, "MapSplit", screen_, spawnContext_, x, y);
  tree_.setMapView(MapView::Mini);
  mapRectStale_ = true;
  if (hitSpawn) {
    spawnScreen_.mutableChoice().marker = static_cast<int>(*hitSpawn);
    spawnDirty_ = true;
    std::printf("  spawn screen: point %d\n", spawnScreen_.choice().marker);
  }
  // The spawn screen's buttons lie in two branches: SpawnMenu itself and TopLayer,
  // where DONE and SUICIDE sit.
  const Node* hit = nullptr;
  for (const char* root : {"SpawnMenu", "TopLayer"}) {
    if (const Node* found = buttonAt(tree_, root, screen_, x, y, &spawnContext_)) hit = found;
  }
  if (hit == nullptr) return;
  // A button has several commands, and each has its own event. In the data there
  // are four: 0 (247 times), 1 (73), 3 (50) and 2 (11). A click owns 0 and 3 — on
  // those hang spawnManager.setPlayerTeam and scoreboard.setToggleShow among
  // others; 1 and 2 are hover and unhover, holding only sounds.
  for (const auto& [event, line] : hit->commands) {
    if (event != 0 && event != 3) continue;
    std::printf("  spawn screen: %s -> %s\n", hit->name.c_str(), line.c_str());
    if (!console.executeLine(line)) {
      std::printf("  spawn screen: a command without a handler — %s\n", line.c_str());
    }
  }
}

void Manager::reportVariables() const {
  // The HUD is a stream of nodes hanging off names, and a name nobody writes is a
  // node that never appears — silently. This is the list of those names, so the
  // debt is a list rather than a feeling. The kind is said because the kinds live
  // in different maps: a show condition is a bool, a bar's fill a float, a caption
  // and a texture path a string.
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
  for (const auto& node : tree_.nodes()) {
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
    // Three maps and one lambda: the corner regions' alphas are computed by their
    // own state machine rather than kept in a map, and `variableAlpha` is where a
    // node asks for them.
    const bool have = variables_.count(name) != 0 || values_.count(name) != 0 ||
                      strings_.count(name) != 0 ||
                      (context_.variableAlpha && context_.variableAlpha(name).has_value());
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

}  // namespace obf2::hud
