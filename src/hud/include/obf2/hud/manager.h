#pragma once
// The interface of a round, whole: the node tree from the game's files, the
// variables the nodes hang on, the state machine that switches them, the spawn
// screen and the animation that moves the corner regions.
//
// The engine keeps the same thing in `Code/BF2/Menu/BF2HudManager.cpp` beside
// `Bf2HudBuilder.cpp`: the manager owns the tree and the values, and the renderer
// draws what it produces. So there is no graphics here either — every method
// gives back `DrawPiece`s, and putting them on the card is the caller's business.
#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "obf2/engine/engine.h"
#include "obf2/font/font_file.h"
#include "obf2/game/controls.h"
#include "obf2/game/object_template.h"
#include "obf2/hud/animation.h"
#include "obf2/hud/blinker.h"
#include "obf2/hud/bottom_left.h"
#include "obf2/hud/chat.h"
#include "obf2/hud/hud.h"
#include "obf2/hud/map_node.h"
#include "obf2/hud/render.h"
#include "obf2/hud/scoreboard.h"
#include "obf2/hud/squad_list.h"
#include "obf2/hud/spawn_interface.h"
#include "obf2/hud/states.h"
#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"
#include "obf2/meme/graph.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::hud {

class Manager {
 public:
  // What the interface cannot do for itself: spawning is the session's business,
  // and where the rectangles are reported is the command line's.
  struct Hooks {
    // DONE. Returns whether the request really reached the server: if it did not,
    // the spawn screen has to stay where it is, or the picture freezes.
    std::function<bool(int team, int kit, int group)> requestSpawn;
    std::function<void()> commitSuicide;
    // The player clicked a kit on the spawn screen (`HudKitSelection::selectKit`).
    std::function<void(int kit)> selectKit;
    // A squad request from the squad tab (`SquadList::Action`): join, leave,
    // create, lock. Whoever holds the connection sends it.
    std::function<void(const SquadList::Action& action)> squadAction;
    // `openbf2.spawnAt <group>` — a spawn straight by the server's group number.
    std::function<void(int team, int kit, int group)> spawnAtGroup;
    // The scoreboard's manage page (OpenBF2 `HudScoreboard::singleClick`):
    // column 4 mutes or unmutes a player's voice; 7 and 8 start a kick vote on
    // him, or cast yes / no on the one already open.
    std::function<void(int player)> toggleMute;
    std::function<void(int player, int column)> kickVote;
    // `--hud-rects`: every rectangle drawn, in the original frame dump's format.
    std::function<void(const char* where, const std::vector<DrawPiece>& pieces)> reportRects;
  };

  struct Setup {
    int screenWidth = 800;
    int screenHeight = 600;
    int kit = 0;
    int team = 1;
    std::string levelName;
  };

  // A screen visible only while a key is held: the scoreboard, the radio, the map
  // menu. Its geometry does not change — only whether it is drawn this frame.
  struct KeyScreen {
    std::string group;
    std::string action;  // the action's name in the ControlMap, not a key
    // The HUD state this screen belongs to (docs/functions/hud-states.md). The
    // scoreboard is state 9 and really is on a key; the spawn screen is state 1
    // and stands until the player has spawned.
    int state = -1;
    bool heldByKey = true;
    std::vector<DrawPiece> pieces;
  };

  // A node whose contents change in the game: a caption, a bar, the turning
  // compass, the map itself. Their geometry is rebuilt on its own, so that a
  // value's change does not rebake the whole interface.
  struct DynamicNode {
    const Node* node = nullptr;
    std::string shownText;
    float shownValue = -1.0f;
    float shownAngle = 0.0f;
    int shownRevision = -1;  // a list: the scoreboard's revision it was built from
    bool built = false;
  };

  // Reads the tree, the animation graph, the control layout and the level's
  // gameplay objects. False when the HUD's own font is missing — then there is no
  // interface and the caller draws the world alone.
  bool load(FileSystem& files, engine::Engine& engine, const level::Level* level,
            const game::Registry& registry, const Setup& setup, Hooks hooks);

  bool ready() const { return ready_; }

