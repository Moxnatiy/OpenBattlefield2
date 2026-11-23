#include "obf2/app/world_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "obf2/core/path.h"
#include "obf2/game/object_mesh.h"
#include "obf2/game/scene.h"
#include "obf2/game/soldier_model.h"
#include "obf2/level/gameplay.h"
#include "obf2/mesh/primitives.h"
#include "obf2/server/soldier_move.h"

namespace obf2::app {

namespace {

// A RotationalBundle input (`setInputTo*`) read from one of our actions.
float rotationInputAxis(game::RotationInput input, const net::bf2::PlayerAction& action) {
  using I = game::RotationInput;
  int index = -1;
  switch (input) {
    case I::MouseX: index = net::bf2::kAxisMouseX; break;
    case I::MouseY: index = net::bf2::kAxisMouseY; break;
    case I::Throttle: index = net::bf2::kAxisThrottle; break;
    case I::Yaw: index = net::bf2::kAxisYaw; break;
    case I::Pitch: index = net::bf2::kAxisPitch; break;
    case I::Roll: index = net::bf2::kAxisRoll; break;
    case I::None: break;
  }
  return index < 0 ? 0.0f : net::bf2::axisFromWire(action.axes[index]);
}

}  // namespace

bool WorldView::init(FileSystem& files, const game::Registry& registry,
                     gfx::MeshRenderer& renderer, TextureResolver resolve,
                     session::RemoteWorld& remote, const level::Level* level,
                     const Options& options) {
  files_ = &files;
  registry_ = &registry;
  renderer_ = &renderer;
  resolve_ = std::move(resolve);
  remote_ = &remote;
  options_ = options;

  // What the server's created objects are, by where they stand
  // (obf2/level/placement_index.h). A spawner's object is drawn as the vehicle it
  // issues; a static placement is skipped, because the level draws it already.
  if (level != nullptr) {
    const std::string mode = remote.serverGameMode.empty() ? "gpm_cq" : remote.serverGameMode;
    const int size = remote.serverSize > 0 ? remote.serverSize : 16;
    if (const auto gameplay = level::loadGameplayObjects(files, level->name, mode, size)) {
      placement_.emplace(*gameplay, *level);
      std::printf("  server objects are matched against %s/%d: %zu spawners\n", mode.c_str(), size,
                  gameplay->spawners.size());
    }
  }

  // Three placeholders: another player's soldier, our own and everything else.
  // The colours here are **ours**, a marker for as long as we cannot take the real
  // geometry. The size is not: it is the soldier's collision shape from the game's
  // data (`coll-soldier-radius` 0.25 and `coll-soldier-stand-height` 1.7).
  const server::PhysicsConstants& shape = remote.physics;
  const mesh::Vec3 size{shape.radius * 2.0f, shape.standHeight, shape.radius * 2.0f};
  const char* colors[3] = {"#909090", "#3060c0", "#c03030"};  // nobody, ours, theirs
  boxesReady_ = true;
  for (int i = 0; i < 3; ++i) {
    auto uploaded = renderer.upload(mesh::buildBox(size, colors[i]), resolve_);
    if (!uploaded) {
      boxesReady_ = false;
      break;
    }
    boxes_[i] = *uploaded;
  }
  return boxesReady_;
}

const mesh::BoneAnimation* WorldView::clipAt(const std::string& path) {
  const std::string key = normalizeAssetPath(path);
  auto found = clips_.find(key);
  if (found == clips_.end()) {
    std::optional<mesh::BoneAnimation> clip;
    if (const auto bytes = files_->read(key)) clip = mesh::loadBoneAnimation(*bytes);
    found = clips_.emplace(key, std::move(clip)).first;
  }
  return found->second ? &*found->second : nullptr;
}

WorldView::Look* WorldView::lookFor(const std::string& soldierName, const std::string& kitName,
                                    int weaponItem, bool stowed) {
  const std::string key = lookKey(soldierName, kitName, weaponItem, stowed);
  if (const auto found = looks_.find(key); found != looks_.end()) {
    return found->second.ready ? &found->second : nullptr;
  }
  Look look;
  const auto model = game::soldierModel(*registry_, soldierName, kitName, weaponItem);
  const auto loadPart = [&](const game::SoldierPart& part) {
    const std::string path = game::resolveGeometryPath(*files_, part.templateFile, part.geometryName);
    return path.empty() ? std::nullopt
                        : game::loadMesh(*files_, path, part.geometry, 0, false);
  };
  std::optional<mesh::RenderMesh> bind = model ? loadPart(model->body) : std::nullopt;
  if (bind && model->kit) {
    if (const auto kit = loadPart(*model->kit)) mesh::appendSkinned(*bind, *kit);
  }
  // The weapon in his hands. It is a BundledMesh, not a skinned one: its parts are
  // carried by the skeleton's bones 64 and up, one each, and the weapon's own
  // third-person clips move exactly those (`obf2::mesh::bindPartsToBones`). Bound
  // that way it is the same mesh and the same shader as the body. In an open
  // seat he is drawn holding none; what `Soldier::onEnter` (Linux 0x54b2f0) does
  // with the item he had out is not read.
  if (bind && model->weapon && !stowed) {
    if (auto weapon = loadPart(*model->weapon)) {
      mesh::bindPartsToBones(*weapon);
      mesh::appendSkinned(*bind, *weapon);
    }
  }
  std::optional<mesh::Skeleton> skeleton;
  if (bind && !model->skeleton3p.empty()) {
    if (const auto bytes = files_->read(normalizeAssetPath(model->skeleton3p))) {
      skeleton = mesh::loadSkeleton(*bytes);
    }
  }
  if (bind && skeleton) {
    look.skeleton = std::move(*skeleton);
    look.legs = animationSystem(model->animationSystem3p);
    look.weapon = animationSystem(model->weaponAnimationSystem3p);
    if (const auto uploaded = renderer_->upload(*bind, resolve_)) look.gpu = *uploaded;
    look.ready = look.gpu.vertices != nullptr;
    std::printf("  soldier look %s: %zu vertices, kit %s, legs %s, weapon %s, on the gpu %s\n",
                key.c_str(), bind->vertices.size(), model->kit ? "yes" : "no",
                look.legs ? "yes" : "no", look.weapon ? "yes" : "no",
                look.gpu.skin != nullptr ? "skinned" : "unskinned");
  } else {
    std::printf("  soldier look %s: not assembled (model %d, mesh %d, skeleton %d)\n", key.c_str(),
                model ? 1 : 0, bind ? 1 : 0, skeleton ? 1 : 0);
  }
  auto& stored = looks_.emplace(key, std::move(look)).first->second;
  return stored.ready ? &stored : nullptr;
}

std::optional<Mat4> WorldView::poseBody(std::uint16_t id, const net::bf2::RemoteObject& object,
                                        DrawnSoldier& drawn, const Mat4& place,
                                        float frameStep) {
  if (object.ragdollParticles.empty() || object.ragdollExact == 0) {
    bodies_.erase(id);
    return std::nullopt;
  }
  if (!ragdollTried_) {
    ragdollTried_ = true;
    std::string error;
    ragdollTemplate_ = anim::RagdollTemplate::load(*files_, {}, &error);
    std::printf("  ragdoll template: %s\n", ragdollTemplate_ ? "read" : error.c_str());
  }
  if (!ragdollTemplate_ || drawn.look == nullptr) return std::nullopt;

  auto found = bodies_.find(id);
  if (found == bodies_.end()) {
    // The pose he was drawn in, in the world: the engine's
    // `makeInstance(objectMatrix, velocity, skeleton)` after `Skeleton::transform`.
    std::vector<mesh::Mat4> world =
        drawn.pose.empty() ? mesh::poseSkeleton(drawn.look->skeleton, nullptr, 0) : drawn.pose;
    for (mesh::Mat4& bone : world) {
      Mat4 local;
      std::copy(std::begin(bone.m), std::end(bone.m), std::begin(local.m));
      const Mat4 placed = place * local;
      std::copy(std::begin(placed.m), std::end(placed.m), std::begin(bone.m));
    }
    anim::Ragdoll ragdoll(*ragdollTemplate_, world, object.lastVelocity);
    // `enableRagDoll`: not the server, and a remote object — the client's mode.
    ragdoll.setClient(true);
    found = bodies_.emplace(id, Body(std::move(ragdoll))).first;
    std::printf("  soldier %u lies down: a ragdoll of %zu particles\n", id,
                found->second.ragdoll.particles().size());
  }
  Body& body = found->second;
  if (body.fed != object.ragdollExact) {
    body.ragdoll.readNetwork(object.ragdollParticles);
    body.fed = object.ragdollExact;
    // The watched body, state by state: where the server has its hips (bone 6,
    // the second particle on the wire) against where ours are, both over the
    // terrain.
    if (id == watched_ && remote_->terrain != nullptr && object.ragdollParticles.size() > 1) {
      const Vec3f server = object.ragdollParticles[1];
      const Vec3f ours = body.ragdoll.center();
      std::printf("    body %u state %d at tick %u: the server's hips at %.2f %.2f %.2f, %.2f over "
                  "the terrain, ours %.2f\n",
                  id, body.fed, remote_->world.gameTick(), server.x, server.y, server.z,
                  server.y - remote_->terrain->groundContactAt(server).height,
                  ours.y - remote_->terrain->groundContactAt(ours).height);
    }
  }

  anim::RagdollGround ground;
  if (const level::Level* terrain = remote_->terrain) {
    ground.cast = [terrain](const Vec3f& start,
                            const Vec3f& end) -> std::optional<anim::RagdollGround::Hit> {
      const auto hit = terrain->castSegment(start, end);
      if (!hit) return std::nullopt;
      return anim::RagdollGround::Hit{hit->t, hit->normal};
    };
    ground.height = [terrain](const Vec3f& at) { return terrain->groundContactAt(at).height; };
  }
  // The objects: the session's collision world, the soldiers' lod (2) of every
  // static object and of the vehicles the server spawned. The engine's filter
  // (`RagDollObjectPredicator`, Linux 0x6c3a40: object flag 0x100, physics flag
  // 0x10 clear) is not applied — what those flags mean is not established.
  if (const server::CollisionWorld* collision = remote_->collision) {
    ground.objects = [collision](const Vec3f& start, const Vec3f& motion, const Vec3f& around,
                                 float radius) -> std::optional<anim::RagdollGround::ObjectHit> {
      const auto hit = collision->segmentContact(start, motion, around, radius);
      if (!hit) return std::nullopt;
      return anim::RagdollGround::ObjectHit{hit->normal, hit->along};
    };
  }
  // The level of detail `updateInstances` gives by distance to the camera is not
  // ported: every body steps at the full rate (+0x6c = 0).
  const Vec3f centre = body.ragdoll.update(frameStep, ground);
  ++body.frames;
  if (remote_->terrain != nullptr) {
    const float above = centre.y - remote_->terrain->groundContactAt(centre).height;
    body.highest = std::max(body.highest, above);
    if (above > 0.6f) ++body.framesHigh;
    float surface = remote_->terrain->groundContactAt(centre).height;
    float onObject = 0.0f;
    if (remote_->collision != nullptr &&
        remote_->collision->groundHeight(centre, 20.0f, 0.1f, &onObject)) {
      surface = std::max(surface, onObject);
    }
    const float overSurface = centre.y - surface;
    body.highestOverSurface = std::max(body.highestOverSurface, overSurface);
    if (overSurface > 0.6f) ++body.framesOverSurface;
  }
  body.last = centre;
  {
    const auto& t = *ragdollTemplate_;
    const auto& ps = body.ragdoll.particles();
    const int head = t.particleOf(47), left = t.particleOf(4), right = t.particleOf(9);
    if (head >= 0 && left >= 0 && right >= 0) {
      const float h = ps[static_cast<std::size_t>(head)].position.y;
      if (h - ps[static_cast<std::size_t>(left)].position.y > 0.8f &&
          h - ps[static_cast<std::size_t>(right)].position.y > 0.8f) {
        ++body.framesUpright;
      }
    }
  }
  // The measure for walls: a particle that a face separates from the centre.
  ++bodyFrames_;
  if (const server::CollisionWorld* collision = remote_->collision) {
    for (const auto& p : body.ragdoll.particles()) {
      if (collision->segmentContact(centre, p.position - centre, centre, 1.5f)) {
        ++bodyFramesThroughFace_;
        break;
      }
    }
  }
  if (drawn.pose.empty()) drawn.pose = mesh::poseSkeleton(drawn.look->skeleton, nullptr, 0);
  body.ragdoll.applyOnSkeleton(drawn.pose);
  return translation(centre);
}

Vec3f WorldView::carry(std::uint16_t id, const net::bf2::RemoteObject& object,
                       const net::bf2::GhostPose& pose) {
  Carried& carried = carried_[id];
  const std::uint32_t nowTick = remote_->world.gameTick();
  const float pivot = remote_->physics.pivotHeight;
  const Vec3f feet{pose.position.x, pose.position.y - pivot, pose.position.z};
  const auto* newestUpdate = object.track.newest();
  const Vec3f velocity =
      newestUpdate && newestUpdate->velocity ? *newestUpdate->velocity : Vec3f{};
  if (!carried.ready || nowTick < carried.tick || nowTick - carried.tick > 8) {
    // First sight, or a gap too long to replay: stand him where the network says.
    carried.body = server::BodyState{};
    carried.body.position = feet;
    carried.body.onGround = true;
    carried.from = feet;
    carried.tick = nowTick;
    carried.ready = true;
  }
  while (carried.tick < nowTick) {
    carried.from = carried.body.position;
    server::carryRemoteSoldier(carried.body, carried.swim, feet, velocity, pose.bodyYaw,
                               remote_->physics, remote_->terrain, remote_->collision,
                               server::kTickTime);
    ++carried.tick;
  }
  const float fraction = std::clamp(remote_->tick.pending / server::kTickTime, 0.0f, 1.0f);
  const Vec3f blended = carried.from + (carried.body.position - carried.from) * fraction;
  if (remote_->terrain != nullptr) {
    DrawStat& stat = stats_[id];
    if (feet.y < remote_->terrain->groundHeightAt(feet) - 0.05f) ++stat.predictedUnder;
    if (blended.y < remote_->terrain->groundHeightAt(blended) - 0.05f) ++stat.drawnUnder;
  }
  return Vec3f{blended.x, blended.y + pivot, blended.z};
}

std::vector<WorldView::MapObject> WorldView::mapObjects() const {
  std::vector<MapObject> out;
  if (remote_ == nullptr) return out;
  for (const auto& [id, resolved] : resolved_) {
    if (resolved.placed.kind != level::PlacedAt::Kind::Spawned) continue;
    const auto found = remote_->world.objects().find(id);
    if (found == remote_->world.objects().end()) continue;
    MapObject object;
    object.templateName = resolved.placed.templateName;
    object.x = found->second.position.x;
    object.z = found->second.position.z;
    object.yaw = found->second.yaw.value_or(0.0f);
    out.push_back(std::move(object));
  }
  return out;
}

Mat4 WorldView::objectRotation(std::uint16_t id, const net::bf2::RemoteObject& object) {
  if (object.rotation) {
    // The engine's quaternion is a row-vector one: conjugated for ours.
    const auto& r = *object.rotation;
    return rotationFromQuaternion({-r[0], -r[1], -r[2], r[3]});
  }
  const auto found = resolved_.find(id);
  if (found != resolved_.end() && found->second.placed.hasRotation) {
    const auto& rotation = found->second.placed.rotation;
    return rotationYawPitchRoll(rotation.x, rotation.y, rotation.z);
  }
  return Mat4::identity();
}

const std::vector<game::VehicleSeat>& WorldView::seatsOf(const std::string& vehicle) {
  auto found = seatsByVehicle_.find(vehicle);
  if (found == seatsByVehicle_.end()) {
    found = seatsByVehicle_.emplace(vehicle, game::vehicleSeats(remote_->registry, vehicle)).first;
  }
  return found->second;
}

std::optional<Vec3f> WorldView::displayedTurretAngle(std::uint16_t root, unsigned seat,
                                                     std::size_t bundle) const {
  const auto id = static_cast<std::uint16_t>(root + seat);
  const auto* aim = remote_->world.seatAim(id);
  // The seat we control: the state that answered our actions, with the actions
  // the server has not answered yet stepped on it, as the soldier's prediction
  // does (RotationalBundle::calculateAndClipAngle, Linux 0x585a40).
  if (id == remote_->ourSoldier && aim && aim->controlled && aim->root == root &&
      bundle < aim->angles.size()) {
    const auto vehicle = remote_->world.objects().find(root);
    const auto* name = vehicle == remote_->world.objects().end()
        ? nullptr : remote_->templateNumbers.nameOf(vehicle->second.templateId);
    if (!name) return aim->angles[bundle];
    const auto key = *name + "#" + std::to_string(seat);
    auto [found, inserted] = localTurretSettings_.try_emplace(key);
    if (inserted) {
      for (const auto& part : game::vehicleTurretBundles(*registry_, *name, seat)) {
        const auto* definition = registry_->find(part);
        auto settings = definition ? game::rotationalSettings(*definition) : game::RotationalSettings{};
        if (!definition) settings.supported = false;
        found->second.push_back(settings);
      }
    }
    if (bundle >= found->second.size()) return aim->angles[bundle];
    const auto& settings = found->second[bundle];
    game::RotationalState state{aim->angles[bundle], {}, {}};
    if (bundle < aim->normalizedRates.size()) {
      const auto rate = aim->normalizedRates[bundle];
      // RotationalBundleNetworkable::setNetUpdate, Linux 0x5d5440;
      // BF2.exe 0x6322e0: normalized rate * abs(authored maxSpeed) * 16.
      state.velocity = {rate.x * std::abs(settings.maxSpeed.x) * 16,
                        rate.y * std::abs(settings.maxSpeed.y) * 16,
                        rate.z * std::abs(settings.maxSpeed.z) * 16};
    }
    for (const auto& [counter, action] : remote_->sent) {
      (void)counter;
      game::stepRotationalBundle(state, settings,
                                 {rotationInputAxis(settings.inputs[0], action),
                                  rotationInputAxis(settings.inputs[1], action),
                                  rotationInputAxis(settings.inputs[2], action)},
                                 server::kTickTime);
    }
    return state.angle;
  }
  // Anyone else's: the angle the seat's networkable last set.
  return aim && aim->root == root && bundle < aim->angles.size()
      ? std::optional<Vec3f>(aim->angles[bundle]) : std::nullopt;
}

std::optional<WorldView::VehicleView> WorldView::occupiedCamera(float lookX, float lookY,
                                                                SDL_Scancode cameraKey) {
  if (!remote_) return std::nullopt;
  const auto pose = remote_->occupiedVehiclePose();
  if (!pose) {
    cameraSeat_ = 0;
    return std::nullopt;
  }
  const auto seatId = remote_->ourSoldier;
  const auto root = remote_->vehicleRootOf(seatId);
  const auto vehicle = remote_->world.objects().find(root);
  if (vehicle == remote_->world.objects().end()) return std::nullopt;
  if (seatId != cameraSeat_ || cameraRound_ != remote_->roundResets) {
    cameraSeat_ = seatId;
    cameraRound_ = remote_->roundResets;
    cameraLookYaw_ = cameraLookPitch_ = 0.0f;
    cameraViewMode_ = 1;
    chaseDirection_.reset();
  }
  // A seat with no camera of its own looks from 1.2 m over the hull's origin,
  // or 0.65 m over its seat: ours, **not measured**.
  game::VehicleSeatCamera fallback;
  fallback.transform = translation({0, 1.2f, 0});
  const auto* mount = &fallback;
  cockpitGeometry_.reset();
  if (const auto* name = remote_->templateNumbers.nameOf(vehicle->second.templateId)) {
    const auto& seats = seatsOf(*name);
    const auto index = static_cast<std::size_t>(seatId - root);
    if (index < seats.size()) {
      const auto& seat = seats[index];
      if (seat.camera) mount = &*seat.camera;
      else if (seat.hasSeatInformation) fallback.transform = seat.transform * translation({0, 0.65f, 0});
      cockpitGeometry_ = seat.cockpitGeometry;
    }
  }
  // View modes as Camera::setViewMode (Linux 0x579bd0) allows them: Inside by
  // the template; Chase, FrontChase and FlyBy by the template and the
  // server's sv.allowExternalViews (serverInfo +0x2e).
  const auto allowed = [&](int mode) {
    switch (mode) {
      case 1: return mount->allowInside;
      case 3: return mount->allowChase && remote_->allowExternalViews;
      case 4: return mount->allowFrontChase && remote_->allowExternalViews;
      case 5: return mount->allowFlyBy && remote_->allowExternalViews;
      default: return false;
    }
  };
  const int previousMode = cameraViewMode_;
  // F9 and F10 are c_PICameraMode1 and 2; which mode each sets is not read in the
  // binary: ours takes them for Inside and Chase.
  if (cameraKey == SDL_SCANCODE_F9) {
    if (allowed(1)) cameraViewMode_ = 1;
  } else if (cameraKey == SDL_SCANCODE_F10) {
    if (allowed(3)) cameraViewMode_ = 3;
  } else if (cameraKey == SDL_SCANCODE_C) {
    // C (c_PIToggleCameraMode) steps to the next allowed mode, ascending:
    // Inside -> Chase -> FrontChase -> FlyBy -> Inside, as measured on Retail in
    // the M1A2's driver seat.
    for (int step = 1; step <= 7; ++step) {
      const int next = (cameraViewMode_ - 1 + step) % 7 + 1;
      if (allowed(next)) {
        cameraViewMode_ = next;
        break;
      }
    }
  }
  if (!allowed(cameraViewMode_)) cameraViewMode_ = 1;
  externalCamera_ = cameraViewMode_ != 1;
  if (previousMode != cameraViewMode_) {
    cameraLookYaw_ = cameraLookPitch_ = 0.0f;
    chaseDirection_.reset();
    flyByEye_.reset();  // setViewMode(5) clears the recorded frames
  }
  auto rotation = objectRotation(root, vehicle->second);
  if (pose->rotation) {
    const auto& q = *pose->rotation;
    rotation = rotationFromQuaternion({-q[0], -q[1], -q[2], q[3]});
  }
  auto joints = Mat4::identity();
  bool followsYaw = false, followsPitch = false;
  for (const auto& joint : mount->joints) {
    if (const auto angle = displayedTurretAngle(root, joint.seat, joint.bundle)) {
      joints = game::composeTurretRotation(joints, joint.rest, joint.parentRest, *angle,
                                           joint.stabilizeYaw);
      followsYaw |= joint.aimYaw;
      followsPitch |= joint.aimPitch;
    }
  }
  // A camera on an aimed joint turns with it; otherwise the look turns the
  // camera on its mount. A pilot's mouse flies the aircraft, so his camera stays
  // on the hull; which key frees a pilot's look is not read. The look's limits
  // (−50°..45°) are **not measured**.
  const bool piloting = remote_->pilotControls() != session::RemoteWorld::PilotControls::None;
  if (followsYaw || piloting) {
    cameraLookYaw_ = 0;
  } else {
    cameraLookYaw_ = std::remainder(cameraLookYaw_ + lookX * remote_->physics.lookFactorX, 360.0f);
  }
  if (followsPitch || piloting) {
    cameraLookPitch_ = 0;
  } else {
    cameraLookPitch_ = std::clamp(cameraLookPitch_ - lookY * remote_->physics.lookFactorY, -50.0f, 45.0f);
  }
  const auto placement = translation(pose->position) * rotation * joints * mount->transform;
  const auto orbit = placement * rotationYawPitchRoll(cameraLookYaw_, -cameraLookPitch_, 0);
  const auto anchor = transformPoint(placement, {});
  const auto forward = normalize(transformDirection(orbit, {0, 0, 1}));
  constexpr float degrees = 180.0f / std::numbers::pi_v<float>;
  if (!externalCamera_) {
    return VehicleView{{anchor, anchor + forward, transformDirection(orbit, {0, 1, 0})},
                       std::atan2(forward.x, forward.z) * degrees,
                       std::atan2(forward.y, std::hypot(forward.x, forward.z)) * degrees,
                       mount->insideFieldOfView};
  }
  if (cameraViewMode_ == 5) {
    // FlyBy: Camera::getTransformationCVMFlyBy (Linux 0x579980) plants a
    // point ahead of the vehicle, (forward * radius * 1.2 + velocity * 0.3)
    // * (+0x2d0 * 0.4) from the camera, at least 1 m above the ground, and
    // records it (writeFrame 0x5779e0). +0x2d0 is 8.0 (Camera::Camera,
    // 0x57a13f), so a new point is taken only after 8 * 60 = 480 s: in
    // practice the point stays until the mode is entered again. viewFrame
    // (0x5796a0) looks from the point at the camera, up (0, 1, 0).
    const auto cameraAt = anchor;
    if (!flyByEye_) {
      // The root's bounding radius (vtable +0x130) is not read: the drawn
      // hull mesh's bounding sphere stands in (not measured against Retail).
      const auto* model = vehicleMesh(root, vehicle->second);
      const float radius = model && model->boundsRadius > 0.0f ? model->boundsRadius : 2.0f;
      Vec3f velocity{};
      if (const auto motion = vehicleMotion_.find(root); motion != vehicleMotion_.end()) {
        velocity = motion->second.forward * motion->second.speed;
      }
      constexpr float flyByField = 8.0f;  // Camera +0x2d0, Camera::Camera 0x57a13f
      const auto hullForward = transformDirection(translation(pose->position) * rotation, {0, 0, 1});
      Vec3f point = cameraAt + (hullForward * (radius * 1.2f) + velocity * 0.3f) * (flyByField * 0.4f);
      if (remote_->terrain) point.y = std::max(point.y, remote_->terrain->groundHeightAt(point) + 1.0f);
      flyByEye_ = point;
    }
    const auto eye = *flyByEye_;
    const auto direction = normalize(cameraAt - eye);
    return VehicleView{{eye, cameraAt, {0, 1, 0}}, std::atan2(direction.x, direction.z) * degrees,
                       std::atan2(direction.y, std::hypot(direction.x, direction.z)) * degrees,
                       mount->fieldOfView};
  }
  // Chase and FrontChase: Camera::getTransformationCVMChase (Linux 0x57ab80),
  // the second argument set for FrontChase. Everything is in the hull's
  // frame (the root parent), not the turret's or the camera's, and the look
  // input does not move it.
  const auto hull = translation(pose->position) * rotation;
  // The target: chaseOffset as a point in the hull's frame (template +0x2bc).
  const auto target = transformPoint(hull, mount->chaseOffset);
  // chaseDistance -1: the bounding radius (at least 2) times g_cam_dist. Our
  // seats carry no radius and g_cam_dist is not measured: 2 m stands in.
  const float distance = mount->chaseDistance == -1.0f ? 2.0f : mount->chaseDistance;
  // Away from the target: -forward (+forward for FrontChase) plus
  // chaseAngle * up, normalised.
  const float sign = cameraViewMode_ == 4 ? -1.0f : 1.0f;
  const auto hullForward = transformDirection(hull, {0, 0, 1});
  const auto hullUp = transformDirection(hull, {0, 1, 0});
  auto wanted = normalize(hullForward * -sign + hullUp * mount->chaseAngle);
  // The stored direction follows at followStiffness per second: a slerp of
  // (direction, 1) by clamp(dt * followStiffness, 0, 1), then normalised.
  const auto now = std::chrono::steady_clock::now();
  const float dt = std::clamp(std::chrono::duration<float>(now - chaseUpdated_).count(), 0.0f, 0.1f);
  chaseUpdated_ = now;
  if (chaseDirection_) {
    const float t = std::clamp(dt * mount->followStiffness, 0.0f, 1.0f);
    const auto& a = *chaseDirection_;
    // The 4D dot of (a, 1) and (wanted, 1), both of length sqrt 2.
    const float cosine = std::clamp((dot(a, wanted) + 1.0f) / 2.0f, -1.0f, 1.0f);
    const float angle = std::acos(cosine);
    float wa = 1.0f - t, wb = t;
    if (angle > 1e-4f) {
      wa = std::sin((1.0f - t) * angle) / std::sin(angle);
      wb = std::sin(t * angle) / std::sin(angle);
    }
    const auto blended = a * wa + wanted * wb;
    if (length(blended) > 1e-6f) wanted = normalize(blended);
  }
  chaseDirection_ = wanted;
  // The distance's speed term (the vehicle's velocity along its forward,
  // times g_acc) is not ported: g_acc is not measured.
  Vec3f eye = target + wanted * distance;
  // At least 1 m above the ground under the eye. The original also keeps it
  // 1 m above the water level (heightmapCluster +0xd8); water is not read here.
  if (remote_->terrain) eye.y = std::max(eye.y, remote_->terrain->groundHeightAt(eye) + 1.0f);
  const auto direction = normalize(target - eye);
  return VehicleView{{eye, target, {0, 1, 0}}, std::atan2(direction.x, direction.z) * degrees,
                     std::atan2(direction.y, std::hypot(direction.x, direction.z)) * degrees,
                     mount->fieldOfView};
}

const anim::System* WorldView::animationSystem(const std::string& path) {
  if (path.empty()) return nullptr;
  const auto key = normalizeAssetPath(path);
  auto found = animationSystems_.find(key);
  if (found == animationSystems_.end()) {
    found = animationSystems_.emplace(key, anim::System::load(*files_, key)).first;
    if (!found->second) std::printf("  animation system %s: did not load\n", path.c_str());
  }
  return found->second ? &*found->second : nullptr;
}

std::optional<Vec3f> WorldView::seatedAt(std::uint16_t soldier) {
  const auto* owner = remote_->world.infantryOwnerOf(soldier);
  const auto occupied = remote_->playerSpawned && soldier == remote_->infantryObject
      ? remote_->ourSoldier : owner ? owner->object : 0;
  if (!occupied || occupied == soldier) return std::nullopt;
  const auto seat = seatOf(occupied);
  if (!seat || !seat->seat->hasSeatInformation) return std::nullopt;
  return transformPoint(seat->place, Vec3f{0.0f, 0.0f, 0.0f});
}

std::optional<WorldView::SeatAt> WorldView::seatOf(std::uint16_t seatId, bool drawing) {
  const auto vehicleId = remote_->vehicleRootOf(seatId);
  const auto& objects = remote_->world.objects();
  const auto vehicle = objects.find(vehicleId);
  if (vehicleId == 0 || vehicle == objects.end() || seatId < vehicleId) return std::nullopt;
  const auto* name = remote_->templateNumbers.nameOf(vehicle->second.templateId);
  if (name == nullptr) return std::nullopt;
  const auto& seats = seatsOf(*name);
  const auto index = static_cast<std::size_t>(seatId - vehicleId);
  if (index >= seats.size()) return std::nullopt;
  // seatInformation places the Soldier object at its physics pivot. The
  // skinned mesh starts at the feet, so the pivot height comes off along the
  // seat's up (`coll-soldier-pivot-height`, as for a soldier on foot).
  const auto modelSeat = seats[index].transform * translation({0, -remote_->physics.pivotHeight, 0});
  auto seatRotation = Mat4::identity();
  for (const auto& joint : seats[index].joints) {
    if (const auto angle = displayedTurretAngle(vehicleId, joint.seat, joint.bundle)) {
      seatRotation = game::composeTurretRotation(seatRotation, joint.rest, joint.parentRest, *angle,
                                                 joint.stabilizeYaw);
    }
  }
  const auto mountedSeat = seatRotation * modelSeat;
  if (drawing) {
    const auto hull = vehicleDrawTransforms_.find(vehicleId);
    if (hull == vehicleDrawTransforms_.end()) return std::nullopt;
    return SeatAt{vehicleId, &seats[index], hull->second * mountedSeat};
  }
  const auto local = seatId == remote_->ourSoldier ? remote_->occupiedVehiclePose() : std::nullopt;
  const auto pose = local ? local
      : remote_->world.poseOf(vehicleId, remote_->tick.pending / server::kTickTime);
  const Vec3f position = pose ? pose->position : vehicle->second.position;
  Mat4 rotation = objectRotation(vehicleId, vehicle->second);
  if (pose && pose->rotation) {
    const auto& q = *pose->rotation;
    rotation = rotationFromQuaternion({-q[0], -q[1], -q[2], q[3]});
  }
  return SeatAt{vehicleId, &seats[index], translation(position) * rotation * mountedSeat};
}

Mat4 WorldView::steeringTransform(const SteeringJoint& joint, float steering, std::uint16_t vehicle) const {
  // Our own vehicle: the joint's bundle stepped from our key input, as the
  // engine steps it (Linux 0x585a40; drawVehicle).
  if (remote_->world.ownObject() == vehicle && joint.settings.supported) {
    return game::turretRotationAroundRest(joint.rest, joint.own.angle);
  }
  // The bundle's own networked rotation (RotationalBundleNetworkable, seat 0
  // of the vehicle's descriptor) is what Retail turns the part by.
  if (joint.bundle && remote_->world.ownObject() != vehicle) {
    if (const auto angle = displayedTurretAngle(vehicle, 0, *joint.bundle)) {
      return game::turretRotationAroundRest(joint.rest, *angle);
    }
  }
  return steeringTransform(joint, steering);
}

Mat4 WorldView::steeringTransform(const SteeringJoint& joint, float steering) {
  const float input = joint.reversed ? -steering : steering;
  const auto angles = input < 0 ? joint.minRotation * -input : joint.maxRotation * input;
  return game::turretRotationAroundRest(joint.rest, angles);
}

const gfx::GpuMesh* WorldView::uploadVehicleParts(mesh::RenderMesh mesh, const std::string& name,
                                                  bool cockpit) {
  const auto upload = [&](const mesh::RenderMesh& part) -> const gfx::GpuMesh* {
    auto gpu = renderer_->upload(part, resolve_);
    if (!gpu) return nullptr;
    vehicleMeshes_.push_back(std::move(*gpu));
    return &vehicleMeshes_.back();
  };
  const auto instance = game::flattenObject(*registry_, name);
  if (!instance) return upload(mesh);

  VehicleParts visuals;
  const auto* vehicle = registry_->find(name);
  // Wheels touch the ground on land vehicles only: not aircraft, not the sea
  // category (`vehicleCategory VCSea`).
  visuals.groundSuspension = vehicle &&
      session::RemoteWorld::aircraftControls(vehicle) == session::RemoteWorld::PilotControls::None &&
      normalizeAssetPath(vehicle->text("vehicleCategory")) != "vcsea";
  struct MovingPart {
    int geometry;
    Vec3f pivot;
    Vec3f axle;
    std::vector<TurretJoint> joints;
    std::optional<SteeringJoint> steering;
    bool spring = false;
  };
  std::vector<MovingPart> parts;
  std::vector<std::pair<const game::PartPlacement*, std::vector<TurretJoint>>> guns;
  std::vector<std::pair<int, TurretJoint>> ancestry;
  std::vector<std::pair<int, SteeringJoint>> steeringAncestry;
  std::vector<Mat4> restPath;
  // Each bundle with the objects that share its networkable (BF2.exe
  // 0x6aaa10): FAAV nav_LF turns with nav_RF, M1A2 Barrel_Base with Hud_Spring.
  std::array<std::vector<std::vector<std::string>>, 8> turretBundles;
  for (unsigned seat = 0; seat < turretBundles.size(); ++seat) {
    turretBundles[seat] = game::vehicleTurretBundleObjects(*registry_, name, seat);
  }
  const auto drives = [](const std::string& key) {
    return [&key](const std::vector<std::string>& objects) {
      return std::ranges::any_of(objects, [&](const std::string& object) {
        return normalizeAssetPath(object) == key;
      });
    };
  };
  for (const auto& part : instance->parts) {
    restPath.resize(static_cast<std::size_t>(part.depth));
    const auto parentRest = restPath.empty() ? Mat4::identity() : restPath.back();
    restPath.push_back(part.transform);
    const auto key = normalizeAssetPath(part.templateName);
    while (!ancestry.empty() && ancestry.back().first >= part.depth) ancestry.pop_back();
    while (!steeringAncestry.empty() && steeringAncestry.back().first >= part.depth) {
      steeringAncestry.pop_back();
    }
    // A steered joint: a RotationalBundle whose yaw or roll input is the
    // driver's yaw axis (`setInputToYaw PIYaw`, JEEP_FAAV.tweak's nav bundles).
    bool steeringJoint = false;
    if (visuals.groundSuspension && part.className == "RotationalBundle") {
      const auto* definition = registry_->find(part.templateName);
      for (const auto axis : {"setInputToYaw", "setInputToRoll"}) {
        if (!definition || normalizeAssetPath(definition->text(axis)) != "piyaw") continue;
        const auto vector = [&](const char* property) -> Vec3f {
          const auto* value = definition->property(property);
          const auto data = value ? value->asVec3() : std::nullopt;
          return data ? Vec3f{data->x, data->y, data->z} : Vec3f{};
        };
        const auto speed = vector("setMaxSpeed");
        std::optional<std::size_t> bundle;
        const auto found = std::ranges::find_if(turretBundles[0], drives(key));
        if (found != turretBundles[0].end()) {
          bundle = static_cast<std::size_t>(found - turretBundles[0].begin());
        }
        steeringAncestry.emplace_back(part.depth, SteeringJoint{part.transform,
            vector("setMinRotation"), vector("setMaxRotation"), speed.x < 0 || speed.z < 0, bundle,
            game::rotationalSettings(*definition), {}, 0.0f});
        steeringJoint = true;
        break;
      }
    }
    // A steering joint turns by its own bundle (steeringTransform), not as a
    // turret joint of the parts below it.
    for (std::size_t seat = 0; seat < turretBundles.size() && !steeringJoint; ++seat) {
      const auto& bundles = turretBundles[seat];
      const auto joint = std::ranges::find_if(bundles, drives(key));
      if (joint != bundles.end()) {
        ancestry.emplace_back(part.depth, TurretJoint{static_cast<unsigned>(seat),
            static_cast<std::size_t>(joint - bundles.begin()), part.transform, parentRest,
            registry_->find(part.templateName)->number("automaticYawStabilization").value_or(0) != 0});
      }
    }
    // cockpitSubGeom selects a replacement hull, not a frozen turret:
    // USTNK_M1A2.tweak places both the gun and the driver's view beneath the
    // authored RotationalBundle hierarchy in first person too.
    if (!ancestry.empty()) {
      std::vector<TurretJoint> joints;
      for (const auto& node : ancestry) joints.push_back(node.second);
      if (part.geometryPart >= 0) {
        parts.push_back({part.geometryPart, {}, {}, joints, std::nullopt, false});
      }
      // A weapon on a turret with a mesh of its own (the M1A2's HMG).
      if (!cockpit && (part.className == "GenericFireArm" || part.className == "SimpleObject") &&
          !part.geometryName.empty()) {
        guns.emplace_back(&part, std::move(joints));
      }
    } else if (!cockpit && part.geometryPart >= 0 && part.className == "Spring" &&
               key.find("wheel") != std::string::npos) {
      // A wheel: a Spring part of the hull's mesh named for it (`*_Wheel*` in
      // every land vehicle's .tweak), turning about its own x.
      auto axle = normalize(transformDirection(part.transform, {1, 0, 0}));
      if (axle.x < 0.0f) axle = axle * -1.0f;
      parts.push_back({part.geometryPart, transformPoint(part.transform, {}), axle, {},
                       steeringAncestry.empty() ? std::nullopt : std::optional(steeringAncestry.back().second),
                       true});
    } else if (part.geometryPart >= 0 && !steeringAncestry.empty() &&
               steeringAncestry.back().first == part.depth) {
      parts.push_back({part.geometryPart, {}, {}, {}, steeringAncestry.back().second, false});
    }
    if (cockpit || visuals.hasEngine || part.className != "Engine") continue;
    // EngineBase::initSound 0x6304e0: an EngineSound unless
    // `audio.transformationRelativeRoot` (EngineTemplate +0x6f1). Its sounds
    // are makeSoundTemplateName(suffix) (0x58c8f0): S_<engine>_Idle, _Rpm1,
    // _Rpm2 in the data (JEEP_FAAV.tweak lines 419–425).
    const auto* engine = registry_->find(part.templateName);
    const auto* audio = engine ? engine->component("audio") : nullptr;
    const auto audioFlag = [audio](const char* flag) {
      if (!audio) return false;
      const auto found = audio->properties.find(flag);
      return found != audio->properties.end() && !found->second.empty() && found->second.back().asFloat(0) != 0;
    };
    if (!engine || audioFlag("transformationrelativeroot")) continue;
    visuals.hasEngine = true;
    visuals.engineSettings.newStyleAudio = audioFlag("newstyleaudio");
    visuals.engineType = game::engineTypeFromName(engine->text("setEngineType"));
    visuals.engineBundle = game::rotationalSettings(*engine);
    visuals.engineUpdate = game::engineUpdateSettings(*engine);
    constexpr std::array<const char*, 3> suffixes{"_Idle", "_Rpm1", "_Rpm2"};
    for (std::size_t slot = 0; slot < suffixes.size(); ++slot) {
      const auto* sound = registry_->find("S_" + part.templateName + suffixes[slot]);
      if (!sound) continue;
      auto& voice = visuals.engineVoices[slot];
      // Template defaults as in SmokeFlare (BF2Audio.dll 0x10017560).
      voice.volume = std::clamp(sound->number("volume").value_or(1), 0.0f, 1.0f);
      voice.pitch = std::max(0.01f, sound->number("pitch").value_or(1));
      voice.is3d = sound->number("is3dSound").value_or(1) != 0;
      SoundEnvelope::parse(sound->text("volumeEnvelope"), voice.volumeEnvelope, nullptr, nullptr);
      SoundEnvelope::parse(sound->text("pitchEnvelope"), voice.pitchEnvelope, nullptr, nullptr);
      voice.distance = SoundDistance::compute(voice.volume, sound->number("minDistance").value_or(1),
                                              sound->number("halfVolumeDistance").value_or(0),
                                              kMaxDistanceCutoff);
      const auto paths = sound->text("soundFilename");
      const auto bytes = files_->read(normalizeAssetPath(paths.substr(0, paths.find(','))));
      if (!bytes || bytes->empty()) continue;
      Uint8* pcm = nullptr;
      Uint32 size = 0;
      if (SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes->data(), bytes->size()), true, &voice.format, &pcm, &size)) {
        std::unique_ptr<Uint8, decltype(&SDL_free)> owned(pcm, SDL_free);
        voice.sample.assign(pcm, pcm + size);
      }
    }
  }

  // The hull's mesh cut by part: every triangle of a moving part's geometry
  // goes to that part's own mesh, the rest stays the chassis.
  const gfx::GpuMesh* chassis = nullptr;
  if (!parts.empty() && mesh.vertexPart.size() == mesh.vertices.size()) {
    std::vector<mesh::RenderMesh> pieces(parts.size() + 1);
    std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> remap(pieces.size());
    for (const auto& range : mesh.ranges) {
      for (std::size_t index = 0; index < pieces.size(); ++index) {
        auto& piece = pieces[index];
        auto material = range;
        material.indexStart = static_cast<std::uint32_t>(piece.indices.size());
        for (std::uint32_t at = range.indexStart; at + 2 < range.indexStart + range.indexCount; at += 3) {
          const int part = mesh.vertexPart[mesh.indices[at]];
          const bool samePart = mesh.vertexPart[mesh.indices[at + 1]] == part &&
                                mesh.vertexPart[mesh.indices[at + 2]] == part;
          const auto found = samePart ? std::ranges::find(parts, part, &MovingPart::geometry) : parts.end();
          if (static_cast<std::size_t>(found - parts.begin()) != index) continue;
          for (int corner = 0; corner < 3; ++corner) {
            const auto source = mesh.indices[at + corner];
            const auto [entry, added] = remap[index].try_emplace(
                source, static_cast<std::uint32_t>(piece.vertices.size()));
            if (added) piece.vertices.push_back(mesh.vertices[source]);
            piece.indices.push_back(entry->second);
          }
        }
        material.indexCount = static_cast<std::uint32_t>(piece.indices.size()) - material.indexStart;
        if (material.indexCount) piece.ranges.push_back(std::move(material));
      }
    }
    bool complete = true;
    for (std::size_t index = 0; index < parts.size(); ++index) {
      const auto& part = parts[index];
      auto& wheel = pieces[index];
      if (wheel.indices.empty()) continue;
      wheel.bounds = mesh.bounds;
      const auto* gpu = upload(wheel);
      if (!gpu) {
        complete = false;
        break;
      }
      if (!part.joints.empty()) {
        visuals.turretPieces.push_back({gpu, part.joints});
        continue;
      }
      if (!part.spring && part.steering) {
        visuals.steeringPieces.push_back({gpu, *part.steering});
        continue;
      }
      // The wheel's radius: its farthest vertex from the axle.
      float radius = 0.0f;
      std::vector<Vec3f> contacts;
      for (const auto& vertex : wheel.vertices) {
        const auto& point = vertex.position;
        const Vec3f position{point.x, point.y, point.z};
        const auto fromAxle = position - part.pivot;
        radius = std::max(radius, length(fromAxle - part.axle * dot(fromAxle, part.axle)));
        if (visuals.groundSuspension) contacts.push_back(position);
      }
      // UV seams duplicate positions but do not change the tire's support.
      const auto coordinates = [](Vec3f point) { return std::array{point.x, point.y, point.z}; };
      std::ranges::sort(contacts, {}, coordinates);
      const auto duplicates = std::ranges::unique(contacts, {}, coordinates);
      contacts.erase(duplicates.begin(), duplicates.end());
      // A tire under 0.15 m is held at 0.15 m, so a degenerate one does not
      // spin wildly: ours, **not measured**.
      visuals.wheels.push_back({gpu, part.pivot, part.axle, std::max(radius, 0.15f),
                                part.steering, std::move(contacts)});
      if (part.steering) {
        visuals.steeringRange = std::max(visuals.steeringRange,
            std::max(std::abs(part.steering->minRotation.x), std::abs(part.steering->maxRotation.x)));
      }
    }
    if (visuals.wheels.size() >= 2) {
      const auto [rear, front] = std::ranges::minmax_element(visuals.wheels, {},
          [](const Wheel& wheel) { return wheel.pivot.z; });
      visuals.wheelbase = front->pivot.z - rear->pivot.z;
    }
    pieces.back().bounds = mesh.bounds;
    if (complete && (!visuals.wheels.empty() || !visuals.turretPieces.empty() ||
                     !visuals.steeringPieces.empty())) {
      chassis = upload(pieces.back());
    }
  }
  if (!chassis) {
    chassis = upload(mesh);
    visuals.wheels.clear();
    visuals.steeringPieces.clear();
    visuals.turretPieces.clear();
  }
  if (chassis) {
    for (const auto& [part, joints] : guns) {
      const auto gun = game::buildObjectMesh(*files_, *registry_, part->templateName,
                                             0, options_.lodIndex, false, true);
      if (const auto* gpu = gun ? upload(*gun) : nullptr) {
        visuals.turretPieces.push_back({gpu, joints, part->transform});
      }
    }
  }
  if (chassis && (!visuals.wheels.empty() || !visuals.turretPieces.empty() ||
                  !visuals.steeringPieces.empty() || visuals.hasEngine)) {
    const auto loaded = [&](std::size_t slot) { return visuals.engineVoices[slot].sample.empty() ? "-" : "loaded"; };
    std::printf("  %s: %zu separate wheels, %zu steering parts, %zu turret parts, engine %s "
                "(idle %s, rpm1 %s, rpm2 %s)\n",
                name.c_str(), visuals.wheels.size(), visuals.steeringPieces.size(),
                visuals.turretPieces.size(),
                !visuals.hasEngine ? "none" : visuals.engineSettings.newStyleAudio ? "new-style" : "old-style",
                loaded(0), loaded(1), loaded(2));
    vehicleParts_.emplace(chassis, std::move(visuals));
  }
  return chassis;
}

