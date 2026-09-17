#pragma once
// The live link to a real BF2 server: the handshake, the world it keeps and the
// stream of our own actions.
//
// It was `RemoteWorld` in `src/app/main.cpp`, where it had grown to 1382 lines of
// the entry point (CLAUDE.md, rule 11). It does not live under `net/` the way the
// plan there says, and for a plain reason: it needs `obf2::server` for the
// soldier's physics, and `obf2_server` already links `obf2_net`, so putting it
// there would close a cycle. It sits above all of them instead.
#include <algorithm>
#include <array>
#include <cctype>
#include <utility>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/core/platform.h"
#include "obf2/engine/engine.h"
#include "obf2/game/object_mesh.h"
#include "obf2/game/object_template.h"
#include "obf2/game/template_numbers.h"
#include "obf2/level/level.h"
#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_join.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/bf2_world.h"
#include "obf2/net/md5.h"
#include "obf2/net/udp.h"
#include "obf2/server/collision_objects.h"
#include "obf2/server/collision_world.h"
#include "obf2/server/physics.h"
#include "obf2/server/soldier_move.h"
#include "obf2/server/soldier_look.h"
#include "obf2/server/soldier_sprint.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::session {

// What the session takes from the command line. It used to read the whole `Args`
// of `main.cpp`; these ten fields are all of it that ever mattered.
struct Settings {
  std::string connectTo;
  std::string connectPassword;
  std::string playerName;
  std::string modDir;
  std::string recordTo;      // --record: where to write the captured traffic
  int ordinal = -1;          // --ordinal: which fingerprint of the archive list
  bool blockReady = false;
  bool skipContent = false;
  bool skipDatabase = false;
  bool startSimulation = false;
  // --trace-own-state: every controlled-object state of our soldier, with the
  // physics node's fields it carries and our prediction of the same tick.
  bool traceOwnState = false;
};

struct KnownObject {
  std::string name;
  obf2::Vec3f position;
};

// Why an object is or is not drawn. It belongs to the geometry rather than to
// the network, so it lives with the mesh assembly (`obf2/game/object_mesh.h`);
// the session only reports what it is told.
using game::DrawStage;
using game::drawStageName;

struct ContentHashes {
  std::array<std::byte, 16> misc{};
  std::array<std::byte, 16> archives{};
  std::array<std::byte, 16> level{};
};
std::vector<KnownObject> buildKnownObjects(FileSystem& files, const std::string& levelName,
                                          std::string* error);
// The named object standing at a point, if one does within `tolerance` metres.
const KnownObject* nearestKnown(const std::vector<KnownObject>& known, const Vec3f& at,
                                float tolerance = 2.0f);
std::optional<ContentHashes> contentHashes(FileSystem& files, const std::string& levelName,
                                           int ordinal);
game::Registry buildRegistry(FileSystem& files);


