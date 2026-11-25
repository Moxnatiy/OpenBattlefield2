#pragma once
// What the server put in the world, on screen: other players' soldiers, the
// vehicles its spawners made, and a placeholder for whatever we cannot draw yet.
//
// The engine splits the same work between `GhostManager` (what exists and where)
// and `BF2FrameInterpolator` (where it is drawn between two ticks). A soldier is
// not drawn from the predicted point but carried by his own physics from it, once
// per game tick, and the frame blends the last two results — the same thing
// `FUN_0045c190` does (docs/functions/soldier-physics.md).
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "obf2/anim/player.h"
#include "obf2/anim/ragdoll.h"
#include "obf2/anim/system.h"
#include "obf2/app/camera.h"
#include "obf2/app/engine_sound.h"
#include "obf2/app/render_context.h"
#include "obf2/app/sound_envelope.h"
#include "obf2/app/sound_voices.h"
#include "obf2/game/engine_update.h"
#include "obf2/game/object_template.h"
#include "obf2/game/rotational_bundle.h"
#include "obf2/game/vehicle_seats.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/level.h"
#include "obf2/level/placement_index.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/skinning.h"
#include "obf2/server/physics.h"
#include "obf2/session/remote_world.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

class WorldView {
 public:
  struct Options {
    // `--draw-predicted`: draw other soldiers from the predicted point, as before
    // `carryRemoteSoldier`. A measuring switch, not a way to play.
    bool drawPredicted = false;
    // `--own-box`: a placeholder at our own soldier's position too. Not needed in
    // the game, but without it the placeholder cannot be checked on an empty server.
    bool showOwnBox = false;
    int geometryIndex = -1;
    int lodIndex = 0;
    // `--trace-frames <frame>:<frames>`: per frame, every soldier drawn.
    int traceFrom = -1;
    int traceFrames = 0;
    // `--watch-pose <n>`: `--watch-soldier` moves to the nearest soldier whose
    // ghost carries this pose (0x20: 1 crouch, 2 prone, 3 swim) whenever the one it
    // watches is not in it. A measuring switch: bots pick their poses themselves.
    int watchPose = -1;
    // `--watch-weapon <item>`: the same, for a soldier with this `itemIndex` out
    // (0x1000). Both together ask for both.
    int watchWeapon = -1;
    // `--watch-ragdoll`: the same, for a dead soldier whose ragdoll the server sends.
    bool watchRagdoll = false;
    // `--watch-seated`: a soldier sitting in a vehicle's open seat.
    bool watchSeated = false;
  };

  // The server's vehicles and strategic objects as the map shows them: what each
  // is (the template it was resolved to, by its creation spot or its number) and
  // where it is now. Only objects already met by the drawing are known.
  struct MapObject {
    std::string templateName;
    float x = 0.0f, z = 0.0f;
    float yaw = 0.0f;  // degrees
  };
  std::vector<MapObject> mapObjects() const;

  // The placeholders and the level's gameplay placement. False when the boxes did
  // not reach the card — then nothing of the ghost stream is drawn.
  bool init(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
            TextureResolver resolve, session::RemoteWorld& remote, const level::Level* level,
            const Options& options);

  // Everything the server created, added to this frame's draw list. `eye` is
  // where the frame is seen from: the engines' sounds are heard there.
  void collect(std::vector<gfx::MeshRenderer::DrawItem>& out, int frame, float frameStep,
               const Vec3f& eye);

  // The camera of the seat we sit in (docs/functions/vehicle-camera-modes.md):
  // its eye, its yaw and pitch in degrees and its field of view. `lookX/Y` is the
  // frame's look input, `cameraKey` a camera key pressed this frame
  // (gfx::Device::InputState::vehicleCameraKey). Nothing when we are on foot.
  struct VehicleView {
    CameraView camera;
    float yaw;
    float pitch;
    float fieldOfView;
  };
  std::optional<VehicleView> occupiedCamera(float lookX, float lookY,
                                            SDL_Scancode cameraKey = SDL_SCANCODE_UNKNOWN);