void WorldView::drawVehicle(const gfx::GpuMesh& model, std::uint16_t id, const Mat4& transform,
                            Vec3f eye, int frame, float step, bool occupied, bool hull,
                            std::vector<gfx::MeshRenderer::DrawItem>& items) {
  if (hull) items.emplace_back(&model, transform);
  const auto parts = vehicleParts_.find(&model);
  if (parts == vehicleParts_.end()) return;
  auto& motion = vehicleMotion_[id];
  if (motion.model != &model || motion.frame != frame - 1) motion = {};
  const Vec3f position = transformPoint(transform, {});
  const auto& visuals = parts->second;
  const auto forward = visuals.groundSuspension
      ? normalize(Vec3f{transform.m[8], 0, transform.m[10]})
      : transformDirection(transform, {0, 0, 1});
  const bool own = remote_->world.ownObject() == id;
  // Another player's vehicle whose steered bundles are not on the wire: the
  // steering is estimated from how its hull turns over the distance it rolled,
  // the bicycle model over the wheelbase. Ours, **not measured**, and so are its
  // rates: a jump of more than 2 m (or 45 m/s) is a respawn or a snap and not a
  // roll, the speed follows at 8 per second and the steering at 12.
  float steering = 0.0f;
  if (motion.model && step > 0.0f) {
    const auto delta = position - motion.position;
    const float advance = dot(delta, normalize(forward + motion.forward));
    if (length(delta) < std::max(2.0f, step * 45.0f)) {
      motion.distance += advance;
      motion.speed = std::lerp(motion.speed, std::abs(advance) / step, std::min(1.0f, step * 8.0f));
      if (std::abs(advance) > step * 0.5f && visuals.steeringRange > 0.0f) {
        const float yaw = std::atan2(cross(motion.forward, forward).y, dot(motion.forward, forward));
        const float angle = std::atan(visuals.wheelbase * yaw / advance);
        steering = angle * 180.0f / (std::numbers::pi_v<float> * visuals.steeringRange);
      }
    }
  }
  if (own) {
    steering = static_cast<float>(remote_->pendingKeys.axes[net::bf2::kAxisYaw]) / net::bf2::kVehicleAxisFull;
  }
  motion.steering = std::lerp(motion.steering, std::clamp(steering, -1.0f, 1.0f),
                              std::clamp(step * 12.0f, 0.0f, 1.0f));
  if (own && step > 0.0f) {
    // The vehicle we drive: each steered joint's bundle runs on our key input
    // at the engine's tick (WorldPref::mTickTime 1/30), as the server runs it
    // (calculateAndClipAngle, Linux 0x585a40: setAutomaticReset,
    // snapToZeroOnNoInput).
    const auto axis = [&](game::RotationInput input) { return rotationInputAxis(input, remote_->pendingKeys); };
    const auto stepJoint = [&](const SteeringJoint& joint) {
      if (!joint.settings.supported) return;
      const Vec3f input{axis(joint.settings.inputs[0]), axis(joint.settings.inputs[1]),
                        axis(joint.settings.inputs[2])};
      joint.ownPending += step;
      while (joint.ownPending >= server::kTickTime) {
        game::stepRotationalBundle(joint.own, joint.settings, input, server::kTickTime);
        joint.ownPending -= server::kTickTime;
      }
    };
    for (const auto& wheel : visuals.wheels) {
      if (wheel.steering) stepJoint(*wheel.steering);
    }
    for (const auto& piece : visuals.steeringPieces) stepJoint(piece.joint);
  }
  motion.position = position;
  motion.forward = forward;
  motion.model = &model;
  motion.frame = frame;

  for (const auto& wheel : visuals.wheels) {
    const Mat4 mounted = wheel.steering
        ? transform * steeringTransform(*wheel.steering, motion.steering, id) : transform;
    // Rolled by the distance the hull covered over the tire's radius.
    const float half = std::remainder(motion.distance / wheel.radius, 2 * std::numbers::pi_v<float>) * 0.5f;
    const float sine = std::sin(half);
    const Mat4 spin = rotationFromQuaternion({wheel.axle.x * sine, wheel.axle.y * sine,
                                              wheel.axle.z * sine, std::cos(half)});
    const Mat4 rolling = mounted * translation(wheel.pivot) * spin * translation(wheel.pivot * -1.0f);
    // The wheel lifted out of the terrain where its tread would sink into it:
    // ours, not the engine's suspension (the Spring's own travel is not read).
    float lift = 0.0f;
    if (remote_->terrain) {
      for (const auto& vertex : wheel.contactVertices) {
        const auto point = transformPoint(rolling, vertex);
        lift = std::max(lift, remote_->terrain->groundHeightAt(point) - point.y);
      }
    }
    items.emplace_back(wheel.mesh, translation({0, lift, 0}) * rolling);
  }
  for (const auto& piece : visuals.steeringPieces) {
    items.emplace_back(piece.mesh, transform * steeringTransform(piece.joint, motion.steering, id));
  }
  for (const auto& piece : visuals.turretPieces) {
    items.emplace_back(piece.mesh, transform * vehicleTurretTransform(piece, id));
  }

  // Retail's EngineSound (docs/functions/engine-sound.md). The engine is on
  // while EngineBase +0x2c8 is set (EngineBase::handleMessage 0x6300cd,
  // setIsRunning); which message sets it on the client is not established,
  // so here: while the vehicle is occupied. A ghost's rpm, gear and load are
  // what its EngineNetworkable last set (Bf2World::vehicleEngine); before one
  // arrives, EngineBase's constructor values (0x6308f0: rpm 0, load 0, gear 1).
  // The vehicle we drive runs Engine::handleUpdate (0x62f020) on our input.
  if (!occupied || !visuals.hasEngine) {
    for (auto& play : motion.engine) {
      play.voice.reset();
      play.stream.reset();
    }
    motion.engineSound.reset();
    motion.engineBundle = {};
    motion.engineState = {};
    return;
  }
  if (!motion.engineSound) {
    // setIsRunning(true, …) 0x7629b0: resetAudioRpm, velocity 0, play all.
    motion.engineSound.emplace(visuals.engineSettings);
    motion.engineTick = 0.0f;
    motion.engineState.on = true;
  }
  // updateEngineSound 0x6302bd: the root object's velocity.
  motion.engineSound->setVelocity(forward * motion.speed);
  // EngineSound steps 1/30 s (0xb2fcc0); one update per engine tick.
  motion.engineTick += step;
  const auto random = [](void* state) {
    // Not BF2.exe's generator (not established): a 31-bit LCG.
    auto& seed = *static_cast<std::uint32_t*>(state);
    seed = seed * 1103515245u + 12345u;
    return seed & 0x7fffffffu;
  };
  const double systemTime = static_cast<double>(SDL_GetTicks()) / 1000.0;
  EngineSound::Engine input{.running = true};
  if (own) {
    // calculateAndClipAngle then handleUpdate's engine step, each tick; the
    // bundle's roll input (+0x260, setInputToRoll) is "running".
    const Vec3f keys{rotationInputAxis(visuals.engineBundle.inputs[0], remote_->pendingKeys),
                     rotationInputAxis(visuals.engineBundle.inputs[1], remote_->pendingKeys),
                     rotationInputAxis(visuals.engineBundle.inputs[2], remote_->pendingKeys)};
    for (float pending = motion.engineTick; pending >= kEngineSoundStep; pending -= kEngineSoundStep) {
      game::stepRotationalBundle(motion.engineBundle, visuals.engineBundle, keys, kEngineSoundStep);
      game::engineHandleUpdate(motion.engineState, visuals.engineUpdate, motion.engineBundle.angle, keys.z,
                               std::abs(motion.speed), kEngineSoundStep);
    }
    const auto& engine = motion.engineState;
    input.rpm = engineSoundRpm(visuals.engineType, engine.rpm, engine.field2ec);
    input.running = keys.z != 0.0f;
    input.load = engine.load;
    input.gear = engine.gear;
  } else if (const auto* net = remote_->world.vehicleEngine(id)) {
    // updateEngineSound 0x6302c2…0x63037d. +0x2ec (type 0x20) is not kept.
    input.rpm = engineSoundRpm(visuals.engineType, net->rpm, 0.0f);
    input.load = net->load;
    input.gear = static_cast<int>(net->gear);
  }
  while (motion.engineTick >= kEngineSoundStep) {
    motion.engineTick -= kEngineSoundStep;
    // The Load (gear-shift) sound is not played yet.
    motion.engineSound->update(input, systemTime, random, &motion.engineRandom);
  }
  // Idle (start_idle_stop.wav, stopType 2: start, loop and stop segments)
  // is not played yet; Rpm1 and Rpm2 loop.
  const float distance = length(position - eye);
  for (std::size_t slot = EngineSound::Rpm1; slot <= EngineSound::Rpm2; ++slot) {
    const auto& voice = visuals.engineVoices[slot];
    auto& play = motion.engine[slot];
    if (voice.sample.empty()) continue;
    const auto which = static_cast<EngineSound::Slot>(slot);
    const float x = motion.engineSound->x(which);
    // ScaleEvaluator::getValue 0x763ca0: scale × the template's volume
    // envelope at x (row 0: no engine envelope is randomized), times the
    // template volume (0x10016100).
    const float scale = motion.engineSound->scale(which);
    const float level = voice.volume * (voice.volumeEnvelope.empty() ? scale : scale * voice.volumeEnvelope.evaluate(x, 0));
    // One pool for every sample (sound_voices.h); priority 0x10005510. A
    // looping sample whose source is taken is re-queued kRequeueTicks on
    // (0x10006ba0: its length is endless, so soundRequeues holds).
    const float priority = soundPriority(voice.is3d, level, voice.distance, distance);
    if (play.stream && play.voice.id() != 0 && !play.voice.held()) {
      play.voice.reset();
      play.stream.reset();
      play.retry = static_cast<float>(kRequeueTicks) * kSoundTick;
    }
    if (!play.stream) {
      play.retry -= step;
      if (play.retry > 0.0f) continue;
      if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
      const std::uint32_t claimed = voices_.claim(priority);
      if (claimed == 0) continue;
      SoundVoice held(&voices_, claimed);
      play.stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &voice.format, nullptr, nullptr));
      if (!play.stream) continue;
      voices_.attach(claimed, play.stream.get());
      play.voice = std::move(held);
      SDL_ResumeAudioStreamDevice(play.stream.get());
      std::printf("  vehicle %u engine sound %zu started\n", id, slot);
    }
    auto* stream = play.stream.get();
    const float attenuation = voice.is3d ? voice.distance.gain(distance) : 1.0f;
    SDL_SetAudioStreamGain(stream, level * attenuation);
    SDL_SetAudioStreamFrequencyRatio(stream, soundPitch(voice.pitch, voice.pitchEnvelope, x, 0));
    if (SDL_GetAudioStreamQueued(stream) < static_cast<int>(voice.sample.size() / 2)) {
      SDL_PutAudioStreamData(stream, voice.sample.data(), static_cast<int>(voice.sample.size()));
    }
  }
}