  // --- the state machine (docs/functions/hud-states.md) ---
  // The state the round is in by the game: 0 in battle, 1 on the spawn screen,
  // 2 on the big map. Handed on only when it changes — see `gameState` in the
  // source for why.
  void gameState(int state);
  // Tab, `c_GIShowScoreboard`: held shows the scoreboard over whatever the HUD
  // was on, released goes back to it (`StateMachine::scoreboardPressed`).
  void scoreboardKey(bool down);
  int state() const { return states_.current(); }
  // The derived variables, as the engine's two per-frame functions write them
  // (0x466930 for the map's size, 0x78d0f0 for the player's set).
  void updateVariables(bool hasPlayer, bool mapFullSize);
  // The spawn screen's own variables: the team tabs, the kit rows, the markers.
  void applySpawnState();
  // The server's spawn groups (`CreateSpawnGroupEvent`), as the client's
  // `SpawnManager` keeps them. When there are any, the circles are these and not
  // the level's control points: the map draws `getGroupsForPlayer` (0x77f6e0), and
  // a flag that changed hands since the level's file was written is only in them.
  struct ServerSpawnGroup {
    int id = 0;
    int team = 0;
    bool aiOnly = false;      // +0x9a
    bool selectable = false;  // +0xa0
    int squad = -1;           // `getSquad`: -1 for none, from the group's ghost
    float worldX = 0.0f, worldZ = 0.0f;
  };
  void serverSpawnGroups(std::vector<ServerSpawnGroup> groups);
  // The local player's squad (`getSquadId`), 0 for none: a squad's group is its
  // squad's alone. Whether he leads it (`getIsSquadLeader`, which also hides the
  // group) is not carried by his state, and is taken as not.
  void localSquad(int squad, bool leader = false);
  // The spawn bar's text while the player is dead (`spawnInfoText`, 0x4668d0).
  // `serverGroup` and `timeToSpawn` are the player's own state; the chosen circle
  // and whether the screen is up the manager knows itself.
  void deadSpawnInfo(int serverGroup, float timeToSpawn);
  // Whether the circles are the server's groups — then DONE names a group number.
  bool circlesAreServerGroups() const { return !serverGroups_.empty(); }
  // On a real server the circles are the server's groups and nothing else: the
  // client's `SpawnManager` has none until `CreateSpawnGroupEvent` brings them, so
  // until then the map has no circles (0x77f6e0 walks an empty list). The level's
  // own control points stand in only in our own game, which has no such events.
  void useServerGroupsOnly();
  // The players the scoreboard lists, and which of them is us. The engine's
  // `Scoreboard::update` (0x7a4c80) runs only while the scoreboard is shown, and
  // so does this.
  void scoreboardPlayers(const std::vector<ScoreboardPlayer>& players, int localIndex);
  // The squads as the server has them, for the spawn screen's squad tab.
  void squadWorld(const SquadWorld& world);
  // The scoreboard's mouse (OpenBF2 `BF2HudManager::handleGameInput`'s filter):
  // with the scoreboard held, the right button switches the HUD to its mouse mode
  // (12) — "Right click to activate mouse" — and letting go of the key ends both.
  void scoreboardRightClick();
  // Who the manage page's kick-vote columns are for: whether the server takes
  // votes, whether a kick is for our own team only, and the player an open kick
  // vote is about (-1 for none).
  void scoreboardVoting(bool enabled, bool teamOnly, int kickTarget);

  // The map's live icons for this frame (obf2/app/map_contacts.h): players,
  // vehicles, orders, commander assets and spots. Once any vehicle of the
  // server's is among them, they replace the level's spawner icons.
  void setMinimapContacts(std::vector<MapContact> contacts);
  // The satellite line's width +0xb64 for the map shown now, in node units (the
  // whole world drawn, clamped to the node's width, 0x784bb3..0x784c7d):
  // RemoteWorld::stepMap takes it. Empty while no map node is laid out.
  std::optional<float> satelliteDrawnWidth() const;
  // Whether a world point lies outside the level's combat area (its bounds).
  bool outsideCombatArea(float x, float z) const;
  // The server's name, address and port for the scoreboard's server info
  // (`HudInformationLayer::setServerInfo`, OpenBF2 — the menu calls it on a join)
  // and the level's name (`ActiveLevel`, "HUD_LEVELNAME_<map>").
  void serverInfo(const std::string& name, const std::string& address, int port,
                  const std::string& level, bool votingEnabled = false);
  bool scoreboardPointer() const { return scoreboardPointer_; }
  // A left click while the scoreboard has the mouse: its buttons (the tabs, the
  // server info, the map vote) and its lists.
  void clickScoreboard(float x, float y, engine::Console& console);
  const Scoreboard& scoreboard() const { return scoreboard_; }

  // --- what moves by itself ---
  // One frame of the `Menu/Ingame` graph: the corner regions, the map's
  // rectangle and window, the compass. `yaw` is the view's angle in degrees and
  // `eye` the point the map is centred on.
  void animate(float seconds, float yaw, const Vec3f& eye);