  // `--watch-soldier`: the soldier the camera holds on to. Zero when there is
  // none — the camera then stays where it was.
  std::uint16_t watchSoldier(const Vec3f& from);
  std::uint16_t watched() const { return watched_; }
  // Where a seated soldier is on screen (his seat's place), for the camera that
  // watches him; nothing when he is not in an open seat.
  std::optional<Vec3f> seatedAt(std::uint16_t soldier);

  // How each object was drawn: interpolated, extrapolated or from the newest
  // update, the largest step between frames, and the feet under the terrain.
  void report() const;
  // The one pool of sound sources (sound_voices.h), for the sounds the session
  // plays beside the world's (our weapon's, combat_feedback.h).
  SoundVoices& voices() { return voices_; }
  void release(gfx::MeshRenderer& renderer);

 private:
  // One soldier template and kit: the bind-pose mesh, the skeleton and the two
  // animation systems. Every soldier on screen then has his own posed copy,
  // because his legs are at his own point in the run.
  struct Look {
    // The bind pose on the GPU, uploaded once. It is never written again: the pose
    // reaches the vertex shader as the range's bone matrices, which is where the
    // original deforms a soldier too (`Shaders_client.zip:SkinnedMesh.fx:46`).
    gfx::GpuMesh gpu;
    mesh::Skeleton skeleton;
    // Shared by every look that names the same file (animationSystem).
    const anim::System* legs = nullptr;
    const anim::System* weapon = nullptr;
    bool ready = false;
  };

  // One soldier on screen: his own pose and his own place in the clips.
  struct DrawnSoldier {
    Look* look = nullptr;
    std::vector<mesh::Mat4> pose;
    anim::Player legs;
    anim::Player weapon;
    float fireClock = 0.0f;  // seconds since the last round's message
    anim::Player seat;  // his seat's `seatAnimationSystem`, while he sits
    std::string key;
  };

  // Another player's soldier as his physics carries him: the body, the game tick
  // it was last carried to, and where it stood before that tick.
  struct Carried {
    server::BodyState body;
    server::SwimState swim;
    std::uint32_t tick = 0;
    Vec3f from;
    bool ready = false;
  };

  struct Resolved {
    Vec3f at;
    std::uint32_t templateId;
    level::PlacedAt placed;
  };

  struct DrawStat {
    int frames[3] = {0, 0, 0};  // GhostPrediction: newest, extrapolated, interpolated
    Vec3f last;
    bool seen = false;
    float largestStep = 0.0f;
    // A soldier's feet under the terrain by more than 5 cm: as predicted, and as
    // drawn. The measure for "drops through the floor and pops back".
    int predictedUnder = 0;
    int drawnUnder = 0;
    // A soldier drawn with no clip playing — the bind pose, arms out — and of
    // those frames how many his player was in another object (a vehicle); the
    // highest his feet were drawn over the terrain. The measure for "bodies
    // hanging in the air".
    int bindPose = 0;
    int bindPoseInVehicle = 0;
    float highestAboveGround = 0.0f;
    // Frames his player sat in a vehicle (the player's object not a soldier), and
    // how many of his own ghost records came meanwhile.
    int framesInVehicle = 0;
    int recordsAtEntry = -1;
    int recordsInVehicle = 0;
    int framesHiddenInVehicle = 0;
    int framesSeated = 0;        // drawn in his seat
    int framesInClosedSeat = 0;  // in a seat with no `seatInformation`: not drawn
    std::uint16_t vehicle = 0;
  };