Mat4 WorldView::vehicleTurretTransform(const TurretPiece& piece, std::uint16_t root) const {
  auto turret = Mat4::identity();
  for (const auto& joint : piece.joints) {
    if (const auto angle = displayedTurretAngle(root, joint.seat, joint.bundle)) {
      turret = game::composeTurretRotation(turret, joint.rest, joint.parentRest, *angle, joint.stabilizeYaw);
    }
  }
  return turret * piece.place;
}

const gfx::GpuMesh* WorldView::vehicleMesh(std::uint16_t id,
                                           const net::bf2::RemoteObject& object) {
  // What it is: the template its number names (obf2/game/template_numbers.h),
  // else what was placed on the spot it was **created** on — a jeep that drove
  // off stays a jeep. Resolved again when the object's template or creation spot
  // changes: the server reuses ids.
  auto resolved = resolved_.find(id);
  if (resolved == resolved_.end() || resolved->second.templateId != object.templateId ||
      length(resolved->second.at - object.createdAt) > 0.01f) {
    auto placed = placement_ && object.hasCreationPosition ? placement_->at(object.createdAt)
                                                           : level::PlacedAt{};
    const std::string* name = object.templateId ? remote_->templateNumbers.nameOf(object.templateId) : nullptr;
    if (name && registry_->find(*name) &&
        normalizeAssetPath(placed.templateName) != normalizeAssetPath(*name)) {
      if (placed.kind != level::PlacedAt::Kind::Spawned) placed.hasRotation = false;
      placed.kind = level::PlacedAt::Kind::Spawned;
      placed.templateName = *name;
    }
    resolved = resolved_.insert_or_assign(id, Resolved{object.createdAt, object.templateId, placed}).first;
  }
  const level::PlacedAt& placed = resolved->second.placed;
  if (placed.kind != level::PlacedAt::Kind::Spawned) return nullptr;
  return worldObjectMesh(placed.templateName);
}

