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
#include "obf2/hud/strategic_objects.h"
#include "obf2/server/collision_objects.h"

namespace obf2::hud {
namespace {

int collisionPartOf(const game::ObjectTemplate& object) {
  // `ObjectTemplate.collisionPart` (SimpleObjectTemplate +0x78, setter Linux
  // 0x507830); 0 when not set.
  if (const game::Property* part = object.property("collisionPart")) {
    if (const auto value = part->asInt(0)) return *value;
  }
  return 0;
}

// `Object::getPhysicBoundingRadius` (Linux 0x687770) of an object made from a
// template, before anything moves it:
//
//   * the collision mesh's radius (`Object+0x80`, vtable +0xf0 →
//     `CollisionMesh::maxBoundingRadius`), 0 without one. The object's own mesh
//     is the part its template names (`SimpleObject::SimpleObject`, 0x58a860,
//     hands template +0x78 to the CollisionManager); a child without a mesh of
//     its own takes its bundle's mesh at its part when that part is above 0,
//     until the first child that has a mesh of its own
//     (`setChildPartCollisionMeshes`, 0x574760; collision_objects.h);
//   * plus `getBoundingRadiusModifier` (vtable +0x138): 0 for an `Object`
//     (0x4bd530); for a `PlayerControlObject` (0x536af0) its template's float
//     +0x3d0, `ObjectTemplate.boundingRadiusModifierWhenOccupied` (console
//     command at 0x8f1b6e, named at 0x8ca949), 0 in the template's constructor
//     (0x53ba88). Not ported: `ZiplineRope`'s (0x572460);
//   * widened by each child whose own radius is above 0 to that radius plus
//     the length of the child's offset (vtable +0x1e0 children, +0x1c0 offset).
//
// The early 0 for an object whose flag word (+0x8) has bit 0 set is not taken:
// what sets that bit is not established.
float physicRadiusOf(server::CollisionLibrary& meshes, const game::Registry& registry,
                     const game::ObjectTemplate& object, const mesh::CollisionMesh* bundle,
                     bool& bundleStopped, int depth, int rootPart = -1) {
  constexpr int kMaxDepth = 16;  // the guard collision_objects.cpp uses against a cycle
  const mesh::CollisionMesh* own = meshes.meshOf(object);
  const int part = rootPart >= 0 ? rootPart : collisionPartOf(object);
  float radius = 0.0f;
  if (own != nullptr) {
    radius = own->maxBoundingRadius(static_cast<std::size_t>(std::max(part, 0)));
    bundleStopped = true;
  } else if (bundle != nullptr && !bundleStopped && part > 0) {
    radius = bundle->maxBoundingRadius(static_cast<std::size_t>(part));
  }
  if (object.className == "PlayerControlObject") {
    if (const game::Property* modifier = object.property("boundingRadiusModifierWhenOccupied")) {
      if (const auto value = modifier->asFloat(0)) radius += *value;
    }
  }
  if (depth >= kMaxDepth) return radius;
  const mesh::CollisionMesh* childBundle = own != nullptr ? own : bundle;
  bool childStopped = own != nullptr ? false : bundleStopped;
  for (const game::ChildTemplate& child : object.children) {
    const game::ObjectTemplate* childObject = registry.find(child.name);
    if (childObject == nullptr) continue;
    const float childRadius =
        physicRadiusOf(meshes, registry, *childObject, childBundle, childStopped, depth + 1);
    if (childRadius > 0.0f) {
      const float offset = std::sqrt(child.position.x * child.position.x +
                                     child.position.y * child.position.y +
                                     child.position.z * child.position.z);
      radius = std::max(radius, offset + childRadius);
    }
  }
  if (own == nullptr) bundleStopped = childStopped;
  return radius;
}

float physicBoundingRadius(server::CollisionLibrary& meshes, const game::Registry& registry,
                           const game::ObjectTemplate& object, int rootPart) {
  bool stopped = false;
  return physicRadiusOf(meshes, registry, object, nullptr, stopped, 0, rootPart);
}

// atan2 of the object's forward axis x over z: what 0x77e500 reads from its
// matrix (vtable +0x80, row 2 at +0x20 and +0x28), here the placed object's
// own rotation or ready matrix.
float headingOf(const Mat4& transform) {
  const Vec3f forward = transformDirection(transform, Vec3f{0.0f, 0.0f, 1.0f});
  return std::atan2(forward.x, forward.z);
}

// 0x77e500's child walk (`FUN_0071b670`) with the filter at vtable 0x92ff4c
// (0x6273a0): a child whose template answers class id 0x9c42 (0x91c394), the
// Linux server's `CID_RemoteControlledObjectTemplate`.
bool hasRemoteControlledChild(const game::Registry& registry, const game::ObjectTemplate& object,
                              int depth) {
  if (depth >= 16) return false;  // the cycle guard collision_objects.cpp uses
  for (const game::ChildTemplate& child : object.children) {
    const game::ObjectTemplate* found = registry.find(child.name);
    if (found == nullptr) continue;
    if (found->className == "RemoteControlledObject") return true;
    if (hasRemoteControlledChild(registry, *found, depth + 1)) return true;
  }
  return false;
}

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
  squadList_.text = [&engine](const std::string& key) {
    return std::string(engine.lexicon().text(key));
  };
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
  // The crosshair's colour: the information layer's constructor (0x468dd0) puts
  // 1, 1, 0, 1 into its +0x178..+0x184 (0x469294..0x4692b4) — yellow. Who else
  // writes them is not established (docs/functions/hud-crosshair.md).
  alpha_["CrossHairColorRed"] = 1.0f;
  alpha_["CrossHairColorGreen"] = 1.0f;
  alpha_["CrossHairColorBlue"] = 0.0f;
  alpha_["CrossHairColorAlpha"] = 1.0f;

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
    // The scoreboard's lists and the chat: their rows follow the game, so they are
    // rebuilt on their own whenever their contents change.
    const bool scoreList = node.type == NodeType::List &&
                           (scoreboard_.list(node.listData) != nullptr || node.listData == 3);
    if (!ticketText && !cpBar && !centreMessage && !rotating && !mapNodeItself && !scoreList) {
      continue;
    }
    dynamic_.push_back(DynamicNode{&node, {}, -1.0f, 0.0f, -1, false});
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
  spawnScreen_.setKitListener([this](int kit) {
    if (hooks_.selectKit) hooks_.selectKit(kit);
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
  // next zoom in a cycle: the HUD's own `setZoom` without arguments is
  // `cycleZoom` (executor 0x76bcbb → 0x7779a0).
  console.bind("MiniMap.setZoom", [this](const con::Command& command) {
    if (command.args.empty()) {
      map_.cycleZoom();
    } else {
      map_.setZoomIndex(command.argInt(0).value_or(0));
    }
    std::printf("  map: zoom %d\n", map_.zoomIndex());
  });
  // The scoreboard's three tabs. The buttons in the data run
  // `scoreboard.setToggleShow 0|1|2` (`HudElementsScoreboard.con`), and the three
  // flags they pick between are neighbours in the Scoreboard object —
  // `ToggleSquads` +0x364, `ToggleScore` +0x365, `ToggleManage` +0x366
  // (docs/functions/hud-variables.md). Exactly one is on at a time.
  scoreboard_.setText([this](std::string_view key) {
    return std::string(engine_->lexicon().text(key));
  });
  console.bind("scoreboard.setToggleShow", [this](const con::Command& command) {
    const int tab = command.argInt(0).value_or(0);
    scoreboard_.setPage(tab);
    variables_["ToggleScore"] = tab == 0;
    variables_["ToggleSquads"] = tab == 1;
    variables_["ToggleManage"] = tab == 2;
    dirty_ = true;
    std::printf("  scoreboard: tab %d\n", tab);
  });
  console.bind("sound.playSound", [](const con::Command&) {});
  // The map vote (OpenBF2 Menu/HudVoteMap.cpp). `setVoteMapShow 1` opens the
  // list only while the vote manager takes part in voting; with voting off on the
  // server it refuses, as the original's does. With it on, the vote itself (the
  // list of the server's maps and `sendMapIssue`) needs the vote manager we do
  // not have — said, not done.
  console.bind("levelsList.setVoteMapShow", [this](const con::Command& command) {
    const bool show = command.argInt(0).value_or(0) != 0;
    if (show && !votingEnabled_) {
      std::printf("  map vote: voting is off on this server — the list does not open\n");
      return;
    }
    if (show) {
      std::printf("  NOT DONE: map vote — the vote manager (VoteEvent, the server's map list)\n");
      return;
    }
    variables_["VoteMapSelected"] = false;
    dirty_ = true;
  });
  console.bind("levelsList.singleClick", [](const con::Command&) {
    std::printf("  NOT DONE: map vote — a map was clicked; no vote manager yet\n");
  });
  console.bind("hudManager.addFavouriteServer", [](const con::Command& command) {
    std::printf("  NOT DONE: %s favourite server (GeneralSettings' favourites are not kept)\n",
                command.argInt(0).value_or(0) != 0 ? "add" : "remove");
  });
  // A press on a scoreboard list's row (`setListNodeConCmd 1 "Scoreboard.singleClick
  // <side>"`, HudElementsScoreboard.con; `HudScoreboard::singleClick`, OpenBF2).
  // Only the manage page answers: column 4 is the player's voice, 7 and 8 the kick
  // vote (0x7a3bae..0x7a3e5b). The row's player is in its hidden column 10.
  console.bind("Scoreboard.singleClick", [this](const con::Command& command) {
    const int side = command.argInt(0).value_or(2);
    if (scoreboard_.page() != 2) return;
    const int player = scoreboard_.playerAt(side, lastListHit_.row);
    if (player < 0) return;
    if (lastListHit_.column == 4 && hooks_.toggleMute) {
      hooks_.toggleMute(player);
    } else if ((lastListHit_.column == 7 || lastListHit_.column == 8) && hooks_.kickVote) {
      hooks_.kickVote(player, lastListHit_.column);
    }
  });

  // The squad tab's commands (handlers 0x76d8d5..0x76de3e,
  // docs/functions/squad-menu.md). A request goes out through
  // `hooks_.squadAction`.
  const auto squadAction = [this](std::optional<SquadList::Action> action) {
    if (action && hooks_.squadAction) hooks_.squadAction(*action);
    if (const auto state = squadList_.takeRequestedState()) {
      std::printf("  squad menu: HUD state %d asked (hud->vtbl[0x1dc])\n", *state);
    }
    pushSquadVariables();
  };
  console.bind("SquadMenu.singleClickNew",
               [this, squadAction](const con::Command&) { squadAction(squadList_.singleClickNew()); });
  console.bind("SquadMenu.createSquad",
               [this, squadAction](const con::Command&) { squadAction(squadList_.createSquad()); });
  console.bind("SquadMenu.setSquadCreateSelect", [this, squadAction](const con::Command& command) {
    squadList_.setSquadCreateSelect(command.argInt(0).value_or(0) != 0);
    squadAction(std::nullopt);
  });
  console.bind("SquadMenu.squadCreateLockToggle", [this, squadAction](const con::Command&) {
    squadList_.squadCreateLockToggle();
    squadAction(std::nullopt);
  });
  console.bind("SquadMenu.setShowInviteList", [this, squadAction](const con::Command& command) {
    squadList_.setShowInviteList(command.argInt(0).value_or(0) != 0);
    squadAction(std::nullopt);
  });
  // 0x83d270, a bare `ret`: Retail's double clicks do nothing.
  console.bind("SquadMenu.doubleClickNew", [](const con::Command&) {});
  console.bind("SquadMenu.doubleClickInvite", [](const con::Command&) {});
  // The invite list: a checkbox toggles (0x791f50), APPLY sends (0x791fd0).
  console.bind("SquadMenu.singleClickInvite", [this, squadAction](const con::Command&) {
    squadList_.singleClickInvite();
    squadAction(std::nullopt);
  });
  console.bind("SquadMenu.applyInviteList", [this, squadAction](const con::Command&) {
    for (const auto& action : squadList_.applyInviteList()) squadAction(action);
    squadAction(std::nullopt);
  });
  // The name box's button (HudElementsSquadsNew.con:180): focus it for typing
  // (0x78f190 → 0x7b11e0); the frame loop hands it the keyboard.
  console.bind("SquadMenu.setSquadNameSelect", [this](const con::Command&) {
    if (squadList_.setSquadNameSelect()) std::printf("  squad menu: name box focused\n");
  });
}

void Manager::squadWorld(const SquadWorld& world) {
  // `update(visible)` (0x792100) runs while the members tab is up: the spawn
  // screen's SQUAD tab (`MembersShow`, SpawnManager.toggleMembers 1).
  const bool visible = variables_["MembersShow"];
  const int before = squadList_.revision();
  squadList_.update(world, visible);
  // The scoreboard's squads page heads each group with its squad's name; we
  // know our own team's squads' names from the same world.
  std::array<std::string, 10> names;
  for (std::size_t i = 0; i < names.size(); ++i) names[i] = world.squads[i].name;
  const int team = world.team;
  scoreboard_.setSquadNames([names, team](int askedTeam, int squad) {
    if (askedTeam != team || squad < 1 || squad > 9) return std::string();
    return names[static_cast<std::size_t>(squad)];
  });
  if (squadList_.revision() != before) pushSquadVariables();
}

void Manager::pushSquadVariables() {
  // The menu's own variables (init 0x78dcb0), written where the tree reads
  // them. The lists are baked into the spawn screen, so it is rebuilt.
  for (const char* name : {"SquadMemberListShow", "SquadCreateShow", "SquadRenameShow",
                           "SquadCreateLockedShow", "InviteListShow", "IsInSquad", "IsSquadLeader"}) {
    bool value = false;
    if (squadList_.boolVariable(name, value)) variables_[name] = value;
  }
  for (const char* name :
       {"NrOfSquads", "NrOfUnassigned", "SquadCreateTagString", "NewSquadName", "TeamTopLabelString"}) {
    std::string value;
    if (squadList_.stringVariable(name, value)) strings_[name] = value;
  }
  spawnDirty_ = true;
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
  chat_.setLocalize([this](std::string_view key) { return std::string(engine_->lexicon().text(key)); });
  // The chat wraps on the `ChatList` node's fonts (`setListNodeFont … 0` / `1`),
  // the way `BfListBoxData::measureText` asks the node's.
  for (const Node& node : tree_.nodes()) {
    if (node.type != NodeType::List || node.listData != 3) continue;
    const std::vector<std::string> fonts = node.listFonts;
    chat_.setMeasure([this, fonts](int number, std::string_view text) -> float {
      if (number < 0 || static_cast<std::size_t>(number) >= fonts.size()) return 0.0f;
      const FontRef face = fontFor(fonts[static_cast<std::size_t>(number)]);
      return face.font != nullptr ? font::textWidth(*face.font, text, 1.0f) : 0.0f;
    });
    break;
  }
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
  context_.variableNumber = [this](std::string_view variable) -> std::optional<float> {
    const auto found = values_.find(std::string(variable));
    if (found == values_.end()) return std::nullopt;
    return found->second;
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
    if (found != alpha_.end()) return found->second;
    // The ticket bleed's blinkers (`FriendlyTicketBleed`, `EnemyTicketBleed`,
    // HUD/HudSetup/SpawnInterface/HudElementsMap.con:212, 218) are float
    // variables the information layer holds beside the others (updateTickets).
    const auto value = values_.find(std::string(variable));
    if (value == values_.end()) return std::nullopt;
    return value->second;
  };
  context_.variableText = [this](std::string_view variable) -> std::string_view {
    const auto found = strings_.find(std::string(variable));
    return found == strings_.end() ? std::string_view{} : std::string_view(found->second);
  };
  // The show effects are seen by the geometry builder itself: alpha multiplies the
  // alpha, move shifts the rectangle.
  context_.showState = [this](const Node& node) { return animator_.state(node); };
  // A list node's rows (0x7af170): the chat's, the squads' and the
  // scoreboard's four; the others (the commander's, the level list) are not
  // ours yet.
  // Edit nodes: the squad create panel's name box is string 2
  // (`HudElementsSquadsNew.con`, `setEditNodeString 2`); 1 and 3 are not ours.
  context_.editText = [this](int string) -> const std::string* { return squadList_.editString(string); };
  context_.listData = [this](int data) -> const ListData* {
    if (data == 3) return &chat_.list();  // 0x7af170: `[hud+0x144]->getData()`
    if (const ListData* squads = squadList_.list(data)) return squads;
    return scoreboard_.list(data);
  };

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
  combatBounds_ = {minX, maxX, minZ, maxZ};
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
      context_.combatAreaU0 = context_.mapU0;
      context_.combatAreaU1 = context_.mapU1;
      context_.combatAreaV0 = context_.mapV0;
      context_.combatAreaV1 = context_.mapV1;
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
  contextFlagMarkers_ = context_.mapMarkers.size();

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

  // Strategic objects: one icon per holder, not per object, gathered in
  // creation order: the placed objects as the level lists them, then what the
  // spawners put down when the round starts.
  server::CollisionLibrary meshes(files, registry);
  std::vector<StrategicCandidate> candidates;
  struct StrategicDraw {
    std::string icon;
    float heading = 0.0f;
    bool remoteControlled = false;
    bool spawner = false;
    int team = 0;
  };
  std::vector<StrategicDraw> strategicDraws;
  const auto addStrategic = [&](const game::ObjectTemplate& object, Vec3f position,
                                const Mat4& transform, int rootPart, bool spawned,
                                std::string icon, int team) {
    StrategicCandidate candidate;
    if (const auto* strategic = object.component("StrategicObject")) {
      if (const auto at = strategic->properties.find("strategicobjectname");
          at != strategic->properties.end() && !at->second.empty()) {
        candidate.name = std::string(at->second.back().value());
      }
      if (const auto at = strategic->properties.find("isonepiece");
          at != strategic->properties.end() && !at->second.empty()) {
        candidate.onePiece = at->second.back().asBool().value_or(false);
      }
    }
    candidate.position = position;
    candidate.radius = physicBoundingRadius(meshes, registry, object, rootPart);
    candidates.push_back(std::move(candidate));
    strategicDraws.push_back(StrategicDraw{std::move(icon), headingOf(transform),
                                           hasRemoteControlledChild(registry, object, 0), spawned,
                                           team});
  };
  struct SpawnedStrategic {
    const game::ObjectTemplate* object;
    const level::ObjectSpawner* spawner;
    std::string icon;
    int team;
  };
  std::vector<SpawnedStrategic> spawnedStrategic;

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
    // on it and no vehicle icon in their place. They go through the holders below.
    if (std::string icon = iconOf(vehicle, "StrategicObject", "intacticon"); !icon.empty()) {
      if (const auto* object = registry.find(vehicle)) {
        spawnedStrategic.push_back(
            SpawnedStrategic{object, &spawner, std::move(icon), point ? point->team : 0});
      }
      continue;
    }
    std::string icon = iconOf(vehicle, "VehicleHud", "minimapicon");
    if (icon.empty()) continue;
    Context::MapMarker marker{spawner.position.x, spawner.position.z, std::move(icon), {}, 16.0f,
                              Color{}, 0.0f};
    marker.atIconAlpha = true;
    assetMarkers_.push_back(std::move(marker));
  }
  if (level != nullptr) {
    for (const auto& object : level->objects) {
      std::string icon = iconOf(object.templateName, "StrategicObject", "intacticon");
      if (icon.empty()) continue;
      const auto* found = registry.find(object.templateName);
      if (found == nullptr) continue;
      Mat4 transform = object.transform;
      if (!object.hasTransform) {
        transform = object.hasRotation ? rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                              object.rotation.z)
                                       : Mat4::identity();
      }
      // A placed object's team is not in StaticObjects.con: 0.
      addStrategic(*found, object.position, transform, -1, false, std::move(icon), 0);
    }
  }
  for (auto& spawned : spawnedStrategic) {
    // `ObjectSpawner::getCollisionMesh` asks for part 0 of what it spawns
    // (Linux 0x52f8e7, collision_objects.h); the object stands at the spawner
    // with its rotation.
    const auto& at = *spawned.spawner;
    addStrategic(*spawned.object, at.position,
                 rotationYawPitchRoll(at.rotation.x, at.rotation.y, at.rotation.z), 0, true,
                 std::move(spawned.icon), spawned.team);
  }
  // One icon per holder (`GameLogic::addStrategicObject`, client 0x4f1750,
  // strategic_objects.h): a bridge's segments are one bridge.
  const auto holders = groupStrategicObjects(candidates);
  std::vector<Context::MapMarker> placedHolders;
  for (const StrategicHolder& holder : holders) {
    const StrategicDraw& first = strategicDraws[holder.members.front()];
    Context::MapMarker marker{holder.position.x, holder.position.z, first.icon, {}, 19.0f,
                              Color{}, 0.0f};
    marker.atIconAlpha = true;
    // 0x77e500: with no `RemoteControlledObject` child under the first object the
    // icon turns by `map(+0x76c) − atan2(m[+0x20], m[+0x28]) + π/2`
    // (0x77e689..0x77e69d, π/2 at 0x92fe8c). This map does not turn (+0x76c is
    // 0), so the turn is fixed. With such a child the icon stays upright, and is
    // skipped when the child's team is not the local player's; no template in
    // the data adds such a child, and that team test is not ported.
    if (!first.remoteControlled) {
      constexpr float kHalfPi = 1.5707964f;  // 0x92fe8c
      marker.rotation = kHalfPi - first.heading;
    }
    // A holder a spawner put down counts with the spawners' icons: those give
    // way to the server's own objects once they are known (appendAssets).
    if (first.spawner) {
      assetMarkers_.push_back(std::move(marker));
    } else {
      placedHolders.push_back(std::move(marker));
    }
  }
  spawnerMarkers_ = assetMarkers_.size();
  assetMarkers_.insert(assetMarkers_.end(), placedHolders.begin(), placedHolders.end());
  std::printf("  map: strategic objects %zu in %zu holders\n", candidates.size(), holders.size());
  appendAssets(context_.mapMarkers);
  std::printf("  map: vehicles and assets %zu\n", assetMarkers_.size());
  iconOf_ = [this](const std::string& templateName, const char* component,
                   const char* property) -> std::string {
    const auto* object = registry_ != nullptr ? registry_->find(templateName) : nullptr;
    if (object == nullptr) return {};
    const auto* part = object->component(component);
    if (part == nullptr) return {};
    const auto at = part->properties.find(property);
    if (at == part->properties.end() || at->second.empty()) return {};
    std::string path(at->second.back().value());
    for (char& c : path) {
      if (c == '\\') c = '/';
    }
    return path;
  };

