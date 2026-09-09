#pragma once
// The game server.
//
// In BF2 the server runs **always**, even in a single-player game: the engine
// brings up a local server and connects to it through an in-memory loop. So this
// is not a "multiplayer add-on" but the core — the server owns the world and the
// client only shows what it was sent.
//
// The structure follows the original (`BF2/Game/GameServer/` by the paths from
// the sources): the server holds the world's state, accepts connections and
// broadcasts object updates.
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "obf2/game/object_template.h"
#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"
#include "obf2/net/connection.h"
#include "obf2/net/session.h"
#include "obf2/server/collision_world.h"
#include "obf2/server/physics.h"

namespace obf2::server {

// The round's state. In the original this is `dice::hfe::GameStatus`, and the
// whole game is driven by the `ServerGameLogic::update` state machine.
enum class GameStatus { PreGame, Playing, EndGame };

// The flag's position on the pole. A point's owner can only change at the
// bottom — hence the "neutralisation" before a capture.
enum class FlagPosition { Bottom, Middle, Top };

// A team's tickets. The loss is fractional (bleed per second), while we show an integer.
struct TeamState {
  int tickets = 0;
  float fraction = 0.0f;              // the accumulated fractional part of the loss
  float ticketChangePerSecond = 0.0f;  // < 0 means bleeding
  int ticketState = 0;                 // the warning level for the interface
  float areaValue = 0.0f;              // the sum of the held points' weights
  int controlPoints = 0;
};

// An object in the server's world.
struct WorldObject {
  std::uint32_t id = 0;
  std::string templateName;
  Vec3f position;
  Vec3f rotation;
  Vec3f velocity;
  bool onGround = false;

  // Statics are sent once on spawn, moving things every tick. Without that split
  // 907 buildings would go over the network sixty times a second.
  bool dynamic = false;
  std::uint32_t ownerPlayerId = 0;  // 0 = nobody's
  // A vehicle from a spawner: so we know what to re-place when a point's owner changes.
  int spawnerIndex = -1;
};

struct Player {
  std::uint32_t id = 0;
  std::string name;
  std::unique_ptr<net::Connection> connection;
  bool acknowledged = false;
  // Whether we have sent this player the world's initial state.
  bool worldSent = false;

  net::PlayerInput input;          // the last input received
  std::uint32_t lastSequence = 0;  // so an old packet is not applied twice
  std::uint32_t soldierId = 0;     // the object the player controls

  int team = 1;
  // The chosen spawn point — the id of the control point the player asked to
  // spawn from. Zero means "has not chosen yet", and that is not our invention:
  // in the engine the server spawns exactly those whose `Player::getSpawnGroup() > 0`
  // (`ServerGameLogic::uPlayingSpawning`), and the field is set by the event
  // `NESelectSpawnGroup` — see docs/functions/network-events.md.
  int spawnGroup = 0;
  // The chosen kit (`NESelectKit`). Only remembered for now: the kits'
  // equipment has not been taken apart yet.
  int kit = 0;
  bool alive = false;
  // Health from the soldier's data (`ObjectTemplate.armor.maxHitPoints 100`).
  float health = 100.0f;
  // Floating. Switched with hysteresis: the entry and exit thresholds differ
  // (`phy-soldier-start-float` / `stop-float`), otherwise it jitters at the boundary.
  bool swimming = false;
  float respawnTimer = 0.0f;  // how long is left to wait before spawning
};

struct ServerSettings {
  std::string levelName = "Dalian_plant";
  std::string gameMode = "gpm_cq";
  int maxPlayers = 16;

  // The simulation's rate. Not a "customary" constant: `WorldPref::mTickTime`
  // lies in the Linux server's `.data` at 0xf68c50 and equals 0.0333333333333333
  // (double), that is exactly 1/30 s. See `obf2::server::kTickTime`.
  float tickRate = 30.0f;

  // The soldier's speeds come from the engine's constants (`phy-soldier-run-speed`
  // 3.9 and `phy-soldier-sprint-speed` 7), but stay here so a test can set its
  // own. Zero means "take it from the physics".
  float walkSpeed = 0.0f;
  float sprintSpeed = 0.0f;