const gfx::GpuMesh* WorldView::worldObjectMesh(const std::string& name) {
  const std::string key = normalizeAssetPath(name);
  auto found = vehicleByName_.find(key);
  if (found == vehicleByName_.end()) {
    const gfx::GpuMesh* uploaded = nullptr;
    if (auto built = game::buildObjectMesh(*files_, *registry_, name, options_.geometryIndex,
                                           options_.lodIndex, false)) {
      const auto* definition = registry_->find(name);
      if (definition && definition->className == "PlayerControlObject") {
        uploaded = uploadVehicleParts(std::move(*built), name);
      } else if (auto gpu = renderer_->upload(*built, resolve_)) {
        vehicleMeshes_.push_back(std::move(*gpu));
        uploaded = &vehicleMeshes_.back();
      }
    }
    std::printf("  world model: %s%s\n", name.c_str(), uploaded != nullptr ? "" : " (no geometry)");
    found = vehicleByName_.emplace(key, uploaded).first;
  }
  return found->second;
}

const gfx::GpuMesh* WorldView::cockpitMesh(const std::string& name, int geometry) {
  const auto [mesh, inserted] =
      vehicleByName_.try_emplace(normalizeAssetPath(name) + "|cockpit|" + std::to_string(geometry), nullptr);
  if (inserted) {
    if (geometry < 0) {
      const auto authored = game::authoredCockpitGeometry(*files_, *registry_, name);
      if (!authored) return nullptr;
      geometry = *authored;
    }
    if (auto built = game::buildObjectMesh(*files_, *registry_, name, geometry, 0, false, false)) {
      mesh->second = uploadVehicleParts(std::move(*built), name, true);
    }
  }
  return mesh->second;
}