  // A seat's network id taken back to its vehicle (vehicle_seats.h): the
  // vehicle's object, the seat, and where it is on screen now. `drawing` takes
  // the hull's place from this frame's draw (vehicleDrawTransforms_).
  struct SeatAt {
    std::uint16_t vehicle = 0;
    const game::VehicleSeat* seat = nullptr;
    Mat4 place = Mat4::identity();
  };
  std::optional<SeatAt> seatOf(std::uint16_t seatId, bool drawing = false);
  // An object's rotation as drawn: its record's quaternion (conjugated — the
  // engine's are row-vector ones), else its spawner's, else none.
  Mat4 objectRotation(std::uint16_t id, const net::bf2::RemoteObject& object);
  const std::vector<game::VehicleSeat>& seatsOf(const std::string& vehicle);
  const anim::System* animationSystem(const std::string& path);

  const mesh::BoneAnimation* clipAt(const std::string& path);
  // A soldier, his kit and the `itemIndex` of the weapon he has out make one look.
  // `stowed`: no weapon in his hands, as in an open seat.
  Look* lookFor(const std::string& soldierName, const std::string& kitName, int weaponItem,
                bool stowed = false);
  static std::string lookKey(const std::string& soldierName, const std::string& kitName,
                             int weaponItem, bool stowed = false) {
    return soldierName + "|" + kitName + "|" + std::to_string(weaponItem) +
           (stowed ? "|stowed" : "");
  }
  // Carries one soldier to the current tick and gives the point to draw him at.
  Vec3f carry(std::uint16_t id, const net::bf2::RemoteObject& object,
              const net::bf2::GhostPose& pose);
  // The vehicle a created object is, by its template number, else by where it
  // was created.
  const gfx::GpuMesh* vehicleMesh(std::uint16_t id, const net::bf2::RemoteObject& object);
  // A template's mesh, built and uploaded once; a `PlayerControlObject` is cut
  // into its moving parts (uploadVehicleParts).
  const gfx::GpuMesh* worldObjectMesh(const std::string& name);
  // The vehicle's geometry seen from inside: the seat's `cockpitSubGeom`, or
  // the authored cockpit (game::authoredCockpitGeometry) when `geometry` < 0.
  const gfx::GpuMesh* cockpitMesh(const std::string& name, int geometry);

  // A dead soldier's body (docs/functions/ragdoll.md). The engine makes it in
  // `Soldier::enableRagDoll` (Linux 0x556bb0) from the pose he died in; who
  // starts that on a client — the soldier's +0x4a0, read in `handleUpdate`
  // 0x54bd81 — is not established, so ours is made at his first ragdoll record,
  // from the pose he was drawn in. It is fed every record after and drawn instead
  // of his animation, at its centre with no rotation, as `enableRagDoll` places
  // the soldier.
  struct Body {
    explicit Body(anim::Ragdoll made) : ragdoll(std::move(made)) {}
    anim::Ragdoll ragdoll;
    int fed = 0;  // ragdoll records already read into it
    // The measure for "bodies hanging in the air": frames drawn, frames with the
    // centre more than 0.6 m over the terrain, frames standing (the head 0.8 m
    // over both feet), and the highest the centre was.
    int frames = 0;
    int framesHigh = 0;
    int framesUpright = 0;
    float highest = 0.0f;
    // The same over whatever is under it — a roof as well as the terrain:
    // frames with the centre more than 0.6 m over the highest surface below it.
    int framesOverSurface = 0;
    float highestOverSurface = 0.0f;
    Vec3f last;
  };
  // Poses `drawn` by his body and gives the matrix to draw it at; nullopt when he
  // is not lying dead.
  std::optional<Mat4> poseBody(std::uint16_t id, const net::bf2::RemoteObject& object,
                               DrawnSoldier& drawn, const Mat4& place, float frameStep);

  FileSystem* files_ = nullptr;
  const game::Registry* registry_ = nullptr;
  gfx::MeshRenderer* renderer_ = nullptr;
  TextureResolver resolve_;
  session::RemoteWorld* remote_ = nullptr;
  Options options_;
  // Every sound's sources (BF2Audio.dll 0x10004c60, 0x10007140); declared
  // before the sounds so it outlives them.
  SoundVoices voices_;