  // The soldier's collision radius — from `coll-soldier-radius` (0.25).
  // Kept here only for compatibility; the shape is set by PhysicsConstants.
  float soldierRadius = 0.25f;
  // The soldier's health: in every BF2 kit it is exactly 100.
  float soldierMaxHealth = 100.0f;
  Vec3f spawnPosition{0.0f, 0.0f, 0.0f};
  std::string soldierTemplate = "player_soldier";

  // Movement constants from the game's data (Vars.Set phy-soldier-*).
  PhysicsConstants physics;

  // How many seconds a player waits before spawning. In the original it depends
  // on the mode and the tickets; a constant for now.
  float respawnDelay = 3.0f;
  // Spawn right after the acknowledgement, without waiting for a point to be chosen.
  // **This is ours, not the engine's**: the original has no such path at all,
  // and a real client always goes through the spawn screen and sends
  // `NESelectSpawnGroup`. The flag is for tests and headless runs; the
  // application clears it as soon as it shows the spawn screen.
  bool spawnOnJoin = true;

  // How many players are needed for a round to start (`sv.numPlayersNeededToStart`,
  // 2 by default). While there are fewer, the game holds a caption in the middle
  // HUD_STARTOFROUND_NRPLAYERSNEEDED.
  int playersNeededToStart = 2;

  // --- tickets (values from the data and from the original's code) ---
  //
  // The starting count is set by `gameLogic.setDefaultNumberOfTickets` in
  // GameLogicInit.con (250 per team for bf2), while the engine's own default is
  // 50. The multiplier `sv.ticketRatio` is 100 %.
  int defaultTickets[3] = {0, 50, 50};
  float ticketRatio = 100.0f;
  // The bleed rate at the enemy's full advantage, tickets per minute.
  // In ServerGameLogic::reset it is 10 per team.
  float ticketLossPerMin[3] = {0.0f, 10.0f, 10.0f};
  // When a team has neither points nor anyone alive left, it bleeds like this
  // (1000 per minute in the original's constructor, that is almost instantly).
  float ticketLossAtEndPerMin = 1000.0f;
};

class GameServer {
 public:
  explicit GameServer(ServerSettings settings) : settings_(std::move(settings)) {}

  // Fills the world with the level's static objects. It is the server that
  // decides what is in the world — the client only learns it from the network.
  void loadWorld(const level::Level& level, std::size_t limit = 0);

  // The terrain, for collision with the ground. Without it a soldier falls forever.
  void setTerrain(const level::Level* level) { terrain_ = level; }

  // The level's collision geometry. The application builds it (it has the VFS and
  // the registry) and the server owns it — because it decides where the player got to.
  void setCollision(std::unique_ptr<CollisionWorld> world) { collision_ = std::move(world); }
  const CollisionWorld* collision() const { return collision_.get(); }

  // The mode's logic: control points and vehicle spawners.
  void setGameplay(level::GameplayObjects gameplay);

  // One point's capture state. The model is as in gpm_cq.py: the flag travels up
  // and down, and the owner changes only when the flag is at the bottom.
  struct ControlPointState {
    int id = 0;
    std::string nameKey;
    Vec3f position;
    float radius = 10.0f;
    int team = 0;  // the owner; 0 is neutral

    int flagTeam = 0;        // whose flag is on the pole right now
    float takeOver = 0.0f;   // 0 is the bottom, 1 the top
    float takeOverChangePerSecond = 0.0f;
    FlagPosition flagPosition = FlagPosition::Bottom;
    int occupantsTeam1 = 0;
    int occupantsTeam2 = 0;

    // From the ControlPoint template in GamePlayObjects.con.
    float timeToGetControl = 20.0f;
    float timeToLoseControl = 20.0f;
    float areaValueTeam1 = 0.0f;
    float areaValueTeam2 = 0.0f;
    bool unableToChangeTeam = false;
    int onlyTakeableByTeam = 0;
    int enemyTicketLossWhenCaptured = 0;

    // Compatibility with the previous shape: how much is left until the change.
    int capturingTeam() const { return flagTeam; }
    float progress() const { return takeOver; }
  };
  const std::vector<ControlPointState>& controlPoints() const { return controlPoints_; }

  GameStatus status() const { return status_; }
  const TeamState& team(int index) const { return teams_[index == 2 ? 2 : 1]; }
  int tickets(int index) const { return team(index).tickets; }
  // 0 means the round goes on, otherwise the winning team's number.
  int winner() const { return winner_; }
  float groundHeightAt(const Vec3f& position) const;