void WorldView::collect(std::vector<gfx::MeshRenderer::DrawItem>& out, int frame,
                        float frameStep, const Vec3f& eye) {
  if (!boxesReady_ || remote_ == nullptr) return;
  const bool tracing = frame >= options_.traceFrom && frame < options_.traceFrom + options_.traceFrames;
  if (cameraRound_ != remote_->roundResets) vehicleMotion_.clear();
  std::erase_if(vehicleMotion_, [frame](const auto& entry) { return entry.second.frame < frame - 1; });

  // The vehicle we sit in, and whether we see it from inside (its cockpit).
  const auto localVehicle = remote_->playerSpawned ? remote_->vehicleRootOf(remote_->ourSoldier) : 0;
  const auto localVehiclePose = remote_->occupiedVehiclePose();
  if (!localVehicle) cameraSeat_ = 0;
  const bool insideView = localVehicle && cameraSeat_ == remote_->ourSoldier && !externalCamera_;
  vehicleDrawTransforms_.clear();
  const auto& objects = remote_->world.objects();
  // By class, not by team: a jeep a player entered has a team and is still a
  // jeep, and a soldier whose `EnterVehicleEvent` came before we joined has none
  // and is still a soldier. Seated, our own soldier is the infantry object.
  const auto isSoldier = [&](const auto& entry) {
    return entry.second.netClass == net::bf2::GhostClass::Soldier ||
           (localVehicle && remote_->playerSpawned && entry.first == remote_->infantryObject);
  };
  // The vehicles first: a seated soldier is drawn at his hull's place this frame.
  std::vector<const std::remove_reference_t<decltype(objects)>::value_type*> drawOrder;
  drawOrder.reserve(objects.size());
  for (const auto& entry : objects) if (!isSoldier(entry)) drawOrder.push_back(&entry);
  for (const auto& entry : objects) if (isSoldier(entry)) drawOrder.push_back(&entry);

  for (const auto* entry : drawOrder) {
    const auto& [id, object] = *entry;
    const bool localSeated = localVehicle != 0 && id == remote_->infantryObject;
    // We do not draw ourselves from inside: on foot, nor seated with the
    // cockpit's view.
    if (id == remote_->ourSoldier && !localVehicle && !options_.showOwnBox) continue;
    if (localSeated && insideView) continue;
    // A seat's own object (`NetworkManager::analyseObject`'s ids after the
    // root's) carries no template: the vehicle draws it.
    if (!object.templateId && remote_->vehicleSeatOf(id)) continue;
    const bool soldier = isSoldier(*entry);
    const auto* owner = soldier ? remote_->world.infantryOwnerOf(id) : nullptr;
    const auto occupied = localSeated ? remote_->ourSoldier
                                      : owner && owner->object != id ? owner->object : 0;
    // Drawn where its track puts it now (ghost_track.h), not where the last update
    // left it; the hull we sit in where our own prediction has it.
    auto pose = remote_->world.poseOf(id, remote_->tick.pending / server::kTickTime);
    if (id == localVehicle && localVehiclePose) pose = localVehiclePose;
    Vec3f drawAt = pose ? pose->position : object.position;
    if (soldier && pose && !options_.drawPredicted) drawAt = carry(id, object, *pose);

    // The measure of smoothness: how the pose was made, and the largest step
    // between two frames for an object that is moving.
    if (pose && object.track.count() >= 2) {
      DrawStat& stat = stats_[id];
      ++stat.frames[static_cast<int>(pose->mode)];
      if (stat.seen) stat.largestStep = std::max(stat.largestStep, length(drawAt - stat.last));
      stat.last = drawAt;
      stat.seen = true;
    }

    if (!soldier) {
      const auto found = resolved_.find(id);
      const bool knownStatic = found != resolved_.end() &&
                               found->second.placed.kind == level::PlacedAt::Kind::Static;
      // A static placement is skipped: the level draws it already.
      if (knownStatic) continue;
      if (const gfx::GpuMesh* vehicle = vehicleMesh(id, object)) {
        // The rotation is the object's own from its record (bit 0x1, a
        // quaternion — docs/functions/network-events.md), and the spawner's until
        // one has arrived.
        const level::PlacedAt& placed = resolved_[id].placed;
        Mat4 place = translation(drawAt);
        const auto rotation = pose && pose->rotation ? pose->rotation : object.rotation;
        if (rotation) {
          // The engine's quaternion is a row-vector one: conjugated for ours.
          const auto& r = *rotation;
          const Mat4 q = rotationFromQuaternion({-r[0], -r[1], -r[2], r[3]});
          place = place * q;
          if (placed.hasRotation && rotationReported_.insert(id).second) {
            const float yaw = std::atan2(q.m[8], q.m[10]) * 180.0f / 3.14159265f;
            std::printf("  object %u's rotation: yaw %.1f from its record, %.1f from its "
                        "spawner\n",
                        id, yaw, placed.rotation.x);
          }
        } else if (placed.hasRotation) {
          place = place * rotationYawPitchRoll(placed.rotation.x, placed.rotation.y,
                                               placed.rotation.z);
        }
        const auto* name = remote_->templateNumbers.nameOf(object.templateId);
        const auto* definition = name ? registry_->find(*name) : nullptr;
        if (definition && definition->className == "PlayerControlObject") {
          vehicleDrawTransforms_.emplace(id, place);
        }
        // From inside, the seat's cockpit geometry stands for the hull when the
        // vehicle has one; a seat without (USJEP_HMMWV.tweak's Codriver and
        // Rear_Passenger have no cockpitSubGeom) sees the hull itself.
        const auto* inside = insideView && id == localVehicle && name
            ? cockpitMesh(*name, cockpitGeometry_.value_or(-1)) : nullptr;
        drawVehicle(*vehicle, id, place, eye, frame, frameStep,
                    remote_->world.ownerOf(id) != nullptr || localVehicle == id, inside == nullptr, out);
        if (inside) {
          out.emplace_back(inside, place);
          const auto motion = vehicleMotion_.find(id);
          const auto parts = vehicleParts_.find(inside);
          if (parts != vehicleParts_.end()) {
            if (motion != vehicleMotion_.end()) {
              for (const auto& piece : parts->second.steeringPieces) {
                out.emplace_back(piece.mesh, place * steeringTransform(piece.joint, motion->second.steering, id));
              }
            }
            // The cockpit geometry's turret subparts are cut out of its hull too
            // (M1A2 geometry 0 under USTNK_M1A2_Turret, ustnk_m1a2.tweak);
            // without them the driver sees no turret.
            for (const auto& piece : parts->second.turretPieces) {
              out.emplace_back(piece.mesh, place * vehicleTurretTransform(piece, id));
            }
          }
        }
        continue;
      }
      if (resolved_[id].placed.kind == level::PlacedAt::Kind::Static) continue;
    }

    // Every object that ends up a box says why, once: a box is a debt.
    if (!soldier && boxReported_.insert(id).second) {
      const auto found = resolved_.find(id);
      const int kind =
          found == resolved_.end() ? -1 : static_cast<int>(found->second.placed.kind);
      std::printf("  box: object %u, template %u, class %d, created at %.1f %.1f %.1f, "
                  "now %.1f %.1f %.1f, placement kind %d\n",
                  id, object.templateId, static_cast<int>(object.netClass), object.createdAt.x,
                  object.createdAt.y, object.createdAt.z, drawAt.x, drawAt.y, drawAt.z, kind);
    }

    // Whose soldier this is the server knows: the team came from
    // `CreatePlayerEvent` and the object from `EnterVehicleEvent`. Zero means "not
    // a player".
    const int which = object.team == 0 ? 0 : (object.team == remote_->world.ownTeam() ? 1 : 2);
    // A soldier's networked position is his pivot, `coll-soldier-pivot-height`
    // above the feet (`FUN_006ed4c0`); the box stands on its base.
    Vec3f base = drawAt;
    if (soldier) base.y -= remote_->physics.pivotHeight;
    Mat4 place = translation(base);
    if (soldier && pose) place = place * rotationY(pose->bodyYaw * 3.14159265f / 180.0f);
    // The watched soldier's height as drawn: the pose the track gave, how it was
    // made, and the newest update's vertical velocity — which the extrapolation
    // runs along (`SoldierNetworkable::predict`, Linux 0x5dc278).
    if (soldier && pose && id == watched_ && remote_->terrain != nullptr && tracing) {
      const auto* newestUpdate = object.track.newest();
      std::printf("    drawn %u: frame %d, mode %d, predicted y %.2f, drawn y %.2f, "
                  "velocity y %.2f, newest y %.2f\n",
                  id, frame, static_cast<int>(pose->mode), pose->position.y, drawAt.y,
                  newestUpdate && newestUpdate->velocity ? newestUpdate->velocity->y : 0.0f,
                  newestUpdate ? newestUpdate->position.y : 0.0f);
    }
    if (soldier && pose && tracing) {
      const auto* newest = object.track.newest();
      std::printf("    soldier %u: at %.4f %.4f %.4f yaw %.3f mode %d, samples %zu, newest "
                  "%.4f %.4f %.4f at tick %.1f\n",
                  id, base.x, base.y, base.z, pose->bodyYaw, static_cast<int>(pose->mode),
                  object.track.count(), newest ? newest->position.x : 0.0f,
                  newest ? newest->position.y : 0.0f, newest ? newest->position.z : 0.0f,
                  newest ? newest->timeMs / net::bf2::kGhostTickMs : 0.0f);
    }

    if (soldier) {
      // In a vehicle: his player's object is a seat (`EnterVehicleEvent`), not
      // this soldier. The engine moves the soldier into the seat
      // (`Soldier::onEnter`, Linux 0x54b2f0: the vehicle at +0x398, the pose
      // cleared, the camera and the spawn point to the seat), and his own ghost
      // goes silent there. An open seat (`seatInformation`) draws him at its
      // place on this frame's hull; a closed one hides him.
      std::optional<SeatAt> seated;
      if (occupied) {
        seated = seatOf(occupied, true);
        if (!seated && remote_->vehicleRootOf(occupied)) continue;
      }
      if (seated) {
        DrawStat& stat = stats_[id];
        stat.vehicle = seated->vehicle;
        ++stat.framesInVehicle;
        if (!seated->seat->hasSeatInformation) {
          ++stat.framesInClosedSeat;
          continue;
        }
        ++stat.framesSeated;
        place = seated->place;
      } else if (owner != nullptr && owner->object != id && owner->object != 0) {
        // A seat we cannot place (its vehicle's seats unknown): drawn where his
        // own records put him while they keep coming, not at all otherwise.
        const auto seat = remote_->world.objects().find(owner->object);
        if (seat != remote_->world.objects().end() &&
            seat->second.netClass == net::bf2::GhostClass::SimpleObject) {
          DrawStat& stat = stats_[id];
          if (stat.recordsAtEntry < 0 || stat.vehicle != owner->object) {
            stat.recordsAtEntry = object.soldierRecords;
            stat.vehicle = owner->object;
          }
          ++stat.framesInVehicle;
          stat.recordsInVehicle = object.soldierRecords - stat.recordsAtEntry;
          if (stat.recordsInVehicle == 0) {
            ++stat.framesHiddenInVehicle;
            continue;
          }
        }
      }
      const std::string* soldierName = remote_->templateNumbers.nameOf(object.templateId);
      const std::uint32_t kit = remote_->world.kitTemplateOf(id);
      const std::string* kitName = kit != 0 ? remote_->templateNumbers.nameOf(kit) : nullptr;
      // The weapon he has out is his state's 0x1000, the item's `itemIndex`
      // (soldier_state.h). Until a record carrying it is read — or when it says
      // nothing is out — the kit's main weapon, slot 3, stands in.
      const int weaponItem = object.lastWeaponIndex >= 0 ? object.lastWeaponIndex : 3;
      const std::string kitText = kitName != nullptr ? *kitName : std::string();
      Look* look = soldierName != nullptr
          ? lookFor(*soldierName, kitText, weaponItem, seated.has_value()) : nullptr;
      if (look != nullptr) {
        const std::string key = lookKey(*soldierName, kitText, weaponItem, seated.has_value());
        DrawnSoldier& drawn = drawn_[id];
        // A soldier who respawned with another kit is another look, so his mesh is
        // built again.
        if (drawn.look != look || drawn.key != key) {
          drawn = DrawnSoldier{};
          drawn.look = look;
          drawn.key = key;
          const auto length = [this](const std::string& path) {
            const mesh::BoneAnimation* clip = clipAt(path);
            return clip != nullptr ? clip->duration() : 0.0f;
          };
          drawn.legs.setLengthOf(length);
          drawn.weapon.setLengthOf(length);
          drawn.seat.setLengthOf(length);
        }

        // The state the conditions read: the speed and the direction the server
        // itself reports for this soldier, turned into his own frame. The yaw a
        // ghost carries is the body's plus the aim's, which is the matrix the
        // movement is built from (docs/functions/soldier-physics.md).
        anim::State animState;
        // The pose the `PoseTrigger` picks its branch by is the soldier's own
        // (`setTriggerPose(+0x3c8)`, Linux 0x55490f), which his ghost carries in
        // bit 0x20. The trigger clamps an index past its last child itself.
        // Seated he stands: `Soldier::onEnter` (Linux 0x54b2f0) clears the
        // current and requested stance, while his silent ghost may still hold
        // the pose he had before.
        animState.pose = seated ? anim::Pose::Stand
                                : static_cast<anim::Pose>(std::min<std::uint32_t>(object.lastPose, 3));
        const auto* newest = object.track.newest();
        const Vec3f velocity = newest != nullptr && newest->velocity ? *newest->velocity : Vec3f{};
        const float planar = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        animState.speed = planar;
        if (planar > 0.001f && pose) {
          const float yawRadians = pose->bodyYaw * 3.14159265358979323846f / 180.0f;
          const float forward =
              (velocity.x * std::sin(yawRadians) + velocity.z * std::cos(yawRadians)) / planar;
          const float side =
              (velocity.x * std::cos(yawRadians) - velocity.z * std::sin(yawRadians)) / planar;
          animState.direction[0] = side;
          animState.direction[1] = 0.0f;
          animState.direction[2] = forward;
        }
        // His seat's own system (`seatAnimationSystem`), stepped with no state of
        // its own: the passenger's clips.
        const anim::System* seatAnimation =
            seated ? animationSystem(seated->seat->animationSystem) : nullptr;
        if (seatAnimation != nullptr) drawn.seat.update(*seatAnimation, anim::State{}, frameStep);
        if (look->legs) drawn.legs.update(*look->legs, animState, frameStep);
        // A round: while his player's firing state is on (`setFiringState`, the
        // player's 0x40), one once-message 1 every `1800 / rpm` ticks — the
        // weapon's own rate, the default 600 a minute (weapon_fire.h) — to his
        // weapon's system (`DefaultAnimationComp::postFire`). That the state is
        // "firing" and the cadence is the rate is **inferred**: how the client
        // turns the state into rounds for a ghost is not read.
        {
          const net::bf2::RemotePlayer* firer = remote_->world.ownerOf(id);
          if (firer != nullptr && firer->object == id && firer->firing.value_or(false)) {
            drawn.fireClock += frameStep;
            const float interval = 3.0f / 30.0f;
            if (drawn.fireClock >= interval) {
              drawn.fireClock -= interval;
              animState.messages |= 1u;
              ++firedAnimations_;
            }
          } else {
            drawn.fireClock = 0.0f;
          }
        }
        if (look->weapon) drawn.weapon.update(*look->weapon, animState, frameStep);

        // The legs first and the weapon over them: a clip touches only its own
        // bones, and the weapon's clips own the upper body (`mesh::poseSkeleton`).
        std::vector<mesh::PoseStage> stages;
        const auto addStages = [&](const anim::Player& player) {
          for (const anim::PlayingAnimation& playing : player.playing()) {
            if (playing.weight <= 0.001f) continue;
            const mesh::BoneAnimation* clip = clipAt(playing.path);
            if (clip == nullptr || clip->frameCount == 0) continue;
            // Fractional, and not wrapped here: the clip is sampled between two
            // frames and wraps itself, the way the engine does
            // (`BoneAnimation::sample`). Rounding this down to a whole frame is what
            // made a soldier at sixty frames a second look like ten.
            const float at = playing.time * mesh::kAnimationFramesPerSecond;
            stages.push_back(mesh::PoseStage{clip, at, playing.weight});
          }
        };
        // Soldier::applyAnimationsOnSkeleton, Linux 0x556540: body, weapon,
        // then seat. Passenger clips animate only a subset of the skeleton.
        addStages(drawn.legs);
        addStages(drawn.weapon);
        if (seatAnimation != nullptr) addStages(drawn.seat);
        if (tracing && pose) {
          // The yaw the direction is measured against, next to the heading of the
          // velocity itself. A sprinting soldier cannot strafe
          // (`updateSoldierSpeed`, soldier-physics.md), so above sprint speed the
          // two have to agree — the difference is the error in our yaw.
          const float heading =
              std::atan2(velocity.x, velocity.z) * 180.0f / 3.14159265358979323846f;
          // What the original's animation would read instead: the node's
          // displacement over the last tick times 30
          // (`PointPhysicsNode::postFrameUpdate`, Linux 0x6ddca0).
          float nodeSpeed = -1.0f;
          if (const auto carriedIt = carried_.find(id);
              carriedIt != carried_.end() && carriedIt->second.ready) {
            const Vec3f moved = carriedIt->second.body.position - carriedIt->second.from;
            nodeSpeed = std::sqrt(moved.x * moved.x + moved.z * moved.z) * 30.0f;
          }
          std::printf("    soldier %u node speed %.2f\n", id, nodeSpeed);
          std::printf("    soldier %u anim: speed %.2f, direction %.2f %.2f, yaw %.1f, "
                      "heading %.1f, angles body %.1f aim %.1f 0x8 %.1f pitch %.1f, clips",
                      id, animState.speed, animState.direction[0], animState.direction[2],
                      pose->bodyYaw, heading, object.lastBodyYaw, object.lastAimYaw,
                      object.lastAngle8, object.lastPitch);
          for (const anim::Player* player : {&drawn.legs, &drawn.weapon}) {
            for (const anim::PlayingAnimation& playing : player->playing()) {
              if (playing.weight <= 0.001f) continue;
              const std::size_t slash = playing.path.find_last_of("/\\");
              std::printf(" %s@%.2f×%.2f",
                          slash == std::string::npos ? playing.path.c_str()
                                                     : playing.path.c_str() + slash + 1,
                          playing.time, playing.weight);
            }
          }
          std::printf("\n");
        }

        if (!stages.empty() && bodies_.count(id) == 0) {
          drawn.pose = mesh::poseSkeleton(look->skeleton, stages);
        }
        // Dead: his body, not his animation.
        if (const auto lying = poseBody(id, object, drawn, place, frameStep)) place = *lying;
        {
          DrawStat& stat = stats_[id];
          if (stages.empty() && bodies_.count(id) == 0) {
            ++stat.bindPose;
            const net::bf2::RemotePlayer* owner = remote_->world.ownerOf(id);
            if (owner != nullptr && owner->object != id) ++stat.bindPoseInVehicle;
          }
          if (remote_->terrain != nullptr && bodies_.count(id) == 0 && !seated) {
            const float above = base.y - remote_->terrain->groundContactAt(base).height;
            stat.highestAboveGround = std::max(stat.highestAboveGround, above);
          }
        }
        if (look->gpu.vertices != nullptr) {
          gfx::MeshRenderer::DrawItem item{&look->gpu, place};
          // With no clip to stand on he is drawn as the file holds him, which is
          // the bind pose — the same as before any animation system was read.
          if (!drawn.pose.empty()) item.pose = &drawn.pose;
          out.push_back(item);
          continue;
        }
      }
    }
    out.push_back(gfx::MeshRenderer::DrawItem{&boxes_[which], place});
  }
}