  // A vehicle cut into the parts that move (uploadVehicleParts): its wheels, its
  // steered parts, its turrets and guns, its rotors and its engine.
  struct SteeringJoint {
    Mat4 rest;
    Vec3f minRotation;
    Vec3f maxRotation;
    bool reversed;
    // The joint's networked bundle in the driver seat's descriptor
    // (JEEP_FAAV_nav_LF, BasicInfo), when it is decoded.
    std::optional<std::size_t> bundle;
    // The bundle's own template, stepped for the vehicle we drive
    // (RotationalBundle::calculateAndClipAngle, Linux 0x585a40).
    game::RotationalSettings settings;
    mutable game::RotationalState own;
    mutable float ownPending = 0.0f;  // seconds not yet stepped, at the 1/30 tick
  };
  struct SteeringPiece {
    const gfx::GpuMesh* mesh;
    SteeringJoint joint;
  };
  struct Wheel {
    const gfx::GpuMesh* mesh;
    Vec3f pivot;
    Vec3f axle;
    float radius;
    std::optional<SteeringJoint> steering;
    std::vector<Vec3f> contactVertices;
  };
  using TurretJoint = game::VehicleSeatJoint;
  struct TurretPiece {
    const gfx::GpuMesh* mesh;
    std::vector<TurretJoint> joints;
    Mat4 place = Mat4::identity();
  };
  struct VehicleParts {
    std::vector<Wheel> wheels;
    std::vector<SteeringPiece> steeringPieces;
    float wheelbase = 0.0f;
    float steeringRange = 0.0f;
    bool groundSuspension = false;
    std::vector<TurretPiece> turretPieces;
    // The engine's sounds, `S_<engine>_Idle`, `_Rpm1`, `_Rpm2`
    // (docs/functions/engine-sound.md). Idle is read but not played yet.
    struct EngineVoice {
      SDL_AudioSpec format{};
      std::vector<Uint8> sample;
      float volume = 1.0f;
      float pitch = 1.0f;
      bool is3d = true;
      SoundEnvelope volumeEnvelope;
      SoundEnvelope pitchEnvelope;
      SoundDistance distance;
    };
    std::array<EngineVoice, 3> engineVoices;
    EngineSoundSettings engineSettings;
    unsigned engineType = 0;  // `setEngineType`
    // The engine template as a RotationalBundle and as an engine, for the
    // vehicle we drive (Engine::handleUpdate, Linux 0x62f020).
    game::RotationalSettings engineBundle;
    game::EngineUpdateSettings engineUpdate;
    bool hasEngine = false;
  };
  // A drawn vehicle from one frame to the next.
  struct VehicleMotion {
    Vec3f position;
    Vec3f forward;
    float steering = 0.0f;
    float distance = 0.0f;
    float speed = 0.0f;
    int frame = -1;
    const gfx::GpuMesh* model = nullptr;
    struct EnginePlay {
      std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{nullptr, SDL_DestroyAudioStream};
      SoundVoice voice;  // declared after `stream`: given back before the stream goes
      float retry = 0.0f;  // seconds until a stolen sample asks again
    };
    std::array<EnginePlay, 3> engine;
    std::optional<EngineSound> engineSound;  // set while the engine runs
    float engineTick = 0.0f;  // time not yet given to EngineSound::update
    game::RotationalState engineBundle;  // our own engine's axis angles (+0x240)
    game::EngineUpdateState engineState; // our own engine's rpm, load, gear
    std::uint32_t engineRandom = 1;
  };
  static Mat4 steeringTransform(const SteeringJoint& joint, float steering);
  // The networked angle of the joint's bundle when there is one, else the
  // steering estimate.
  Mat4 steeringTransform(const SteeringJoint& joint, float steering, std::uint16_t vehicle) const;
  const gfx::GpuMesh* uploadVehicleParts(mesh::RenderMesh mesh, const std::string& name,
                                         bool cockpit = false);
  // The hull and every moving part, and the engine's sound; `occupied` runs the
  // engine. `hull` false leaves the hull out (a cockpit replaces it).
  void drawVehicle(const gfx::GpuMesh& model, std::uint16_t id, const Mat4& transform,
                   Vec3f eye, int frame, float step, bool occupied, bool hull,
                   std::vector<gfx::MeshRenderer::DrawItem>& items);
  Mat4 vehicleTurretTransform(const TurretPiece& piece, std::uint16_t root) const;
  // A seat bundle's angle as drawn: ours replayed from the actions the server
  // has not answered, anyone else's as the seat's networkable last set it.
  std::optional<Vec3f> displayedTurretAngle(std::uint16_t root, unsigned seat, std::size_t bundle) const;
  std::uint16_t cameraSeat_ = 0;
  int cameraRound_ = -1;
  float cameraLookYaw_ = 0.0f;
  float cameraLookPitch_ = 0.0f;
  // The vehicle camera's view mode, CameraViewMode numbering (Linux
  // operator<<(CameraViewMode) 0x8c1460): 1 Inside, 3 Chase, 4 FrontChase, 5 FlyBy.
  // Nose (2) and the traces (6, 7) are not drawn yet.
  int cameraViewMode_ = 1;
  bool externalCamera_ = false;  // cameraViewMode_ != Inside
  // Chase state (Camera +0x2d4 direction): slerped each frame.
  std::optional<Vec3f> chaseDirection_;
  std::chrono::steady_clock::time_point chaseUpdated_{};
  // FlyBy's recorded point (Camera +0x2f0 frames); cleared when the mode changes.
  std::optional<Vec3f> flyByEye_;
  std::optional<int> cockpitGeometry_;
  std::unordered_map<const gfx::GpuMesh*, VehicleParts> vehicleParts_;
  std::unordered_map<std::uint16_t, VehicleMotion> vehicleMotion_;
  // Each vehicle's place this frame, for the soldiers seated in it.
  std::unordered_map<std::uint16_t, Mat4> vehicleDrawTransforms_;
  std::map<std::string, std::vector<game::VehicleSeat>> seatsByVehicle_;
  mutable std::map<std::string, std::vector<game::RotationalSettings>> localTurretSettings_;
  std::map<std::string, std::optional<anim::System>> animationSystems_;

