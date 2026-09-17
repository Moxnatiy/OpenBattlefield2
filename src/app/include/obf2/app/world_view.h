#pragma once
// What the server put in the world, on screen: other players' soldiers, the
// vehicles its spawners made, and a placeholder for whatever we cannot draw yet.
//
// The engine splits the same work between `GhostManager` (what exists and where)
// and `BF2FrameInterpolator` (where it is drawn between two ticks). A soldier is
// not drawn from the predicted point but carried by his own physics from it, once
// per game tick, and the frame blends the last two results — the same thing
// `FUN_0045c190` does (docs/functions/soldier-physics.md).
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "obf2/anim/player.h"
#include "obf2/anim/system.h"
#include "obf2/app/render_context.h"
#include "obf2/game/object_template.h"
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
  };

  // The placeholders and the level's gameplay placement. False when the boxes did
  // not reach the card — then nothing of the ghost stream is drawn.
  bool init(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
            TextureResolver resolve, session::RemoteWorld& remote, const level::Level* level,
            const Options& options);

  // Everything the server created, added to this frame's draw list.
  void collect(std::vector<gfx::MeshRenderer::DrawItem>& out, int frame, float frameStep);

  // `--watch-soldier`: the soldier the camera holds on to. Zero when there is
  // none — the camera then stays where it was.
  std::uint16_t watchSoldier(const Vec3f& from);
  std::uint16_t watched() const { return watched_; }

  // How each object was drawn: interpolated, extrapolated or from the newest
  // update, the largest step between frames, and the feet under the terrain.
  void report() const;
  void release(gfx::MeshRenderer& renderer);

 private:
  // One soldier template and kit: the bind-pose mesh, the skeleton and the two
  // animation systems. Every soldier on screen then has his own posed copy,
  // because his legs are at his own point in the run.
  struct Look {
    mesh::RenderMesh bind;
    // The bind pose on the GPU, uploaded once. It is never written again: the pose
    // reaches the vertex shader as the range's bone matrices, which is where the
    // original deforms a soldier too (`Shaders_client.zip:SkinnedMesh.fx:46`).
    gfx::GpuMesh gpu;
    mesh::Skeleton skeleton;
    std::optional<anim::System> legs;
    anim::System weapon;
    bool hasWeapon = false;
    bool ready = false;
  };

  // One soldier on screen: his own pose and his own place in the clips.
  struct DrawnSoldier {
    Look* look = nullptr;
    std::vector<mesh::Mat4> pose;
    anim::Player legs;
    anim::Player weapon;
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
  };

  const mesh::BoneAnimation* clipAt(const std::string& path);
  Look* lookFor(const std::string& soldierName, const std::string& kitName);
  // Carries one soldier to the current tick and gives the point to draw him at.
  Vec3f carry(std::uint16_t id, const net::bf2::RemoteObject& object,
              const net::bf2::GhostPose& pose);
  // The vehicle a created object is, by where it was created.
  const gfx::GpuMesh* vehicleMesh(std::uint16_t id, const net::bf2::RemoteObject& object);

  FileSystem* files_ = nullptr;
  const game::Registry* registry_ = nullptr;
  gfx::MeshRenderer* renderer_ = nullptr;
  TextureResolver resolve_;
  session::RemoteWorld* remote_ = nullptr;
  Options options_;

  gfx::GpuMesh boxes_[3];
  bool boxesReady_ = false;
  std::optional<level::PlacementIndex> placement_;
  // The meshes are built the first time a vehicle type is seen and kept in a
  // deque, whose elements stay put: the frame's draw list holds pointers to them
  // while new ones are still being added in the same loop.
  std::deque<gfx::GpuMesh> vehicleMeshes_;
  std::unordered_map<std::string, gfx::GpuMesh*> vehicleByName_;
  std::unordered_map<std::uint16_t, Resolved> resolved_;
  std::set<std::uint16_t> boxReported_;
  std::unordered_map<std::uint16_t, Carried> carried_;
  std::unordered_map<std::string, Look> looks_;
  // A clip that could not be read is remembered as empty so it is not looked for
  // every frame.
  std::unordered_map<std::string, std::optional<mesh::BoneAnimation>> clips_;
  std::map<std::uint16_t, DrawnSoldier> drawn_;
  std::map<std::uint16_t, DrawStat> stats_;
  std::uint16_t watched_ = 0;
};

}  // namespace obf2::app