  // The spawn screen's context is the general one plus its own gate; it is copied
  // after the map's window and markers are known.
  Context spawn = context_;
  spawn.isVisible = spawnContext_.isVisible;
  spawn.showState = spawnContext_.showState;
  // The spawn state's icon alpha target, +0x704 = 0.5 (0x777fe8).
  spawn.mapIconAlpha = 0.5f;
  spawnContext_ = std::move(spawn);
  dynamicContext_ = context_;
}

void Manager::afterTransition(int before, bool changed) {
  const int now = states_.current();
  // The map changes its target by the state number — verbatim 0x777dc0.
  map_.applyState(now);
  if (changed) dirty_ = true;
  // The spawn screen is baked geometry we keep, and its nodes hang on the same
  // variables: a state that changes them changes what it should be showing. The
  // engine has no such problem — it walks the tree every frame — so this is our
  // cache to invalidate, and forgetting it is what left the screen empty.
  if (before != now) spawnDirty_ = true;
}

void Manager::applyState(int state) {
  const int before = states_.current();
  afterTransition(before, states_.set(variables_, state, false));
}

void Manager::gameState(int state) {
  // In the engine the round's own events call `setState` — a spawn, a death, the
  // map key — each once, when it happens. Our frame works the state out anew every
  // frame instead, so only its changes are handed on: handing it on every frame
  // would take the HUD straight back out of the scoreboard.
  if (state == lastGameState_) return;
  lastGameState_ = state;
  applyState(state);
}

void Manager::serverInfo(const std::string& name, const std::string& address, int port,
                         const std::string& level, bool votingEnabled) {
  votingEnabled_ = votingEnabled;
  const auto set = [this](const char* key, const std::string& value) {
    std::string& slot = strings_[key];
    if (slot != value) dirty_ = true;
    slot = value;
  };
  set("ServerNameString", name);
  set("ServerIPString", address);
  set("ServerPortString", std::to_string(port));
  std::string key = "HUD_LEVELNAME_" + level;
  set("ActiveLevel", std::string(engine_->lexicon().text(key)));
}

bool Manager::outsideCombatArea(float x, float z) const {
  if (!combatBounds_) return false;
  const auto& [minX, maxX, minZ, maxZ] = *combatBounds_;
  return x < minX || x > maxX || z < minZ || z > maxZ;
}

void Manager::appendAssets(std::vector<Context::MapMarker>& out) const {
  // The level's spawner icons until the server's objects are known; then the
  // objects themselves, where they are. The level's static strategic objects
  // (after the spawners in `assetMarkers_`) stay either way.
  const std::size_t from = liveObjectsKnown_ ? spawnerMarkers_ : 0;
  out.insert(out.end(), assetMarkers_.begin() + static_cast<std::ptrdiff_t>(from),
             assetMarkers_.end());
  out.insert(out.end(), liveMarkers_.begin(), liveMarkers_.end());
}

void Manager::liveMap(const std::vector<LivePlayer>& players, const std::vector<LiveObject>& objects,
                      int localTeam, int localSquad) {
  // The map is baked geometry here (the engine paints it every frame): a rebuild
  // costs a frame's worth of milliseconds, so the icons move four times a second.
  // Chosen, not the original's rate.
  const auto now = std::chrono::steady_clock::now();
  if (now - liveMapAt_ < std::chrono::milliseconds(250)) return;
  liveMapAt_ = now;
  constexpr float kDegrees = 3.14159265f / 180.0f;
  // `mapObjectColors[2]` and `squadChatColors[2]` (OpenBF2 Menu/HudColors.cpp).
  constexpr Color kTeam{0.254f, 0.552f, 0.89f, 1.0f};
  constexpr Color kSquad{0.4470588f, 0.8901961f, 0.3450980f, 1.0f};
  std::vector<Context::MapMarker> markers;
  for (const LivePlayer& player : players) {
    // `collectIconFamily783440`: the living, and of the other team only those
    // near our own vehicle — which on the spawn screen is none. Ourselves is the
    // local arrow (`collectLocalPlayerIcon`), not this.
    if (player.local || player.team != localTeam) continue;
    Context::MapMarker icon;
    icon.worldX = player.x;
    icon.worldZ = player.z;
    icon.size = 16.0f;
    icon.atIconAlpha = true;  // the soldier layer reads +0x6fc (0x783440)
    icon.texture = "Ingame/Player/Icons/Minimap/mini_Soldier.tga";
    bool heading = false;
    // `populatePlayerMapIcon` with the role icons on: a squad leader's squad
    // number, the commander's own mark, both with the heading arrow.
    if (player.leader && player.squad > 0) {
      icon.texture =
          "Ingame/Player/Icons/Minimap/mini_Squad_" + std::to_string(player.squad) + "_small.tga";
      heading = true;
    } else if (player.commander) {
      icon.texture = "Ingame/Player/Icons/Minimap/mini_commander2.tga";
      icon.size = 32.0f;
      heading = true;
    }
    // `colorMapIcon`: our squad green, the rest of the team blue; the commander white.
    icon.color = (player.squad > 0 && player.squad == localSquad && !player.commander) ? kSquad
                 : player.commander                                                   ? Color{}
                                                                                      : kTeam;
    if (heading) {
      Context::MapMarker arrow = icon;
      arrow.texture = "Ingame/Player/Icons/Minimap/mini_headingArrow.tga";
      arrow.size = 32.0f;
      arrow.color = Color{0.0f, 0.0f, 0.0f, 1.0f};
      arrow.rotation = -player.yaw * kDegrees;
      markers.push_back(std::move(arrow));
    }
    markers.push_back(std::move(icon));
  }
  for (const LiveObject& object : objects) {
    if (!iconOf_) break;
    std::string texture = iconOf_(object.templateName, "StrategicObject", "intacticon");
    float size = 19.0f;
    float rotation = 0.0f;
    if (texture.empty()) {
      texture = iconOf_(object.templateName, "VehicleHud", "minimapicon");
      size = 16.0f;
      rotation = -object.yaw * kDegrees;  // a vehicle turns with itself
    }
    if (texture.empty()) continue;
    Context::MapMarker icon;
    icon.worldX = object.x;
    icon.worldZ = object.z;
    icon.texture = std::move(texture);
    icon.size = size;
    icon.rotation = rotation;
    icon.atIconAlpha = true;  // the vehicle layer reads +0x6fc (0x783f90)
    markers.push_back(std::move(icon));
  }
  if (!objects.empty()) liveObjectsKnown_ = true;
  // Rebuilt only on a change worth a pixel.
  bool same = markers.size() == liveMarkers_.size();
  for (std::size_t i = 0; same && i < markers.size(); ++i) {
    same = markers[i].texture == liveMarkers_[i].texture &&
           std::abs(markers[i].worldX - liveMarkers_[i].worldX) < 1.0f &&
           std::abs(markers[i].worldZ - liveMarkers_[i].worldZ) < 1.0f &&
           std::abs(markers[i].rotation - liveMarkers_[i].rotation) < 0.05f;
  }
  if (same) return;
  liveMarkers_ = std::move(markers);
  context_.mapMarkers.resize(contextFlagMarkers_);
  appendAssets(context_.mapMarkers);
  dirty_ = true;
  spawnDirty_ = true;
}

void Manager::scoreboardVoting(bool enabled, bool teamOnly, int kickTarget) {
  scoreboard_.setVoting(enabled, teamOnly, kickTarget);
}

void Manager::scoreboardRightClick() {
  if (!scoreboardDown_ || scoreboardPointer_) return;
  scoreboardPointer_ = true;
  std::printf("  scoreboard: the mouse is on\n");
}

void Manager::clickScoreboard(float x, float y, engine::Console& console) {
  if (!scoreboardPointer_) return;
  // The map vote's list and the scoreboard's two first; then the buttons, the
  // last hit winning as everywhere.
  for (const char* root : {"Scoreboard", "MapsList"}) {
    const auto list = listAt(tree_, root, screen_, x, y, context_);
    if (!list || list->node == nullptr || list->row < 0) continue;
    lastListHit_ = *list;
    std::printf("  scoreboard: list %s row %d column %d\n", list->node->name.c_str(), list->row,
                list->column);
    for (const auto& [event, line] : list->node->listCommands) {
      if (event != 1) continue;
      if (!console.executeLine(line)) {
        std::printf("  scoreboard: a list command without a handler — %s\n", line.c_str());
      }
    }
    dirty_ = true;
    return;
  }
  const Node* hit = nullptr;
  for (const char* root : {"Scoreboard", "MapsList"}) {
    if (const Node* found = buttonAt(tree_, root, screen_, x, y, &context_)) hit = found;
  }
  if (hit == nullptr) return;
  for (const auto& [event, line] : hit->commands) {
    if (event != 0 && event != 3) continue;
    std::printf("  scoreboard: %s -> %s\n", hit->name.c_str(), line.c_str());
    if (!console.executeLine(line)) {
      std::printf("  scoreboard: a command without a handler — %s\n", line.c_str());
    }
  }
  dirty_ = true;
}

void Manager::scoreboardKey(bool down) {
  if (down == scoreboardDown_) return;
  scoreboardDown_ = down;
  if (!down) scoreboardPointer_ = false;
  const int before = states_.current();
  const bool changed =
      down ? states_.scoreboardPressed(variables_) : states_.scoreboardReleased(variables_);
  afterTransition(before, changed);
}

void Manager::serverSpawnGroups(std::vector<ServerSpawnGroup> groups) {
  const auto same = [](const ServerSpawnGroup& a, const ServerSpawnGroup& b) {
    return a.id == b.id && a.team == b.team && a.aiOnly == b.aiOnly &&
           a.selectable == b.selectable && a.squad == b.squad && a.worldX == b.worldX &&
           a.worldZ == b.worldZ;
  };
  if (groups.size() == serverGroups_.size() &&
      std::equal(groups.begin(), groups.end(), serverGroups_.begin(), same)) {
    return;
  }
  serverGroups_ = std::move(groups);
  spawnDirty_ = true;
}

void Manager::useServerGroupsOnly() {
  if (serverGroupsOnly_) return;
  serverGroupsOnly_ = true;
  spawnDirty_ = true;
}

void Manager::localSquad(int squad, bool leader) {
  if (squad == localSquad_ && leader == localSquadLeader_) return;
  localSquad_ = squad;
  localSquadLeader_ = leader;
  spawnDirty_ = true;
}

void Manager::deadSpawnInfo(int serverGroup, float timeToSpawn) {
  // 0x4668d0: the screen counts as up in HUD states 1, 0xd, 0x11 and 0x12.
  const int state = states_.current();
  SpawnInfo info;
  info.serverGroup = serverGroup;
  info.chosenGroup = spawnScreen_.chosenPoint();
  info.screenUp = state == 1 || state == 0xd || state == 0x11 || state == 0x12;
  info.timeToSpawn = timeToSpawn;
  const SpawnInfoText made = spawnInfoText(info);
  std::string text(engine_->lexicon().text(made.key));
  if (made.time >= 0) {
    // `#TIME#` replaced by the number (0x467460).
    const std::string marker = "#TIME#";
    if (const std::size_t at = text.find(marker); at != std::string::npos) {
      text.replace(at, marker.size(), std::to_string(made.time));
    }
  }
  std::string& slot = strings_["SpawnInfoString"];
  if (slot == text) return;
  slot = std::move(text);
  spawnDirty_ = true;
}

void Manager::scoreboardPlayers(const std::vector<ScoreboardPlayer>& players, int localIndex) {
  // `Scoreboard::update(visible)` (0x7a4c80) does its work only while visible.
  const auto shown = variables_.find("ScoreboardShow");
  if (shown == variables_.end() || !shown->second) return;
  const std::string label(engine_->lexicon().text("HUD_TEXT_MENU_SCOREBOARD_NUMBER_OF_PLAYERS"));
  scoreboard_.update(players, localIndex, label);
  // The two headers are the local player's team and the other one
  // (`FriendlyTeamNameString` +0x52c, `EnemyTeamNameString` +0x530). They were
  // written only when the spawn screen was rebuilt, so the scoreboard of a player
  // who never saw it named the sides from team 1.
  const auto teamName = [this](int team) -> std::string {
    if (level_ == nullptr || team < 0 || team > 2) return {};
    return level_->teamNames[team];  // `gameLogic.setTeamName`, as applySpawnState reads it
  };
  const auto teamLabel = [&](int team) { return armyLabelKey(teamName(team)); };
  const auto teamFlag = [&](int team) { return teamFlagIcon(teamName(team)); };
  for (const ScoreboardPlayer& player : players) {
    if (player.index != localIndex || player.team <= 0) continue;
    const int other = player.team == 2 ? 1 : 2;
    strings_["FriendlyTeamNameString"] = std::string(engine_->lexicon().text(teamLabel(player.team)));
    strings_["EnemyTeamNameString"] = std::string(engine_->lexicon().text(teamLabel(other)));
    strings_["FriendlyFlagIconPathString"] = teamFlag(player.team);
    strings_["EnemyFlagIconPathString"] = teamFlag(other);
  }
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

void Manager::updateTickets(const TeamTickets& friendly, const TeamTickets& enemy, float dt) {
  strings_["FriendlyTicketsString"] = std::to_string(friendly.tickets);
  strings_["EnemyTicketsString"] = std::to_string(enemy.tickets);
  // The blinkers run every frame (`m_blinkers->update(dt)`, before the tickets).
  // Until a first update the blinkers rest where a released gate leaves them: 1.
  const float friendlyWas = values_.try_emplace("FriendlyTicketBleed", 1.0f).first->second;
  const float enemyWas = values_.try_emplace("EnemyTicketBleed", 1.0f).first->second;
  friendlyBleed_.update(dt, friendly.bleed, values_["FriendlyTicketBleed"]);
  enemyBleed_.update(dt, enemy.bleed, values_["EnemyTicketBleed"]);
  // They are the alpha of two of the spawn screen's map nodes
  // (HudElementsMap.con:212, 218), which is baked: a change means a rebuild.
  if (values_["FriendlyTicketBleed"] != friendlyWas || values_["EnemyTicketBleed"] != enemyWas) {
    spawnDirty_ = true;
  }
  // The tens digit of our ticket state, floored for negatives the way the
  // original's `state % 10` correction does.
  int remainder = friendly.state % 10;
  if (friendly.state < 0) remainder += 10;
  const int level = (friendly.state - remainder) / 10;
  if (level == 1 && !lowOnTickets_) {
    lowOnTickets_ = true;
    std::printf("  HUD: low on tickets — NOT DONE: the `lowOnTickets` sound (no sound)\n");
  } else if (level == 0 && lowOnTickets_) {
    lowOnTickets_ = false;
    std::printf("  HUD: tickets no longer low — the `lowOnTickets` sound would stop\n");
  }
}

void Manager::chatString(const ChatLine& line, bool hasLocalPlayer) {
  const int team = line.senderKnown ? line.senderTeam : 0;
  const bool always = line.channel == 12 || line.channel == 13;
  const bool withPlayer = (line.channel >= 0 && line.channel <= 11) ||
                          (line.channel >= 14 && line.channel <= 18);
  if (!always && !(withPlayer && hasLocalPlayer)) return;
  const int shownTeam = always ? 0 : team;
  std::printf("  HUD chat: channel %d, type %d, %s: %s\n", line.channel, line.type,
              line.senderKnown ? line.senderName.c_str() : "-", line.text.c_str());
  switch (line.type) {
    case 0:
    case 1:
      chat_.addGameMessage(line.text, nullptr, 0);  // `addTeamChat(text, 0, 0)`
      return;
    case 2:
      // `addGlobalChat` → `addPlayerMessage`; the original dereferences the
      // player unchecked, so an unknown sender is not shown here.
      if (line.senderKnown) {
        chat_.addPlayerMessage(line.text,
                               Chat::Talker{line.senderName, line.senderTeam, line.senderCommander});
      }
      return;
    case 3: {
      // The squad radio line; `ECHud 60` with the sender's soldier (his marker on
      // the map) is not ours.
      // The line carries a lexicon key (PLAYER_SPOTTED_TM_enemyforcesspotted);
      // an unknown key stays as sent. Which of a key's `_alt` lines the original
      // picks is not traced: the base line is shown.
      const std::string message(engine_->lexicon().text(line.text));
      const std::string code(1, static_cast<char>(0xa7));
      const std::string text =
          line.senderKnown ? code + "0" + line.senderName + ": " + code + "1" + message : message;
      if (!line.text.empty()) chat_.addKillMessage(text, &line.speaker, shownTeam);
      return;
    }
    default:
      std::printf("  HUD chat: unknown message type %d\n", line.type);
      return;
  }
}

void Manager::killedBy(const Kill& kill, const Local& local) {
  // `BF2HudManager::handleGameLogicEvent` case 3 (OpenBF2
  // BF2HudManagerGameLogicEvents.cpp; BF2.exe 0x753de0).
  const auto localized = [this](std::string_view key) {
    return std::string(engine_->lexicon().text(key));
  };
  const auto replace = [](std::string text, std::string_view token, const std::string& with) {
    const std::size_t at = text.find(token);
    if (at != std::string::npos) text.replace(at, token.size(), with);
    return text;
  };
  chat_.setLocalTeam(local.team);
  // Our kill of an enemy: a notice.
  if (local.id >= 0 && kill.victimKnown && !local.commander && local.id == kill.killer &&
      kill.killerAlive && kill.victimTeam != local.team) {
    chat_.addNotice(replace(localized("HUD_CHAT_YOUGOTAKILLSCORE"), "#PLAYERNAME#", kill.victimName));
  }
  if (kill.killerAlive && kill.killerKnown) {
    if (kill.victimKnown && kill.victim == kill.killer) {
      chat_.addGameMessage(replace(localized("HUD_HUD_KILLEDBY_MYSELF"), "#PLAYERNAME#",
                                   kill.victimName),
                           nullptr, kill.victimTeam);
      return;
    }
    if (local.id >= 0 && local.id == kill.victim) {
      // `setDeathMessage(HUD_TEXT_MENU_YOU_WERE_KILLED)` on the information layer
      // is not ours yet; the chat's half is.
      chat_.addNotice(replace(localized("HUD_CHAT_YOUWEREKILLEDBY"), "#PLAYERNAME#", kill.killerName));
    }
    if (!kill.victimKnown) return;
    const char* key = kill.victimTeam != kill.killerTeam
                          ? (!kill.weaponKey.empty() && !kill.missingWeapon ? "HUD_HUD_KILLEDBY_KILLS"
                                                                             : "HUD_HUD_KILLEDBY_KILLED")
                          : "HUD_HUD_KILLEDBY_TEAMKILLS";
    std::string text = localized(key);
    text = replace(std::move(text), "#PLAYERNAME1#", kill.killerName);
    text = replace(std::move(text), "#WEAPON#",
                   kill.weaponKey.empty() ? std::string{} : localized(kill.weaponKey));
    text = replace(std::move(text), "#PLAYERNAME2#", kill.victimName);
    std::printf("  HUD kill feed: %s\n", text.c_str());
    chat_.addGameMessage(text, &kill.killerSpeaker, kill.killerTeam);
    return;
  }
  // No killer player: the commander's artillery (0xFD the enemy's, 0xFE the
  // victim's own team's).
  if (!kill.victimKnown || (kill.killer != 0xFD && kill.killer != 0xFE)) return;
  const int team = kill.killer == 0xFD ? (kill.victimTeam == 1 ? 2 : 1) : kill.victimTeam;
  std::string text = localized(kill.killer == 0xFD ? "HUD_HUD_KILLEDBY_KILLS" : "HUD_HUD_KILLEDBY_TEAMKILLS");
  text = replace(std::move(text), "#PLAYERNAME1#", " ");
  text = replace(std::move(text), "#WEAPON#", localized("HUD_TEXT_COMMANDER_ARTILLERY"));
  text = replace(std::move(text), "#PLAYERNAME2#", kill.victimName);
  chat_.addGameMessage(text, nullptr, team);
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
  const auto addCircle = [&](float x, float z, int id) {
    const bool chosen =
        static_cast<int>(spawnContext_.spawnMarkers.size()) == spawnScreen_.choice().marker;
    spawnContext_.spawnMarkers.push_back(Context::SpawnMarker{x, z, chosen});
    markerPoints.push_back(id);
  };
  for (const auto& point : controlPoints_) {
    Context::MapMarker marker;
    marker.worldX = point.position.x;
    marker.worldZ = point.position.z;
    marker.label = point.nameKey;
    marker.texture = controlPointIcon(point.team == 0 ? "" : teamName(point.team),
                                      point.unableToChangeTeam);
    spawnContext_.mapMarkers.push_back(std::move(marker));
    // Without a server (our own game): the level's points. On a real server none
    // until its groups arrive — otherwise the circles jump from these to those.
    if (!serverGroupsOnly_ && serverGroups_.empty() && point.team == selectedTeam) {
      addCircle(point.position.x, point.position.z, point.id);
    }
  }
  // With them: `SpawnManager::getGroupsForPlayer` (Linux 0x4b8790), the list the
  // map's circles come from (0x77f6e0) — the player's team, not bots-only (we are
  // not a bot), selectable, and a squad's group (`getSquad` != -1, from the
  // group's ghost) only for that squad's members who do not lead it.
  // On a server, nothing until it has named our team (`teamFromServer`).
  // A squad's group only for its members who do not lead it (OpenBF2
  // `SpawnManager::getGroupsForPlayer`, the same condition as 0x4b8790).
  const bool teamKnown = !serverGroupsOnly_ || spawnScreen_.teamFromServer();
  for (const ServerSpawnGroup& group : serverGroups_) {
    if (!teamKnown) break;
    if (group.team != selectedTeam || group.aiOnly || !group.selectable) continue;
    if (group.squad != -1 && (group.squad != localSquad_ || localSquadLeader_)) continue;
    // A dynamic group (192..255, OpenBF2 `SpawnManager::createDynamicSpawnGroup`)
    // is a squad's: the squads make them (`SquadManager::reAddDynamicSpawnGroups`).
    // `CreateSpawnGroupEvent` does not carry the squad — only the group's ghost
    // does — so until that comes the group is nobody's we know, not everyone's.
    // Taken for everyone's, it drew a circle at some squad leader's feet for the
    // first second of the spawn screen.
    if (group.id >= 192 && group.squad == -1) continue;
    addCircle(group.worldX, group.worldZ, group.id);
  }
  spawnScreen_.setMarkerPoints(std::move(markerPoints));
  // The vehicles and the strategic objects come after the flags, the way the
  // original's batch has them.
  appendAssets(spawnContext_.mapMarkers);
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
  // Printed when what was built changes, not on every rebuild: an animation or a
  // countdown rebuilds it every frame, and the log drowned in the same line. The
  // count of rebuilds rides on the line, so a screen rebuilt far too often still
  // shows.
  ++spawnRebuilds_;
  const std::array<std::size_t, 3> shape{built.size(), mapCount, spawnContext_.spawnMarkers.size()};
  if (shape != lastSpawnShape_) {
    lastSpawnShape_ = shape;
    std::printf("  HUD: the spawn screen rebuilt, pieces %zu (map %zu), circles %zu "
                "(rebuild %zu)\n",
                shape[0], shape[1], shape[2], spawnRebuilds_);
  }
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
  // The scoreboard is not one of them any more. It is a state (9), and Tab drives
  // it the engine's way (`StateMachine::scoreboardPressed`); once `ScoreboardShow`
  // is really on, the ordinary walk draws it where the tree has it — after the map
  // and the spawn screen, so over both — with its own 0.1 s fade
  // (`HudElementsScoreboard.con`: `setNodeInTime 0.1`, `setNodeOutTime 0.1`,
  // `addNodeAlphaShowEffect`). Baked with its gate forced, it had neither the fade
  // nor the order, and a tab clicked on it never showed, because the bake was
  // never made again.
  for (const KeyScreenSetup& setup : {
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

int Manager::listRevision(const Node& node) const {
  return node.listData == 3 ? chat_.revision() : scoreboard_.revision();
}

bool Manager::dynamicChanged(const DynamicNode& live) const {
  const Node& node = *live.node;
  const bool isMap = node.type == NodeType::Map || node.type == NodeType::MiniMap;
  const bool isRotating = !node.rotateVariable.empty();
  const bool isBar = node.type == NodeType::Bar;
  if (!live.built) return true;
  if (node.type == NodeType::List) {
    // The rows, and the fade the node is built with: the scoreboard fades in over
    // 0.1 s (`setNodeInTime 0.1`), and a list built at its start would stay clear.
    return listRevision(node) != live.shownRevision ||
           std::abs(animator_.state(node).alpha - live.shownValue) > 0.001f;
  }
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
  if (node.type == NodeType::List) {
    live.built = true;
    live.shownRevision = listRevision(node);
    live.shownValue = animator_.state(node).alpha;
    return buildNode(node, font_.font, font_.atlasPath, screen_, dynamicContext_);
  }

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
  // itself. That it steps the map the way the HUD's `setZoom` does
  // (`cycleZoom`, 0x7779a0) is our assumption, and it is debt.
  if (down && !zoomKeyWasDown_) {
    map_.cycleZoom();
    std::printf("  map: zoom %d\n", map_.zoomIndex());
  }
  zoomKeyWasDown_ = down;
}

void Manager::animate(float seconds, float yaw, const Vec3f& eye) {
  // `HudChat::update` every frame: the bands' timers.
  chat_.update(seconds);
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

  // The map's window: the combat area's square divided by `pow(2.3, +0x698)`
  // (`getZoomScale`, 0x77397c: the factor is the texture over the square). The
  // thumbnail in the corner follows the player; the big map is centred on the
  // combat area (`setExpanded(true)` → `setViewCenter(getMapCenterOffset())`,
  // 0x776a65). How exactly the engine blends these two centres — 0x773870 takes
  // the weight from +0x694 — has **not been worked out**, so for now there are
  // simply two branches.
  if (mapBaseHalfU_ > 0.0f) {
    const float scale = map_.zoomScale();
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
  // A list node: the cell under the cursor becomes the list's current one, and the
  // list's click commands run (`setListNodeConCmd 1 …`, the press's event 1).
  for (const char* root : {"SpawnMenu", "TopLayer"}) {
    const auto list = listAt(tree_, root, screen_, x, y, spawnContext_);
    if (!list || list->node == nullptr) continue;
    lastListHit_ = *list;
    // The squad menu's lists read their pick back (0x7c40a0 sets the row and
    // column on the press): the member list (5) and the invite list (8).
    if (list->node->listData == 5 || list->node->listData == 8) {
      ListData& picked = list->node->listData == 5 ? squadList_.memberList() : squadList_.inviteList();
      picked.selectedRow = list->row;
      picked.selectedColumn = list->column;
    }
    std::printf("  spawn screen: list %s row %d column %d\n", list->node->name.c_str(), list->row,
                list->column);
    if (list->row < 0) return;
    for (const auto& [event, line] : list->node->listCommands) {
      if (event != 1) continue;
      if (!console.executeLine(line)) {
        std::printf("  spawn screen: a list command without a handler — %s\n", line.c_str());
      }
    }
    spawnDirty_ = true;
    return;
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
