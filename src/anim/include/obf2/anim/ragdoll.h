#pragma once
// The ragdoll: a dead soldier's body — `dice::anim::RagDoll` and
// `RagDollTemplate` (Animation/BoneAnimation/RagDoll.cpp). Every rule here is in
// docs/functions/ragdoll.md with the address it was read at; the comments name
// the function.
//
// What is not here, and says so in the notes: collision with objects and the
// capsules against them (`checkCollisionConstraints`'s first and last parts,
// `checkCapsuleConstraints`), forces (`addForce`, `addHitImpact`), water, and the
// time budget `updateInstances` spreads the bodies over.
#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/con/interpreter.h"
#include "obf2/core/math.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/skeleton.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::anim {

// The tuning variables and their defaults (`Vars::getInt`/`getFloat` in
// RagDoll.cpp's static initialiser, Linux 0x6bad3c..0x6bafc6; the client's
// registration at `BF2.exe` 0x86db40 agrees). Those the port does not use are
// kept too, marked.
struct RagdollSettings {
  float maxInheritSpeed = 40.0f;         // ragdoll-max-inherit-speed
  int netDebug = 0;                      // ragdoll_net_debug — not used
  int netBoneEnable = 15;                // ragdoll_net_boneEnable
  float netInterpolate = 0.1f;           // ragdoll_net_interpolate
  float gravityOverTime = 0.5f;          // ragdoll_gravityOverTime
  float dT = 1.0f / 90.0f;               // ragdoll_dT (0x3c360b61)
  float slowMotion = 1.0f;               // ragdoll_slowMotion
  int enableForce = 1;                   // ragdoll_enableForce — not used (no forces)
  int iterations = 2;                    // ragdoll_iterations
  float clientMaxUpdateTime = 0.002f;    // ragdoll_clientMaxUpdateTime — not used (time budget)
  float sleep = 2.5f;                    // ragdoll_sleep
  float sleepDelta = 0.02f;              // ragdoll_sleepDelta
  int useObjectCollision = 1;            // ragdoll_useObjectCollision — not ported
  int useCapsuleCollision = 1;           // ragdoll_useCapsuleCollision — not ported
  float completeCollRadius = 1.5f;       // ragdoll_completeCollRadius — not used
  int useLandCollision = 1;              // ragdoll_useLandCollision
  int useConstraints = 1;                // ragdoll_useConstraints
  int useAngularConstraints = 1;         // ragdoll_useAngularConstraints
  float expRotateForce = 13.0f;          // ragdoll_expRotateForce — not used
  float boneSize = 0.1f;                 // ragdoll_boneSize
  float kneeBoneSize = 0.05f;            // ragdoll_kneeBoneSize
  float bodyBoneSize = 0.12f;            // ragdoll_bodyBoneSize
  float collMid = 0.9999f;               // ragdoll_collMid — not used
  float legBackAngle = -0.1f;            // ragdoll_legBackAngle
  float legForwardAngle = 10.0f;         // ragdoll_legForwardAngle
  float legForwardFactor = 0.0f;         // ragdoll_legForwardFactor
  float legPointForwardAngle = 0.5f;     // ragdoll_legPointForwardAngle
  float legPointForwardFactor = 0.01f;   // ragdoll_legPointForwardFactor
};

// The template the data builds: `ragDollInit.con` and the
// `ragDollConstraints.inc` it includes (objects/soldiers/common/animations/).
struct RagdollTemplate {
  struct Particle {
    int bone = -1;
    float mass = 1.0f;
    float size = 0.1f;
  };
  struct Distance {          // `addConstraint` → addDistanceConstraint (0x6bbf90)
    int a = 0, b = 0;
    float rest = 0.0f;
  };
  struct Angular {           // addAngularConstraint (0x6bc2b0)
    int a = 0, b = 0, c = 0;
    float ab = 0.0f, bc = 0.0f, target = 0.0f;  // target signed: < 0 is a maximum
  };
  struct Dihedral {          // addDihedralAngleConstraint (0x6bc080)
    int a = 0, b = 0, c = 0, d = 0;
    float ab = 0.0f, bc = 0.0f, cd = 0.0f, target = 0.0f;
  };
  struct DistanceLessThan {  // addDistanceLessThanConstraint (0x6bb020)
    int a = 0, b = 0;
    float length = 0.0f;
  };
  struct Capsule {           // addCapsuleCollision (0x6bb210) — kept, not used
    int a = 0, b = 0;
  };
  struct BoneMapping {       // toSkeleton (0x6bb1c0), as given
    int bone = 0, a = 0, b = 0;
  };

  float gravity = -30.0f;  // +0x4 (`RagDollTemplate::RagDollTemplate`, 0x6bd2a0)
  // setParticleCollisionCriteria (0x6ba5c0): +0x118, +0x11c — kept, not used.
  float collisionMinSpeed = -1.0f;
  float collisionDelay = 0.0f;