std::uint16_t WorldView::watchSoldier(const Vec3f& from) {
  // The camera holds on to **one** soldier. Taking the nearest every frame made it
  // jump from one to another as they passed each other, which reads as the watched
  // soldier teleporting.
  const auto& objects = remote_->world.objects();
  const auto stillThere = [&](std::uint16_t id) {
    const auto found = objects.find(id);
    return found != objects.end() && found->second.netClass == net::bf2::GhostClass::Soldier &&
           id != remote_->ourSoldier;
  };
  if (watched_ != 0 && !stillThere(watched_)) watched_ = 0;
  if (options_.watchPose >= 0 || options_.watchWeapon >= 0 || options_.watchRagdoll ||
      options_.watchSeated) {
    // `--watch-pose`, `--watch-weapon`, `--watch-ragdoll`, `--watch-seated`: keep
    // to a soldier in that pose, with that item out, lying as a ragdoll or in an
    // open seat — the nearest there is.
    const auto inPose = [&](std::uint16_t id) {
      const net::bf2::RemoteObject& object = objects.at(id);
      return (options_.watchPose < 0 || static_cast<int>(object.lastPose) == options_.watchPose) &&
             (options_.watchWeapon < 0 || object.lastWeaponIndex == options_.watchWeapon) &&
             (!options_.watchRagdoll || !object.ragdollParticles.empty()) &&
             (!options_.watchSeated || seatedAt(id).has_value());
    };
    if (watched_ != 0 && inPose(watched_)) return watched_;
    float nearestInPose = 1e9f;
    std::uint16_t chosen = 0;
    for (const auto& [id, object] : objects) {
      if (!stillThere(id) || !inPose(id)) continue;
      const float away = length(object.position - from);
      if (away < nearestInPose) {
        nearestInPose = away;
        chosen = id;
      }
    }
    if (chosen != 0) {
      if (chosen != watched_) {
        std::printf("  watching soldier %u (pose %d, weapon %d)\n", chosen, options_.watchPose,
                    options_.watchWeapon);
      }
      watched_ = chosen;
      return watched_;
    }
  }
  if (watched_ != 0) return watched_;
  float nearest = 1e9f;
  for (const auto& [id, object] : objects) {
    if (!stillThere(id)) continue;
    const auto pose = remote_->world.poseOf(id, remote_->tick.pending / server::kTickTime);
    const float away = length((pose ? pose->position : object.position) - from);
    if (away < nearest) {
      nearest = away;
      watched_ = id;
    }
  }
  if (watched_ != 0) {
    std::printf("  watching soldier %u\n", watched_);
    remote_->world.setTraceObject(watched_);
  }
  return watched_;
}