// The link to a real BF2 server, living together with the window: one and the same
// connection first carries the handshake through and then runs in the frame loop.
// That is what the original does too — a session does not end at the server
// accepting us.
struct RemoteWorld {
  RemoteWorld(Settings a, FileSystem& f) : args(std::move(a)), files(f) {
    world.setOwnName(args.playerName);
    // The connection type the player's profile names, sent as the original does
    // (`ConnectionTypeEvent`, bf2_protocol.h). No profile — no event, and the
    // server keeps its lowest row, as before.
    {
      obf2::engine::Console console;
      obf2::engine::Settings profile;
      profile.bind(console);
      const std::string documents = obf2::userDocumentsDirectory();
      if (!documents.empty()) {
        const std::string read =
            obf2::engine::loadDefaultProfile(documents + "/Battlefield 2/Profiles", console);
        if (!read.empty()) {
          connectionType = profile.general.connectionType;
          std::printf("  profile %s: connection type %d\n", read.c_str(), *connectionType);
        }
      }
    }
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
  std::optional<int> connectionType;
  // Whether a template's geometry assembles, for the report. It is the caller's,
  // because answering needs the mesh loaders and the flags they read
  // (`--geometry`, `--lod`), which belong to the application.
  std::function<DrawStage(const std::string&)> drawabilityOf;
  RemoteWorld(const RemoteWorld&) = delete;
  RemoteWorld& operator=(const RemoteWorld&) = delete;

  // Where to write the captured traffic (--record). Empty means we do not write.
  std::FILE* recording = nullptr;

  // By value, not by reference: the session outlives the block that built the
  // settings, and a reference there is the first of the rakes in CLAUDE.md.
  Settings args;
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
  obf2::game::TemplateNumbers templateNumbers;
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
  // When the first packet arrived, so the report can give a rate rather than a
  // count. The server's send rate is the connection type's own number
  // (`g_connectionTypes` column +4, 20 a second for type 5 — Linux server
  // `GameServer::setConnectionType` 0x45f5c0), and a count alone cannot be held
  // against it.
  float firstPacketMs = 0.0f;
  float lastPacketMs = 0.0f;
  // The stream as the server actually paces it: the gap in **its** ticks between
  // the packets we receive, because that is what the ghost samples are stamped
  // with (ghost_track.h).
  std::uint32_t previousPacketTick = 0;
  int packetGapSum = 0, packetGaps = 0, packetGapMax = 0;
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

  // The game mode and the size the server plays — which `GamePlayObjects.con`
  // its spawners come from (`gamemodes/<mode>/<size>`).
  std::string serverGameMode;
  int serverSize = 0;

  // The spawn screen's SUICIDE button (`spawnManager.commitSuicide`). Sent on the
  // next turn of the conversation.
  bool suicideRequested = false;
  void commitSuicide() { suicideRequested = true; }

  bool havePoint = false;
  float chosenX = 0.0f, chosenZ = 0.0f, chosenWorld = 2048.0f;
  // The action of the current tick: what was sent and what the body moved with.
  obf2::net::bf2::PlayerAction action;
  std::uint32_t actionTick = 0;

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
  obf2::server::CollisionWorld* collision = nullptr;
  // Where a created object's collision comes from; nothing is added without it.
  obf2::server::CollisionLibrary* collisionLibrary = nullptr;
  obf2::server::PhysicsConstants physics;

  // The objects the server created that the level's placement does not already
  // hold — a vehicle an ObjectSpawner made — kept in the collision world as long
  // as the server keeps them, at the place their newest update gives.
  struct MovableCollision {
    std::uint32_t handle = 0;
    obf2::Vec3f position;
  };
  std::map<std::uint16_t, MovableCollision> movableCollision;
  // Networked ids decided to have nothing to add, so they are not asked again.
  std::set<std::uint16_t> noMovableCollision;

  void syncMovableCollision() {
    if (collision == nullptr || collisionLibrary == nullptr) return;
    const auto& objects = world.objects();
    for (auto it = movableCollision.begin(); it != movableCollision.end();) {
      if (objects.count(it->first) == 0) {
        collision->removeMovable(it->second.handle);
        it = movableCollision.erase(it);
      }
      else {
        ++it;
      }
    }
    for (auto it = noMovableCollision.begin(); it != noMovableCollision.end();) {
      it = objects.count(*it) == 0 ? noMovableCollision.erase(it) : std::next(it);
    }
    for (const auto& [id, object] : objects) {
      if (object.templateId == 0 || noMovableCollision.count(id) != 0) continue;
      // A soldier meets another soldier by `checkSoldierVsSoldier` (Linux 0x6f4150),
      // not by his mesh.
      if (object.netClass == obf2::net::bf2::GhostClass::Soldier) {
        noMovableCollision.insert(id);
        continue;
      }
      const obf2::Mat4 transform = [&] {
        obf2::Mat4 m = obf2::translation(object.position);
        // The create event's rotation, in the level's yaw/pitch/roll form
        // (bf2_events.h). Its sign convention against the level's is checked only
        // on a zero rotation; the object's own quaternion (0x1 in its update) is
        // not read, so a turned vehicle keeps it.
        if (object.createdRotation) {
          m = m * obf2::rotationYawPitchRoll(object.createdRotation->x, object.createdRotation->y,
                                             object.createdRotation->z);
        }
        return m;
      }();
      const auto found = movableCollision.find(id);
      if (found != movableCollision.end()) {
        const obf2::Vec3f& was = found->second.position;
        if (was.x != object.position.x || was.y != object.position.y ||
            was.z != object.position.z) {
          collision->placeMovable(found->second.handle, transform);
          found->second.position = object.position;
        }
        continue;
      }
      const std::string* name = templateNumbers.nameOf(object.templateId);
      if (name == nullptr) continue;  // the numbers may not be built yet
      // A placed static the server networks (a destructible fence) is in the grid
      // already, from the level's placement.
      const KnownObject* placed = nearestKnown(known, object.createdAt);
      const auto sameName = [](const std::string& a, const std::string& b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                 return std::tolower(static_cast<unsigned char>(x)) ==
                        std::tolower(static_cast<unsigned char>(y));
               });
      };
      if (placed != nullptr && sameName(placed->name, *name)) {
        noMovableCollision.insert(id);
        continue;
      }
      const auto& pieces = collisionLibrary->soldierPieces(*name);
      if (pieces.empty()) {
        noMovableCollision.insert(id);
        continue;
      }
      const std::uint32_t handle = collision->addMovable(pieces, transform);
      movableCollision[id] = {handle, object.position};
      std::printf("  collision: object %u %s at %.2f %.2f %.2f rotation %.1f %.1f %.1f, %zu pieces\n",
                  id, name->c_str(), object.position.x, object.position.y, object.position.z,
                  object.createdRotation ? object.createdRotation->x : 0.0f,
                  object.createdRotation ? object.createdRotation->y : 0.0f,
                  object.createdRotation ? object.createdRotation->z : 0.0f, pieces.size());
    }
  }
  float maxSpeed = 3.9f;  // phy-soldier-run-speed; filled in from the game's data

  // A correction from the server: we put the body where the server sees it.
  void correct(const obf2::Vec3f& position) {
    observe(position);
    body.position = position;
    if (!bodyReady) body.velocity = obf2::Vec3f{};
    bodyReady = true;
  }

  // Where the server sees our feet, against where the prediction put them —
  // without moving the body. The server's state is a few ticks old, and the
  // original replays the actions it has not answered yet on top of it (the
  // prediction component at the end of the controlled-object state, after the
  // networkables, 0x5b9860); until that is reversed, taking the server's
  // position as it is would throw a running soldier back every packet.
  void observe(const obf2::Vec3f& position) {
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
  }

  // The input gathered since the last tick. The mouse **adds up** — every frame's
  // movement belongs to the next action; the rest is the latest state of the keys.
  float pendingLookX = 0.0f;  // axis units
  float pendingLookY = 0.0f;
  obf2::net::bf2::PlayerAction pendingKeys;
  // Where the soldier looks — the body, the aim's offset, the pitch
  // (`soldier_look.h`), turned only by the quantized actions. Ours looks up with a
  // positive pitch, the engine's down, so `pitch()` turns the sign.
  obf2::server::SoldierLook look{0.0f, 0.0f, 0.0f, 10.0f};
  float yaw() const { return look.yaw(); }
  float pitch() const { return -look.pitch; }

  // A button pressed for one frame between two ticks (a jump is an edge) would be
  // lost if only the latest frame counted, so presses gather until a tick takes them.
  std::uint32_t pendingPresses = 0;
  std::string lastLookReport;

  void queueInput(float lookAxisX, float lookAxisY, const obf2::net::bf2::PlayerAction& keys) {
    pendingLookX += lookAxisX;
    pendingLookY += lookAxisY;
    pendingKeys = keys;
    pendingPresses |= keys.buttons;
  }

  // One frame of the player's own simulation, cut into whole 1/30 s ticks
  // (`WorldPref::mTickTime`). Every tick does what `FUN_005c0260` does in the
  // original: one action is built from the input, quantized for the wire
  // (`axisToWire`, 0x5bc890), turned back (`axisFromWire`, 0x5bc6a0), and **that**
  // turns the look and moves the body — and the same action is sent. So the server
  // and our screen play the same numbers.
  // How many ticks a frame may run — the client's frame, `FUN_004d5740`: when a
  // frame has more ticks due than the allowance at `+0x148`, it runs **one** and
  // the allowance drops to 1; otherwise the allowance grows by one per frame, up
  // to 3. The ticks not run are dropped. Without it a slow frame (a mesh being
  // uploaded) sent the server a burst, and its action buffer, which plays one per
  // tick (`FUN_004cc400`), kept that backlog for the rest of the life.
  int tickAllowance = 1;

  // What a frame shows between ticks — `BF2FrameInterpolator`. The frame loop
  // (`FUN_0040ca80`) stores the objects' state before it runs the frame's ticks
  // (`storeObjectStates`, 0x45bbb0, with the tick count at `+4`), and before
  // drawing blends each stored state toward the current one (`FUN_0045c190`):
  // `f = (now - tick start) / (ticks / 30)`, clamped to -0.1..1.1 (0xbdcccccd,
  // 0x3f8ccccd), per object by `FUN_0045bfc0`. The soldier's camera is not
  // blended: `FUN_0045c190` hands it the input device's mouse movement of this
  // frame (component 0xc4d7, `+0x94`) — so the look turns every frame.
  obf2::Vec3f drawFrom;
  int drawTicks = 0;

  float drawFraction() const {
    if (drawTicks <= 0) return 1.0f;
    const float f = tick.pending / (static_cast<float>(drawTicks) * obf2::server::kTickTime);
    return std::max(-0.1f, std::min(1.1f, f));
  }
  std::optional<obf2::Vec3f> drawnSoldierPosition() const {
    const auto at = soldierPosition();
    if (!at || !bodyReady || drawTicks <= 0) return at;
    return drawFrom + (body.position - drawFrom) * drawFraction();
  }
  // The look this frame: the last tick's plus the mouse not yet in an action.
  float cameraYaw() const { return yaw() + pendingLookX * physics.lookFactorX; }
  float cameraPitch() const {
    return std::max(-physics.lookMaxPitch,
                    std::min(physics.lookMaxPitch, pitch() - pendingLookY * physics.lookFactorY));
  }

  void predict(float step) {
    int ticks = tick.take(step, 1 << 20);
    if (ticks > tickAllowance) {
      ticks = 1;
      tickAllowance = 1;
    } else if (tickAllowance < 3) {
      ++tickAllowance;
    }
    if (ticks > 0) {
      drawFrom = body.position;
      drawTicks = ticks;
    }
    for (int i = 0; i < ticks; ++i) {
      obf2::net::bf2::PlayerAction next = pendingKeys;
      next.buttons |= pendingPresses;
      pendingPresses = 0;
      next.axes[obf2::net::bf2::kAxisMouseX] = obf2::net::bf2::axisToWire(pendingLookX);
      next.axes[obf2::net::bf2::kAxisMouseY] = obf2::net::bf2::axisToWire(pendingLookY);
      // What truncation left over stays for the next tick. This is ours, not the
      // binary's: the link "pixels -> axis" is not reversed (`--mouse-scale`), and
      // without the carry a float like 3 * 0.02 * 100 = 5.9999 would lose a unit.
      pendingLookX -= obf2::net::bf2::axisFromWire(next.axes[obf2::net::bf2::kAxisMouseX]);
      pendingLookY -= obf2::net::bf2::axisFromWire(next.axes[obf2::net::bf2::kAxisMouseY]);
      action = next;

      // The client's tick loop moves the game tick by one (bf2_world.h).
      world.advanceGameTick();
      // The measure of the action queue: how many of ours the server has not
      // answered, every ten seconds of ticks. A number that keeps growing is a
      // queue on the server that never drains.
      // Every ten seconds of ticks. Without a soldier `actionTick` stands at zero,
      // and `0 % 300` is zero on every one of them — which turned this report into
      // thirty lines a second in the frame loop.
      if (ourSoldier != 0 ? actionTick % 300 == 0 : world.gameTick() % 300 == 0) {
        // How long the server takes to answer an action, and how far our clock
        // therefore runs ahead of the newest packet. The ghosts of other players
        // are drawn at our clock minus `GSInterpolationTime` (100 ms), so a round
        // trip above that is what turns their movement from interpolated into
        // extrapolated — and extrapolation is what jerks when the next update
        // disagrees.
        if (roundTrips > 0) {
          std::printf("  round trip: %.0f ms on average, at most %.0f ms, over %d answers\n",
                      roundTripSum / static_cast<float>(roundTrips), roundTripMax, roundTrips);
          roundTripSum = 0.0f;
          roundTripMax = 0.0f;
          roundTrips = 0;
        }
        std::printf("  actions: tick %u, unanswered %zu, game tick %u, newest packet tick %u\n",
                    actionTick, sent.size(), world.gameTick(), world.newestPacketTick());
      }
      // No soldier, no actions: the game tick runs, nothing is built or sent.
      if (ourSoldier == 0) continue;
      const std::uint32_t number = actionTick++;
      sent.push_back({number, action});
      sentAt[number % sentAt.size()] =
          std::chrono::duration<float, std::milli>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      // Nothing answers while there is no soldier; the list is not allowed to grow
      // for a whole spawn screen. Ours, not the binary's.
      while (sent.size() > 256) sent.pop_front();

      playAction(number, action);
      sendAction();
      if (bodyReady && terrain != nullptr) {
        // Where the body is, every two seconds of ticks, for the first minute of a
        // life: the measure for "the soldier hangs in the air".
        if (++bodyTicks % 60 == 0 && bodyTicks <= 1800) {
          std::printf("  body: %.2f %.2f %.2f, terrain %.2f, on the ground %s, falling %.2f m/s\n",
                      body.position.x, body.position.y, body.position.z,
                      terrain->groundHeightAt(body.position), body.onGround ? "yes" : "no",
                      body.velocity.y);
        }
      }
    }
  }

  // Every action sent, oldest first, with its tick number — the list the original
  // keeps in the player's action buffer (`FUN_005bc590` adds, `FUN_005bc530` drops
  // what the server has answered).
  std::deque<std::pair<std::uint32_t, obf2::net::bf2::PlayerAction>> sent;
  // When each action was sent, by its tick — for the round trip the report prints.
  std::array<float, 256> sentAt{};
  float roundTripSum = 0.0f;
  float roundTripMax = 0.0f;
  int roundTrips = 0;

  // One action applied to our soldier: the look, then one tick of movement.
  void playAction(std::uint32_t number, const obf2::net::bf2::PlayerAction& played) {
    // The body's matrix still stands where the previous tick left it: the input
    // (0x5adea0) turns the body first and builds the movement from that older
    // matrix. Measured on the live server: the reported velocity's heading is the
    // look (body plus aim offset) two states back — strafing with the aim at -40,
    // the heading is body + aim - 90 of two ticks before, not body - 90.
    const float matrixYaw = look.yaw();
    // The input reads the sprint as the previous tick's update left it; this
    // action's sprint key reaches the sprint only after (below). Measured on the
    // live server: the tick whose action lets go of sprint and presses strafe still
    // has its strafe dropped, and the state after it reports the flag down.
    const bool sprinting = sprint.sprinting;
    obf2::server::turnSoldier(
        look, obf2::net::bf2::axisFromWire(played.axes[obf2::net::bf2::kAxisMouseX]),
        obf2::net::bf2::axisFromWire(played.axes[obf2::net::bf2::kAxisMouseY]),
        obf2::net::bf2::axisFromWire(played.axes[obf2::net::bf2::kAxisThrottle]), physics);
    if (bodyReady && terrain != nullptr && stepBody(played, matrixYaw, sprinting)) {
      // 0x54fe1d: the jump's stamina, straight off.
      sprint.stamina = std::max(0.0f, sprint.stamina - sprint.lossAtJump);
    }
    bodyAt[number % bodyAt.size()] = {number, body};

    // The sprint message (`FUN_005c0460`): while the player's sprint is on, 0x2a on
    // the tick it came on, 0x29 after. The player's sprint is the key with the
    // throttle forward — measured on the live server (shift and strafe alone never
    // raise the flag, letting go of forward drops it with the smoothed axis still
    // at 0.979); the code that sets it (`Player::setSprintState`) is not found.
    const bool sprintKey = sprintInputOf(played);
    if (sprintKey) obf2::server::sprintMessage(sprint, sprintKeyLastTick);
    sprintKeyLastTick = sprintKey;
    // `Soldier::handleUpdate` hands the recharge delay a jump started (0x54b6f5).
    // Not modelled: the blocking argument.
    obf2::server::updateSprint(sprint, false, body.sprintRechargeDelay, obf2::server::kTickTime);
  }
  obf2::server::SprintState sprint;
  bool sprintKeyLastTick = false;

  static bool sprintInputOf(const obf2::net::bf2::PlayerAction& action) {
    return (action.buttons & obf2::net::bf2::kButtonSprint) != 0 &&
           action.axes[obf2::net::bf2::kAxisThrottle] > 0;
  }

  // The facing of a body yaw in degrees: a zero angle looks along +Z — the same as
  // the server computes, so "right" is the angle plus 90 degrees.
  static obf2::Vec3f facingOf(float degrees) {
    const float radians = degrees * (3.14159265358979323846f / 180.0f);
    return {std::sin(radians), 0.0f, std::cos(radians)};
  }
  static obf2::Vec3f rightOf(float degrees) {
    const float radians = degrees * (3.14159265358979323846f / 180.0f);
    return {std::cos(radians), 0.0f, -std::sin(radians)};
  }
  // The speed state (`Soldier::updateSpeedState`, Linux 0x54e8d0): standing, a
  // sprinting soldier takes `phy-soldier-sprint-speed`, the rest run.
  float speedOf(bool sprinting) const { return sprinting ? physics.sprintSpeed : maxSpeed; }

  // The server's state of our soldier, and the actions it has not played yet on
  // top of it — `FUN_004d4b30`, the prediction component the controlled-object
  // state ends with (0x5b9860 passes it the counter and a tick of 1/30):
  //
  //   the networkable has already set the state (0x62d4e0, type 3);
  //   `FUN_005bc530(counter)` drops every action older than the counter and keeps
  //   the one equal to it;
  //   `FUN_004cbf60` on the same buffer (0x4d4bce) takes that one off too — the
  //   counter is the tick of the action the server has **played**: the server
  //   writes it from its player's action buffer `+0x10` (`FUN_005b7390`), which
  //   0x4cbf60 sets to the tick of the action it pops to play (`FUN_004cc400`);
  //   every action left is played again, with the same tick.
  //
  // We used to keep the counter's action and play it a second time: on a fast
  // turn that is one large mouse movement too many, taken back by the next state.
  void reconcile(const obf2::net::bf2::SoldierState& state, std::int32_t counter) {
    if (!state.position || !bodyReady) return;
    if (args.traceOwnState) traceOwnState(state, counter);
    const obf2::Vec3f predicted = body.position;
    const float predictedYaw = yaw();

    // The position is the pivot, `coll-soldier-pivot-height` above the feet
    // (`FUN_006ed4c0`); our body's position is the feet.
    obf2::Vec3f feet = *state.position;
    feet.y -= physics.pivotHeight;
    // What the state does not carry — the surface speed a jump leaves standing, the
    // sticking flag, the ground's normal, the jump's timers and delays — is taken
    // from our own prediction of the same tick; the client's copies are its own
    // too, never networked.
    if (counter >= 0) {
      const BodyRecord& ours = bodyAt[static_cast<std::uint32_t>(counter) % bodyAt.size()];
      if (ours.number == static_cast<std::uint32_t>(counter)) body = ours.body;
    }
    body.position = feet;
    if (state.velocity) body.velocity = *state.velocity;
    // The physics node, as `SoldierNetworkable::setNetUpdate` puts it back (Linux
    // 0x5dd7d6, 0x5de45d, 0x5de440, 0x5de47a), and the ground flag (0x5dd90b).
    // The friction is the velocity the server's input asked for, still to be taken
    // by the next step: `(surface speed - velocity) * 30` (0x6f3390).
    if (state.vector100) body.acceleration = *state.vector100;
    if (state.vector200) body.friction = *state.vector200;
    if (state.vector40000) body.linearSpeed = *state.vector40000;
    body.onGround = state.bitD;
    body.frictionContacts = 0;
    // The smoothed movement axes, `Soldier +0x22c` and `+0x228` (0x400, 0x800 in
    // the state): without them the replay starts every ramp from our own guess.
    if (state.value400) body.forwardAxis = *state.value400;
    if (state.value800) body.strafeAxis = *state.value800;
    // The angles, each in its own field (soldier_state.h, `soldier_look.h`).
    if (state.bodyYaw) look.bodyYaw = *state.bodyYaw;
    if (state.aimYaw) look.aimYaw = *state.aimYaw;
    if (state.angle8) look.turnLeft = *state.angle8;
    if (state.pitch) look.pitch = *state.pitch;
    // The sprint and its stamina (0x4000). The flag is the one the next tick's
    // input reads.
    if (state.flag4000) sprint.sprinting = *state.flag4000;
    if (state.value4000) sprint.stamina = *state.value4000;

    // How long this answer took: the action the counter names was sent by us, and
    // the state that answers it is here now.
    if (counter >= 0) {
      const float sentMs = sentAt[static_cast<std::uint32_t>(counter) % sentAt.size()];
      if (sentMs > 0.0f) {
        const float now = std::chrono::duration<float, std::milli>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
        const float trip = now - sentMs;
        if (trip >= 0.0f && trip < 5000.0f) {
          roundTripSum += trip;
          roundTripMax = std::max(roundTripMax, trip);
          ++roundTrips;
        }
      }
    }

    while (!sent.empty() && static_cast<std::int64_t>(sent.front().first) < counter) {
      sent.pop_front();
    }
    // 0x4d4bce: the head goes whatever its tick. A counter the server already
    // sent comes as -1 (`FUN_005b7390` against `+0x20e8`), and then this is the
    // one action it took off its buffer.
    if (!sent.empty()) {
      sprintKeyLastTick = sprintInputOf(sent.front().second);
      sent.pop_front();
    }
    for (const auto& [number, played] : sent) playAction(number, played);

    // The measure: how far the replayed body lands from where the prediction had
    // it. A right replay lands on the prediction, unless the server disagreed.
    const obf2::Vec3f replayed = body.position;
    body.position = predicted;
    observe(replayed);
    body.position = replayed;
    const float yawJump = std::remainder(yaw() - predictedYaw, 360.0f);
    yawCorrectionMax = std::max(yawCorrectionMax, std::abs(yawJump));
    yawCorrectionSum += std::abs(yawJump);
    // The corrections a player would see: a turn of over half a degree or a move of
    // over 5 cm, the first thirty of a run, with the state's angles and axes.
    const float moved = obf2::length(replayed - predicted);
    if ((std::abs(yawJump) > 0.5f || moved > 0.05f) && ++bigCorrections <= 30) {
      std::printf("  big correction: counter %d, yaw %+.2f, moved %.3f m, replayed %zu; "
                  "server body %.2f aim %.2f 0x8 %.2f, axes %.3f %.3f, sprint %d stamina %.3f\n",
                  counter, yawJump, moved, sent.size(), state.bodyYaw.value_or(-999.0f),
                  state.aimYaw.value_or(-999.0f), state.angle8.value_or(-999.0f),
                  state.value400.value_or(-99.0f), state.value800.value_or(-99.0f),
                  state.flag4000.value_or(false) ? 1 : 0, state.value4000.value_or(-1.0f));
    }
  }
  // What our prediction had after each action, by action tick — the other half of
  // `--trace-own-state`: the server's state names the action it played last, and
  // this is where we had the body after the same one.
  struct BodyRecord {
    std::uint32_t number = 0;
    obf2::server::BodyState body;
  };
  std::array<BodyRecord, 256> bodyAt{};

  void traceOwnState(const obf2::net::bf2::SoldierState& s, std::int32_t counter) {
    const auto vec = [](const std::optional<obf2::Vec3f>& v, char* out, std::size_t size) {
      if (v) std::snprintf(out, size, "%.4f %.4f %.4f", v->x, v->y, v->z);
      else std::snprintf(out, size, "-");
    };
    char velocity[64], acceleration[64], friction[64], linear[64];
    vec(s.velocity, velocity, sizeof(velocity));
    vec(s.vector100, acceleration, sizeof(acceleration));
    vec(s.vector200, friction, sizeof(friction));
    vec(s.vector40000, linear, sizeof(linear));
    std::printf("  own state %d: pos %.4f %.4f %.4f vel %s acc %s fric %s lin %s ground %d "
                "axes %.3f %.3f sprint %d stamina %.3f\n",
                counter, s.position->x, s.position->y, s.position->z, velocity, acceleration,
                friction, linear, s.bitD ? 1 : 0, s.value400.value_or(-99.0f),
                s.value800.value_or(-99.0f), s.flag4000.value_or(false) ? 1 : 0,
                s.value4000.value_or(-1.0f));
    if (counter < 0) return;
    const BodyRecord& ours = bodyAt[static_cast<std::uint32_t>(counter) % bodyAt.size()];
    if (ours.number != static_cast<std::uint32_t>(counter)) return;
    const obf2::Vec3f pivot = ours.body.position + obf2::Vec3f{0.0f, physics.pivotHeight, 0.0f};
    std::printf("  ours  %d: pos %.4f %.4f %.4f vel %.4f %.4f %.4f acc %.4f %.4f %.4f fric %.4f "
                "%.4f %.4f lin %.4f %.4f %.4f ground %d (off %.4f %.4f %.4f)\n",
                counter, pivot.x, pivot.y, pivot.z, ours.body.velocity.x, ours.body.velocity.y,
                ours.body.velocity.z, ours.body.acceleration.x, ours.body.acceleration.y,
                ours.body.acceleration.z, ours.body.friction.x, ours.body.friction.y,
                ours.body.friction.z, ours.body.linearSpeed.x, ours.body.linearSpeed.y,
                ours.body.linearSpeed.z, ours.body.onGround ? 1 : 0, pivot.x - s.position->x,
                pivot.y - s.position->y, pivot.z - s.position->z);
  }

  int bigCorrections = 0;
  float yawCorrectionMax = 0.0f;
  float yawCorrectionSum = 0.0f;

  bool stepBody(const obf2::net::bf2::PlayerAction& action, float matrixYaw, bool sprinting) {
    // The axes as the server reads them back from the wire (0x5bc6a0). A soldier
    // strafes with the yaw axis: the engine has no separate strafe axis. A
    // sprinting soldier does not strafe: `updateSoldierSpeed` zeroes that input
    // when `isSprinting` (0x5a7c50's first test; Linux 0x54ec75).
    const float forward = obf2::net::bf2::axisFromWire(action.axes[obf2::net::bf2::kAxisThrottle]);
    const float strafe =
        sprinting ? 0.0f : obf2::net::bf2::axisFromWire(action.axes[obf2::net::bf2::kAxisYaw]);
    // `Soldier::updateSoldierSpeed` (0x5a7c50): the axes are smoothed, the facing
    // is not.
    const obf2::Vec3f wish = obf2::server::soldierMoveDirection(
        body, forward, strafe, facingOf(matrixYaw), rightOf(matrixYaw), physics);

    obf2::server::SoldierIntent intent;
    intent.wish = wish;
    intent.forward = facingOf(matrixYaw);
    intent.right = rightOf(matrixYaw);
    intent.speed = speedOf(sprinting);
    intent.jump = (action.buttons & obf2::net::bf2::kButtonAction) != 0;

    // The engine's tick: the input, the node, the collision (`tickSoldier`). The
    // node's matrix, which the drag reads, is the body's after this action's turn
    // (`updateTransformation` runs in the input, Linux 0x55003b) — its yaw is taken
    // as the body's; the matrix's own rows are not measured.
    return obf2::server::tickSoldier(body, swim, intent, look.bodyYaw, physics, terrain, collision,
                                     obf2::server::kTickTime);
  }
  int bodyTicks = 0;

  // Send the newest actions — one packet per tick, as the original builds one
  // action per tick (`FUN_005c0260`). The layout is `PlayerActionManager::transmit`
  // (0x5bfbe0): the counter is the tick of the **first** set, and set i is tick
  // counter + i, oldest first — the receiver (0x5bfea0) numbers them that way and
  // plays only the ones newer than the last it played (0x5bf940). So the last
  // three actions go, and a lost packet costs nothing.
  //
  // Three identical copies were wrong in a way that hid: the receiver took them
  // for three consecutive ticks.
  void sendAction() {
    if (socket == nullptr || ourSoldier == 0 || sent.empty()) return;

    obf2::net::bf2::ExtendedHeader header;
    header.sequence = sequence++ & 0x3F;
    header.ack = lastServerSequence;
    header.ackBits = 0xFFFFFFFFu;
    obf2::net::bf2::PlayerActions stream;
    const std::size_t count = std::min<std::size_t>(3, sent.size());
    const auto first = sent.end() - static_cast<std::ptrdiff_t>(count);
    stream.tick = static_cast<std::int32_t>(first->first);
    for (auto it = first; it != sent.end(); ++it) stream.actions.push_back(it->second);
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
  // Which soldier the predicted body was placed for. A respawn is a new object.
  std::uint16_t placedSoldier = 0;
  // The spawn screen's camera — the controlled object while the player has no
  // soldier. Zero until the first control state names it.
  std::uint16_t cameraObject = 0;

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
    world.setOwnConnection(static_cast<int>(id));
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

  // One turn: advance the join chain and read what arrived. Waiting long is
  // only allowed outside a frame — inside one it would be a freeze.
  // One turn of the conversation: send what is due and **parse one packet**.
  // Returns whether there was a packet — because this has to be called until the
  // queue is empty. The socket's queue does not clear itself: taking one packet
  // per frame while the server sends more makes it grow, and we look at the world
  // as it was several seconds ago.
  bool pump(int timeoutMs) {
      // When to send the next step is decided by JoinSequence — every rule about
      // pauses and waiting is there, together with its test. Assembling the packet
      // and reporting that we sent it is what is left here.
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

      // A request the player makes outside the join chain. `NESuicide` travels
      // the same way as the chain's own events — `PostRemoteEvent` in the
      // network category — and the server answers it with `NEPlayerDead`.
      if (suicideRequested) {
        suicideRequested = false;
        obf2::net::bf2::ExtendedHeader header;
        header.sequence = sequence++ & 0x3F;
        header.ack = lastServerSequence;
        header.ackBits = 0xFFFFFFFFu;
        socket->send(obf2::net::bf2::writePostRemoteEvent(
            id, header, batch++, obf2::net::bf2::kNetworkCategory, obf2::net::bf2::kNetSuicide));
        std::printf("  step: suicide\n");
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
          lastPacketMs = std::chrono::duration<float, std::milli>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
          if (firstPacketMs == 0.0f) firstPacketMs = lastPacketMs;
          if (const auto paced = obf2::net::bf2::readGhostHeader(*more)) {
            if (previousPacketTick != 0 && paced->time > previousPacketTick) {
              const int gap = static_cast<int>(paced->time - previousPacketTick);
              packetGapSum += gap;
              packetGapMax = std::max(packetGapMax, gap);
              ++packetGaps;
            }
            previousPacketTick = paced->time;
          }

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
            // Before we play a soldier there are no actions, and 0x4d4b30 leaves the
            // tick alone; the original's tick then comes from somewhere not found.
            // Ours: the packet's server tick. Not reversed.
            if (sent.empty() || !playerSpawned) {
              if (const auto header = obf2::net::bf2::readGhostHeader(*more)) {
                world.setGameTick(header->time);
              }
            }
            // Our soldier's own state, and the replay of what the server has not
            // played yet on top of it (`reconcile`, `FUN_004d4b30`).
            if (state->soldier && playerSpawned && state->networkId == ourSoldier &&
                bodyReady && placedSoldier == ourSoldier) {
              const auto& s = *state->soldier;
              const float predictedYaw = yaw();
              reconcile(s, state->counter);
              // The game tick: this packet's server tick plus one per action played
              // again (`FUN_004d4b30`, 0x4d4bc9 and 0x4d4c15; bf2_world.h).
              if (const auto header = obf2::net::bf2::readGhostHeader(*more); header && !sent.empty()) {
                const std::uint32_t wanted = header->time + static_cast<std::uint32_t>(sent.size());
                // The measure for the jerking of other players: they are drawn at our
                // clock minus `GSInterpolationTime`, so a clock that leaps forward
                // drags every ghost along its velocity by the same leap.
                const std::int64_t jump =
                    static_cast<std::int64_t>(wanted) - static_cast<std::int64_t>(world.gameTick());
                if (jump > 2 || jump < -2) {
                  std::printf("  clock: %+lld ticks (%u -> %u, packet %u, unanswered %zu)\n",
                              static_cast<long long>(jump), world.gameTick(), wanted, header->time,
                              sent.size());
                }
                world.setGameTick(wanted);
              }
              // The look chain's measure (`--look-at`): the server's angles, our
              // prediction before the replay and after it. Printed when they change.
              char line[256];
              std::snprintf(line, sizeof(line),
                            "server body %.2f aim %.2f 0x8 %.2f pitch %.2f; ours %.2f -> %.2f %.2f",
                            s.bodyYaw.value_or(-999.0f), s.aimYaw.value_or(-999.0f),
                            s.angle8.value_or(-999.0f), s.pitch.value_or(-999.0f), predictedYaw,
                            yaw(), pitch());
              if (lastLookReport != line) {
                std::printf("  look: %s (counter %d, tick %u, unanswered %zu)\n", line,
                            state->counter, actionTick, sent.size());
                lastLookReport = line;
              }
            }
            // The server states the controlled object's number directly. But the
            // controlled object is not always a soldier: before spawning it is the
            // spawn screen's camera (number 257 on Dalian, with the position from
            // `setBeforeSpawnCamera`). The engine tells them apart by calling
            // `getSoldier` right after `getObject`, and we cannot do that yet — so
            // for now we only check the number against the enter event rather than
            // replacing it.
            // Before the first spawn the controlled object is the spawn screen's
            // camera, and it is the same object again after every death: on the
            // live server the control state names 258 before the first spawn and
            // 258 again the moment `NESuicide` is answered, one packet before
            // `NEPlayerDead`. So it is remembered, and never taken for a soldier.
            if (!playerSpawned && cameraObject == 0) cameraObject = state->networkId;
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
            if (playerSpawned && controlObject != 0 && controlObject != cameraObject &&
                ourSoldier != controlObject) {
              ourSoldier = controlObject;
              // A different soldier than the body was built for: a respawn whose
              // `NEPlayerDead` we missed or that arrived out of order. The same
              // rule — a new soldier gets a new body.
              if (bodyReady && placedSoldier != ourSoldier) bodyReady = false;
              std::printf("  our soldier by the controlled-object state: %u\n", ourSoldier);
            }
            // The body starts from the point the server named when it created the
            // soldier; after that only the prediction moves it. The server's own
            // position is read (above) and measured against it, not adopted — the
            // replay of unanswered actions is not reversed. Debt, not a decision.
            // Only after `NEPlayerSpawned`: before spawning we have no soldier,
            // and the enter event happens for other players too.
            if (!bodyReady && playerSpawned && ourSoldier != 0) {
              const auto born = objects.find(ourSoldier);
              if (born != objects.end()) {
                placedSoldier = ourSoldier;
                // The creation position is the pivot, as every state's is: measured on
                // the live server, a soldier created at 163.75 over terrain 162.50 is
                // stood at 163.4971 by his first state, falling at 2.843 m/s — 0.25 m
                // of fall from feet 1.0 under the creation point. Our body is the feet.
                obf2::Vec3f feet = born->second;
                feet.y -= physics.pivotHeight;
                // A new soldier starts from nothing: `Soldier::resetInstance` (Linux
                // 0x549df0) puts the air timers to -1, and the node starts with no
                // forces. The previous life's records must not be replayed into him:
                // on a respawn they carried its air steering into his first ticks.
                body = obf2::server::BodyState{};
                bodyAt.fill(BodyRecord{UINT32_MAX, obf2::server::BodyState{}});
                correct(feet);
                std::printf("  the body was placed at %.1f %.1f %.1f (the soldier's creation position), "
                            "terrain %.2f\n",
                            born->second.x, born->second.y, born->second.z,
                            terrain != nullptr ? terrain->groundHeightAt(born->second) : -1.0f);
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
            syncMovableCollision();
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
                  serverGameMode = info->gameMode;
                  serverSize = info->size;
                  // The challenge number no longer goes here: it is in block 2, while
                  // block 5's first number is something else.
                  std::string levelError;
                  if (!obf2::level::mountLevel(files, args.modDir, info->levelName, &levelError)) {
                    std::printf("  the level was not mounted: %s\n", levelError.c_str());
                  } else {
                    known = buildKnownObjects(files, info->levelName, &levelError);
                    registry = buildRegistry(files);
                    std::printf("  the level was read: known objects %zu\n", known.size());
                    // The server's template numbers (obf2/game/template_numbers.h).
                    if (const auto listed = files.read("ServerArchives.con")) {
                      const std::string text(reinterpret_cast<const char*>(listed->data()),
                                             listed->size());
                      std::vector<std::vector<std::string>> archives;
                      for (const auto& archive : obf2::game::archivesFromCon(text)) {
                        archives.push_back(files.archiveEntries(archive));
                      }
                      templateNumbers = obf2::game::TemplateNumbers::build(files, archives);
                      const auto lav = templateNumbers.numberOf("USAPC_LAV25");
                      std::printf("  template numbers: %zu, USAPC_LAV25 is %d\n",
                                  templateNumbers.size(), lav ? static_cast<int>(*lav) : -1);
                    }
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
                // Both events go to every client and name the player they are about
                // (RemoteEvent in bf2_events.h). Another player's are not ours.
                const bool aboutUs =
                    !remote.value || ourPlayer < 0 || *remote.value == ourPlayer;
                if (!aboutUs && (remote.number == obf2::net::bf2::kNetPlayerSpawned ||
                                 remote.number == obf2::net::bf2::kNetPlayerDead)) {
                  continue;
                }
                if (remote.number == obf2::net::bf2::kNetPlayerSpawned) {
                  playerSpawned = true;
                  std::printf("  THE PLAYER SPAWNED\n");
                }
                // A life is over. Everything the prediction held belonged to that
                // soldier: its position, its velocity, whether it was standing.
                // The next life is a **new object** with its own creation
                // position, and until now the body was placed only once per
                // connection — `!bodyReady` — so on respawn the new soldier was
                // simulated from wherever the old one died, mid-air included.
                if (remote.number == obf2::net::bf2::kNetPlayerDead) {
                  playerSpawned = false;
                  bodyReady = false;
                  bodyTicks = 0;
                  std::printf("  THE PLAYER DIED\n");
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
                const DrawStage stage =
                    drawabilityOf ? drawabilityOf(match->name) : DrawStage::NoTemplate;
                found = checked.emplace(match->name, stage).first;
                ++stageCounts[stage];
              }
              // Every created object once, with the number the server gave its
              // template: the pairs (number, what stands there) are the raw
              // material for the template table the protocol debt asks for.
              std::printf("  created: template %5u  id %5u  at %-44s %s\n",
                          event.object->templateId, event.object->networkId, match->name.c_str(),
                          std::string(drawStageName(found->second)).c_str());
            }
            if (event.player) {
              playerTeam[event.player->id] = static_cast<int>(event.player->team);
              std::printf("  player: %s (id %u, team %u)\n",
                          event.player->name.c_str(), event.player->id, event.player->team);
              // We are the player whose id is our connection id (bf2_world.h,
              // setOwnConnection). It used to be the name's tail, and a stale session
              // of ours kept "OpenBF2" while this one became "OpenBF2_0": we took the
              // stale id, and ignored our own `NEPlayerSpawned` — the "pressed DONE
              // quickly and did not appear".
              if (ourPlayer < 0 && static_cast<int>(event.player->id) == static_cast<int>(id)) {
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
              if (connectionType) {
                socket->send(obf2::net::bf2::writeConnectionTypeEvent(
                    id, nextHeader(), batch++, static_cast<std::uint32_t>(*connectionType)));
                std::printf("  ConnectionTypeEvent sent: %d\n", *connectionType);
              }

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

    const float listenedMs = lastPacketMs - firstPacketMs;
    const float listened = listenedMs > 0.0f ? listenedMs / 1000.0f : 0.0f;
    std::printf("  over %.1f seconds: pings %d (all answered), data packets %d (%zu bytes), "
                "other %d\n",
                listened, pings, dataPackets, dataBytes, other);
    // What the server's own pace is, in its ticks and in packets a second. The
    // connection type asks for twenty a second (type 5, `g_connectionTypes`
    // column +4), which is a gap of one and a half ticks.
    if (listened > 0.0f) {
      std::printf("  the server's pace: %.1f packets a second, %.1f ticks between them "
                  "(at most %d), over %d packets\n",
                  static_cast<float>(dataPackets) / listened,
                  packetGaps > 0 ? static_cast<float>(packetGapSum) / static_cast<float>(packetGaps)
                                 : -1.0f,
                  packetGapMax, packetGaps);
    }
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
      std::printf("    look: the replay moved the yaw %.2f degrees on average, at most %.2f\n",
                  yawCorrectionSum / n, yawCorrectionMax);
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
                  "%.2f m; records %d, with yaw %d, with velocity %d, read to the end %d, "
                  "yaw now %.1f\n",
                  id, object.updates, object.travelled,
                  object.aboveGround / static_cast<float>(object.updates), object.soldierRecords,
                  object.withYaw, object.withVelocity, object.complete,
                  object.yaw.value_or(-999.0f));
    }
    for (const auto& [object, player] : objectOwner) {
      std::printf("  player %u -> object %u: in the ghost records %s\n", player, object,
                  ghostObjects.count(object) ? "YES" : "NO");
    }
    for (const auto& [id, object] : world.objects()) {
      const obf2::Vec3f& at = object.position;
      const char* kind =
          object.netClass == obf2::net::bf2::GhostClass::Soldier        ? "soldier"
          : object.netClass == obf2::net::bf2::GhostClass::SimpleObject ? "object"
                                                                        : "class unknown";
      const char* side = object.team == 0                ? "no player"
                         : object.team == world.ownTeam() ? "our team"
                                                          : "THE ENEMY";
      std::printf("    object %5u  %8.1f %7.1f %8.1f  template %u, %s, %s%s\n", id, at.x, at.y,
                  at.z, object.templateId, kind, side,
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

}  // namespace obf2::session