  mesh::Skeleton skeleton;           // `ragDoll.skeleton`
  std::vector<mesh::Mat4> restWorld; // its bones in model space (`Skeleton::transform`)
  std::vector<Particle> particles;
  std::array<int, 128> particleOfBone{};  // +0x158, -1 where none
  std::vector<Distance> distances;
  std::vector<Angular> angulars;
  std::vector<Dihedral> dihedrals;
  std::vector<DistanceLessThan> lessThans;
  std::vector<Capsule> capsules;
  std::vector<BoneMapping> mappings;
  std::bitset<128> locked;           // lockBone

  RagdollTemplate() { particleOfBone.fill(-1); }

  int particleOf(int bone) const {
    return bone >= 0 && bone < 128 ? particleOfBone[static_cast<std::size_t>(bone)] : -1;
  }
  // `getBoneDistance` (0x6bbef0): the bones' distance in the rest pose.
  float boneDistance(int a, int b) const;

  // One `ragDoll.*` command; false when it is not one the template knows.
  // `loadSkeleton` reads the file `ragDoll.skeleton` names.
  bool apply(const con::Command& command,
             const std::function<std::optional<mesh::Skeleton>(std::string_view)>& loadSkeleton,
             const RagdollSettings& settings = {});

  // The soldiers' template as the game's data builds it.
  static std::optional<RagdollTemplate> load(FileSystem& files,
                                             const RagdollSettings& settings = {},
                                             std::string* error = nullptr);
};

// What the ground answers. `cast` is the terrain's segment test
// (`HeightmapCluster::intersectRayInWorldCoords`, vtable +0x120): the segment's
// end's signed distance from the plane it crossed, in **grid units** as the
// engine leaves it, and that plane's normal; nullopt when it crosses nothing
// (or water). `height` is `getHeightInWorldCoords` (+0x138).
struct RagdollGround {
  struct Hit {
    float t = 0.0f;
    Vec3f normal{0.0f, 1.0f, 0.0f};
  };
  std::function<std::optional<Hit>(const Vec3f& start, const Vec3f& end)> cast;
  std::function<float(const Vec3f&)> height;
};

class Ragdoll {
 public:
  struct Particle {        // 0x58 bytes in the engine (docs/functions/ragdoll.md)
    Vec3f position;        // +0x00
    Vec3f previous;        // +0x0c
    Vec3f acceleration;    // +0x18
    Vec3f lastStep;        // +0x24
    Vec3f network;         // +0x30
    Vec3f force;           // +0x3c
    int bone = -1;         // +0x48
    float mass = 1.0f;     // +0x4c
    float size = 0.1f;     // +0x50
    bool networked = false;  // +0x54
    bool hit = false;        // +0x55
  };

  // `RagDollTemplate::makeInstance` (0x6bf380): the velocity capped at
  // `maxInheritSpeed`, then `RagDoll::reset` (0x6bef00) — every particle at its
  // bone's world position, its previous one 0.015 × the velocity behind.
  // `boneWorld` is the skeleton posed in the world, one matrix per bone.
  Ragdoll(const RagdollTemplate& tmpl, const std::vector<mesh::Mat4>& boneWorld, Vec3f velocity,
          const RagdollSettings& settings = {});

  // `setIsClient` (0x6c1540).
  void setClient(bool client);
  // `readCurrentState` (`BF2.exe` 0x7ea290): the networked particles' positions,
  // in their order. The first read puts them there at once.
  void readNetwork(const std::vector<Vec3f>& positions);
  // `update` (0x6c1850); returns the centre. `lod` is +0x6c, which
  // `updateInstances` sets by distance (0 for the nearest three awake bodies).
  Vec3f update(float frameTime, const RagdollGround& ground, int lod = 0);
  // `applyOnSkeleton` (0x6c1d90): `world` holds the skeleton's matrices (model
  // space, as they stand) and comes back posed **relative to the centre**.
  void applyOnSkeleton(std::vector<mesh::Mat4>& world) const;

  Vec3f center() const;
  bool client() const { return client_; }
  bool awake() const { return awake_; }
  const std::vector<Particle>& particles() const { return particles_; }

 private:
  void accumulateForces();
  void verlet(float dt);
  void satisfyConstraints(const RagdollGround& ground);
  void checkDistance();
  void checkDistanceLessThan();
  void checkAngular();
  void checkDihedral();
  void checkLegAngular();
  void checkCollision(const RagdollGround& ground);
  bool pinned(const Particle& p) const { return client_ && p.networked; }
  void wakeUp();
  Vec3f forward() const;
  Vec3f right() const;

  const RagdollTemplate* tmpl_;
  RagdollSettings settings_;
  std::vector<Particle> particles_;
  bool awake_ = true;          // +0x31
  Vec3f lastCenter_;           // +0x34
  float sleepTimer_ = 0.0f;    // +0x4c
  float accumulated_ = 0.0f;   // +0x60
  float time_ = 0.0f;          // +0x5c — purpose not established, kept
  float gravityRamp_ = 0.0f;   // +0x64 (rd_gravityOverTime)
  float gravityTime_ = 0.0f;   // +0x68
  float clientTimer_ = 0.0f;   // +0x70
  bool frozen_ = false;        // +0x74 — nobody sets it in what is read
  bool client_ = false;        // +0x75
  bool fresh_ = true;          // +0x76: no network state yet
  bool landed_ = false;        // +0x77: something was hit
};

}  // namespace obf2::anim