  // --- geometry ---
  std::vector<DrawPiece> buildIngame();
  std::vector<DrawPiece> buildSpawn();
  // The live nodes: which of them changed since the last frame, and its pieces.
  std::vector<DynamicNode>& dynamicNodes() { return dynamic_; }
  bool dynamicChanged(const DynamicNode& node) const;
  std::vector<DrawPiece> buildDynamic(DynamicNode& node);

  const std::vector<KeyScreen>& keyScreens() const { return keyScreens_; }

  // --- input ---
  // A click on the spawn screen: the circles on the map first, then the buttons,
  // each running its console command as `setButtonNodeConCmd` gave it.
  void clickSpawn(float x, float y, engine::Console& console);
  // The map key and the zoom key, both toggles rather than holds.
  void mapKey(bool down);
  void zoomKey(bool down);
  bool bigMap() const { return bigMap_; }

  // --- what the session fills in ---
  // `HudInformationLayer::updateTickets` (PE 0x4646b0) for one frame: our team's
  // and the other's tickets into `FriendlyTicketsString` / `EnemyTicketsString`,
  // their bleed into the two float blinkers that drive `FriendlyTicketBleed` /
  // `EnemyTicketBleed`, and our ticket state's tens digit for the low-ticket
  // warning (a sound: 1 starts `lowOnTickets`, 0 stops it — we have no sound, so
  // the change is logged).
  struct TeamTickets {
    int tickets = 0;
    bool bleed = false;
    int state = 0;
  };
  void updateTickets(const TeamTickets& friendly, const TeamTickets& enemy, float dt);

  // The in-game chat (`HudChat`, list data 3 — chat.h).
  Chat& chat() { return chat_; }
  // A kill, as `BF2HudManager::handleGameLogicEvent` case 3 turns the
  // `KilledByEvent` into lines (OpenBF2 BF2HudManagerGameLogicEvents.cpp;
  // BF2.exe 0x753de0). The caller resolves the players and the weapon's HUD name;
  // the manager makes the text and puts it where the original does.
  struct Kill {
    int killer = 0;            // the event's byte 0 (0xFD/0xFE: artillery)
    int victim = 0;            // byte 1
    bool killerAlive = false;  // byte 2: there is a killer player
    bool killerKnown = false, victimKnown = false;
    std::string killerName, victimName;
    int killerTeam = 0, victimTeam = 0;
    Chat::Speaker killerSpeaker;
    // The weapon's or vehicle's `hudName` (a localisation key), and whether a
    // soldier's weapon had no weapon HUD at all.
    std::string weaponKey;
    bool missingWeapon = false;
  };
  struct Local {
    int id = -1;
    int team = 0;
    bool commander = false;
  };
  void killedBy(const Kill& kill, const Local& local);
  // A chat string the server sent, as `Game::outputChatString` (OpenBF2
  // Game/Main/Game.cpp) routes it: channels 12 and 13 always, the others only
  // with a local player; type 0 and 1 are game lines, 2 a player's chat, 3 a
  // squad radio line "Name: text".
  struct ChatLine {
    int channel = 0;
    int type = 0;
    std::string text;
    bool senderKnown = false;
    std::string senderName;
    int senderTeam = 0;
    bool senderCommander = false;
    Chat::Speaker speaker;
  };
  void chatString(const ChatLine& line, bool hasLocalPlayer);

  VariableMap& variables() { return variables_; }
  std::map<std::string, std::string>& strings() { return strings_; }
  std::map<std::string, float>& values() { return values_; }
  SpawnInterface& spawnScreen() { return spawnScreen_; }
  SquadList& squadList() { return squadList_; }
  const game::ControlMap& controls() const { return controls_; }
  const Builder& tree() const { return tree_; }
  Builder& tree() { return tree_; }
  const Screen& screen() const { return screen_; }
  MapNode& map() { return map_; }
  const std::vector<level::ControlPoint>& controlPoints() const { return controlPoints_; }
  float mapWorldSize() const { return context_.mapWorldSize; }
  // The red hatch over everything outside the combat area — a picture we make,
  // handed to the texture resolver under `#combatarea`.
  const std::optional<texture::Texture>& combatAreaOverlay() const { return combatArea_; }

  // Whether the baked geometry has to be made again.
  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  bool spawnDirty() const { return spawnDirty_; }
  void clearSpawnDirty() { spawnDirty_ = false; }
  void markDirty() { dirty_ = true; }
  void markSpawnDirty() { spawnDirty_ = true; }