  gfx::GpuMesh boxes_[3];
  bool boxesReady_ = false;
  std::optional<level::PlacementIndex> placement_;
  // The meshes are built the first time a vehicle type is seen and kept in a
  // deque, whose elements stay put: the frame's draw list holds pointers to them
  // while new ones are still being added in the same loop.
  std::deque<gfx::GpuMesh> vehicleMeshes_;
  std::unordered_map<std::string, const gfx::GpuMesh*> vehicleByName_;
  std::unordered_map<std::uint16_t, Resolved> resolved_;
  std::set<std::uint16_t> boxReported_;
  int firedAnimations_ = 0;  // round messages sent to other soldiers' weapons
  std::set<std::uint16_t> rotationReported_;  // the record's yaw against the spawner's, once
  std::unordered_map<std::uint16_t, Carried> carried_;
  std::unordered_map<std::string, Look> looks_;
  // A clip that could not be read is remembered as empty so it is not looked for
  // every frame.
  std::unordered_map<std::string, std::optional<mesh::BoneAnimation>> clips_;
  std::map<std::uint16_t, DrawnSoldier> drawn_;
  std::map<std::uint16_t, DrawStat> stats_;
  std::uint16_t watched_ = 0;
  // The soldiers' ragdoll template, read once from the game's data.
  std::optional<anim::RagdollTemplate> ragdollTemplate_;
  bool ragdollTried_ = false;
  std::map<std::uint16_t, Body> bodies_;
  // Frames a body was drawn, and of them those with a particle a face separates
  // from the body's centre — through a wall.
  int bodyFrames_ = 0;
  int bodyFramesThroughFace_ = 0;
};

}  // namespace obf2::app