  // Kills a player: the team loses a ticket, then comes the wait and a new spawn.
  // Just as onPlayerDeath does in gpm_cq.py.
  void killPlayer(std::uint32_t playerId, std::string_view reason);

  // The player pressed DONE on the spawn screen: the team, the kit and the id of
  // the control point they want to spawn from. Zero in `spawnGroup` means
  // "any point of ours". The soldier itself appears on the next tick — just as
  // in the engine, where it is done by the `ServerGameLogic::uPlayingSpawning`
  // pass rather than by the event itself.
  bool requestSpawn(std::uint32_t playerId, int team, int kit, int spawnGroup);

  // Accepts a new connection. The server takes ownership of the channel.
  void accept(std::unique_ptr<net::Connection> connection);

  // Parses incoming packets and runs the simulation at a fixed step.
  // deltaSeconds is the frame's real time; inside it is accumulated.
  void tick(float deltaSeconds);

  std::uint64_t tickCount() const { return tickCount_; }
  float tickInterval() const { return 1.0f / settings_.tickRate; }

  const std::vector<WorldObject>& objects() const { return objects_; }
  std::size_t playerCount() const { return players_.size(); }
  const std::vector<Player>& players() const { return players_; }
  const ServerSettings& settings() const { return settings_; }

  long long packetsSent() const { return packetsSent_; }
  long long packetsReceived() const { return packetsReceived_; }
  const std::vector<std::string>& log() const { return log_; }

 private:
  void handlePacket(Player& player, const net::Packet& packet);
  void sendWorld(Player& player);
  void simulate(float step);
  void broadcastDynamic();
  WorldObject* findObject(std::uint32_t id);
  std::uint32_t spawnSoldier(Player& player);
  // Places vehicles from the spawners: the template depends on whose the point
  // the spawner is bound to is.
  void spawnVehicles();
  void updateControlPoints(float step);
  // Recomputes the flag's raising rate for one point.
  void refreshTakeOver(ControlPointState& point);
  // The flag reached an end: a capture or a neutralisation.
  void onFlagReachedEnd(ControlPointState& point, bool top);
  // The ticket bleed depending on who holds how many points.
  void updateTicketLoss();
  void updateTickets(float step);
  void endGame(int winner);
  // Where a player spawns: the nearest point of their own team, otherwise the start.
  // The point is picked by the engine's logic (see docs/functions/spawn.md).
  // `spawnGroup` is the id of the control point the choice is limited to. Zero
  // lifts the limit. A group in the engine is exactly the set of points of one
  // flag (`SpawnGroup::getControlPointId`), and within a group the point is taken
  // at random (`SpawnGroup::getSpawnPoint`).
  const level::SpawnPoint* pickSpawnPoint(int team, bool forHuman, int spawnGroup) const;
  bool spawnPointActive(const level::SpawnPoint& spawn, int team, bool forHuman) const;
  Vec3f chooseSpawn(int team, int spawnGroup);
  bool sendTo(Player& player, std::span<const std::byte> data);

  ServerSettings settings_;
  const level::Level* terrain_ = nullptr;
  std::unique_ptr<CollisionWorld> collision_;
  level::GameplayObjects gameplay_;
  std::vector<ControlPointState> controlPoints_;
  GameStatus status_ = GameStatus::PreGame;
  TeamState teams_[3];
  int winner_ = 0;
  std::vector<WorldObject> objects_;
  std::vector<Player> players_;
  std::uint32_t nextPlayerId_ = 1;
  std::uint32_t nextObjectId_ = 1;
  float accumulator_ = 0.0f;
  std::uint64_t tickCount_ = 0;
  long long packetsSent_ = 0;
  long long packetsReceived_ = 0;
  std::vector<std::string> log_;

  // The world's time — needed for the delay before spawning again at the same
  // point (`spawnPreventionDelay`), and how much of it has passed since the last spawn.
  float worldTime_ = 0.0f;
  std::map<const level::SpawnPoint*, float> lastSpawnTime_;
  // In the original the point is chosen at random (`rand() % count`). We keep our
  // own generator so the tests stay reproducible.
  mutable std::minstd_rand random_{12345};
};

}  // namespace obf2::server