  // `--hud-vars`: every variable the tree asks for and which of them nobody
  // fills in. A name nobody writes is a node that never appears, and it fails
  // silently — so the debt is printed as a list.
  void reportVariables() const;

 private:
  void bindCommands(engine::Console& console);
  void buildContexts(engine::Engine& engine);
  void buildMapWindow(FileSystem& files, const level::Level* level, const game::Registry& registry);
  void bakeKeyScreens();
  FontRef fontFor(std::string_view style);

  bool ready_ = false;
  Setup setup_;
  Hooks hooks_;
  FileSystem* files_ = nullptr;
  engine::Engine* engine_ = nullptr;
  const level::Level* level_ = nullptr;
  const game::Registry* registry_ = nullptr;
  // What a kit row is built from when the level named none for that row.
  std::string emptyKitName_;

  Builder tree_;
  Screen screen_;
  font::LoadedFont font_;
  std::map<std::string, font::LoadedFont> fonts_;
  game::ControlMap controls_;
  meme::Graph graph_;

  VariableMap variables_;
  std::map<std::string, std::string> strings_;
  std::map<std::string, float> values_;
  // `HudInformationLayer`'s two ticket blinkers (PE 0x46B500 registration):
  // gate value true, period 1, smooth.
  FloatBlinker friendlyBleed_{true, 1.0f, true};
  FloatBlinker enemyBleed_{true, 1.0f, true};
  bool lowOnTickets_ = false;  // +0x98
  Chat chat_;
  // A list node's revision: the chat's for list data 3, the scoreboard's
  // otherwise (`dynamicChanged` rebuilds a list when it moves).
  int listRevision(const Node& node) const;
  std::map<std::string, float> alpha_;
  float backgroundAlpha_ = 0.0f;

  Context context_;
  Context spawnContext_;
  Context dynamicContext_;

  SpawnInterface spawnScreen_;
  std::vector<ServerSpawnGroup> serverGroups_;
  bool serverGroupsOnly_ = false;  // useServerGroupsOnly
  int localSquad_ = 0;
  bool localSquadLeader_ = false;
  std::vector<level::ControlPoint> controlPoints_;
  std::vector<Context::MapMarker> assetMarkers_;
  std::size_t spawnerMarkers_ = 0;
  std::optional<std::array<float, 4>> combatBounds_;  // min x, max x, min z, max z       // the first of `assetMarkers_` are spawners'
  std::size_t contextFlagMarkers_ = 0;   // the combat map's flags, before the assets
  void appendAssets(std::vector<Context::MapMarker>& out) const;
  bool liveObjectsKnown_ = false;
  // The spawn screen bakes the map, so it takes the contacts at most four times
  // a second (a rebuild costs a frame): a choice of ours, not the original's rate.
  std::chrono::steady_clock::time_point spawnContactsAt_{};
  std::optional<texture::Texture> combatArea_;

  BottomLeftPanel bottomLeft_;
  BottomLeftMode bottomLeftMode_ = BottomLeftMode::Hidden;
  float bottomRightX_ = kBottomRightHiddenX;
  float bottomRightShownX_ = kBottomRightShownX;
  float bottomRightAlpha_ = 0.0f;
  bool bottomRightShow_ = false;

  MapNode map_;
  MapAngle mapAngle_;
  bool mapRectStale_ = true;
  float mapBaseCentreU_ = 0.5f, mapBaseCentreV_ = 0.5f;
  float mapBaseHalfU_ = 0.0f, mapBaseHalfV_ = 0.0f;
  bool bigMap_ = false;
  bool mapKeyWasDown_ = false;
  bool zoomKeyWasDown_ = false;

  Animator animator_;
  Scoreboard scoreboard_;
  SquadList squadList_;
  // The list cell the last click hit, for the list's own commands
  // (`SquadMenu.singleClickNew` reads the list's current row and column).
  ListHit lastListHit_;
  bool scoreboardPointer_ = false;
  bool votingEnabled_ = false;
  void pushSquadVariables();
  std::vector<KeyScreen> keyScreens_;
  std::vector<DynamicNode> dynamic_;

  void applyState(int state);
  void afterTransition(int before, bool changed);

  StateMachine states_;
  int lastGameState_ = -1;
  bool scoreboardDown_ = false;
  bool dirty_ = false;
  bool spawnDirty_ = false;
  // How many times the spawn screen was rebuilt, and what the last one held.
  std::size_t spawnRebuilds_ = 0;
  std::array<std::size_t, 3> lastSpawnShape_{};
  bool ingameReported_ = false;
};

}  // namespace obf2::hud