void WorldView::report() const {
  for (const auto& [id, stat] : stats_) {
    if (stat.largestStep < 0.01f) continue;  // standing still: nothing to measure
    std::printf("  drawn object %u: interpolated %d, extrapolated %d, newest %d frames, "
                "largest step between frames %.2f m, under the terrain: predicted %d, drawn %d\n",
                id, stat.frames[2], stat.frames[1], stat.frames[0], stat.largestStep,
                stat.predictedUnder, stat.drawnUnder);
  }
  for (const auto& [id, stat] : stats_) {
    if (stat.framesInVehicle == 0) continue;
    std::printf("  soldier %u in vehicle %u: %d frames — seated %d, in a closed seat %d, seat "
                "not found and not drawn %d (his own records meanwhile %d)\n",
                id, stat.vehicle, stat.framesInVehicle, stat.framesSeated, stat.framesInClosedSeat,
                stat.framesHiddenInVehicle, stat.recordsInVehicle);
  }
  for (const auto& [id, stat] : stats_) {
    if (stat.bindPose == 0 && stat.highestAboveGround < 0.5f) continue;
    const net::bf2::RemoteObject* object = nullptr;
    if (const auto found = remote_->world.objects().find(id); found != remote_->world.objects().end()) {
      object = &found->second;
    }
    const net::bf2::RemotePlayer* owner = remote_->world.ownerOf(id);
    std::printf("  soldier %u: bind pose %d frames (%d with his player in object %u), highest "
                "%.2f m over the terrain, pose %u, ragdoll particles %zu\n",
                id, stat.bindPose, stat.bindPoseInVehicle, owner ? owner->object : 0,
                stat.highestAboveGround, object ? object->lastPose : 0u,
                object ? object->ragdollParticles.size() : 0);
  }
  if (bodyFrames_ > 0) {
    std::printf("  other soldiers' round messages sent to their weapons: %d\n", firedAnimations_);
    std::printf("  ragdoll bodies: %d frames drawn, %d with a particle through a face\n",
                bodyFrames_, bodyFramesThroughFace_);
  }
  for (const auto& [id, body] : bodies_) {
    std::printf("  body %u: %d frames, %d with the centre over 0.6 m (%d over the surface "
                "below it), %d standing, highest %.2f m (%.2f over the surface), client mode "
                "%s, fed %d states, last at %.2f %.2f %.2f\n",
                id, body.frames, body.framesHigh, body.framesOverSurface, body.framesUpright,
                body.highest, body.highestOverSurface, body.ragdoll.client() ? "yes" : "no",
                body.fed, body.last.x, body.last.y, body.last.z);
  }
}

void WorldView::release(gfx::MeshRenderer& renderer) {
  for (auto& mesh : vehicleMeshes_) renderer.release(mesh);
  vehicleMeshes_.clear();
  vehicleParts_.clear();
  vehicleMotion_.clear();
  vehicleByName_.clear();
  localTurretSettings_.clear();
  seatsByVehicle_.clear();
}

}  // namespace obf2::app
