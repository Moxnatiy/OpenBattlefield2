#include "obf2/app/world_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "obf2/core/path.h"
#include "obf2/game/object_mesh.h"
#include "obf2/game/soldier_model.h"
#include "obf2/level/gameplay.h"
#include "obf2/mesh/primitives.h"
#include "obf2/server/soldier_move.h"

namespace obf2::app {

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

WorldView::Look* WorldView::lookFor(const std::string& soldierName, const std::string& kitName) {
  const std::string key = soldierName + "|" + kitName;
  if (const auto found = looks_.find(key); found != looks_.end()) {
    return found->second.ready ? &found->second : nullptr;
  }
  Look look;
  const auto model = game::soldierModel(*registry_, soldierName, kitName);
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
  // that way it is the same mesh and the same shader as the body.
  if (bind && model->weapon) {
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
    look.bind = std::move(*bind);
    look.skeleton = std::move(*skeleton);
    look.legs = anim::System::load(*files_, normalizeAssetPath(model->animationSystem3p));
    if (!model->weaponAnimationSystem3p.empty()) {
      if (auto weapon =
              anim::System::load(*files_, normalizeAssetPath(model->weaponAnimationSystem3p))) {
        look.weapon = std::move(*weapon);
        look.hasWeapon = true;
      }
    }
    if (const auto uploaded = renderer_->upload(look.bind, resolve_)) look.gpu = *uploaded;
    look.ready = look.gpu.vertices != nullptr;
    std::printf("  soldier look %s: %zu vertices, kit %s, legs %s, weapon %s, on the gpu %s\n",
                key.c_str(), look.bind.vertices.size(), model->kit ? "yes" : "no",
                look.legs ? "yes" : "no", look.hasWeapon ? "yes" : "no",
                look.gpu.skin != nullptr ? "skinned" : "unskinned");
  } else {
    std::printf("  soldier look %s: not assembled (model %d, mesh %d, skeleton %d)\n", key.c_str(),
                model ? 1 : 0, bind ? 1 : 0, skeleton ? 1 : 0);
  }
  auto& stored = looks_.emplace(key, std::move(look)).first->second;
  return stored.ready ? &stored : nullptr;
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

const gfx::GpuMesh* WorldView::vehicleMesh(std::uint16_t id,
                                           const net::bf2::RemoteObject& object) {
  // What it is, by the spot it was **created** on — a jeep that drove off stays a
  // jeep. The template number is stable per template, so a match is remembered by
  // number too, for objects first seen away from their spawner.
  auto resolved = resolved_.find(id);
  if (resolved == resolved_.end()) {
    level::PlacedAt placed = placement_->at(object.createdAt);
    if (placed.kind == level::PlacedAt::Kind::Unknown) {
      // Not on a placement: the template number names it
      // (obf2/game/template_numbers.h).
      if (const std::string* name = remote_->templateNumbers.nameOf(object.templateId);
          name != nullptr && object.templateId != 0) {
        placed.kind = level::PlacedAt::Kind::Spawned;
        placed.templateName = *name;
        placed.hasRotation = false;
      }
    }
    resolved = resolved_.insert_or_assign(id, Resolved{object.createdAt, placed}).first;
  }
  const level::PlacedAt& placed = resolved->second.placed;
  if (placed.kind != level::PlacedAt::Kind::Spawned) return nullptr;
  auto found = vehicleByName_.find(placed.templateName);
  if (found == vehicleByName_.end()) {
    gfx::GpuMesh* uploaded = nullptr;
    if (auto built = game::buildObjectMesh(*files_, *registry_, placed.templateName,
                                           options_.geometryIndex, options_.lodIndex, false)) {
      if (auto gpu = renderer_->upload(*built, resolve_)) {
        vehicleMeshes_.push_back(*gpu);
        uploaded = &vehicleMeshes_.back();
      }
    }
    std::printf("  server object %u is %s%s\n", id, placed.templateName.c_str(),
                uploaded != nullptr ? "" : " (no geometry)");
    found = vehicleByName_.emplace(placed.templateName, uploaded).first;
  }
  return found->second;
}

void WorldView::collect(std::vector<gfx::MeshRenderer::DrawItem>& out, int frame,
                        float frameStep) {
  if (!boxesReady_ || remote_ == nullptr) return;
  const bool tracing = frame >= options_.traceFrom && frame < options_.traceFrom + options_.traceFrames;

  for (const auto& [id, object] : remote_->world.objects()) {
    // We do not draw ourselves from inside.
    if (id == remote_->ourSoldier && !options_.showOwnBox) continue;
    // By class, not by team: a jeep a player entered has a team and is still a
    // jeep, and a soldier whose `EnterVehicleEvent` came before we joined has none
    // and is still a soldier.
    const bool soldier = object.netClass == net::bf2::GhostClass::Soldier;
    // Drawn where its track puts it now (ghost_track.h), not where the last update
    // left it.
    const auto pose = remote_->world.poseOf(id, remote_->tick.pending / server::kTickTime);
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

    if (!soldier && placement_) {
      const auto found = resolved_.find(id);
      const bool knownStatic = found != resolved_.end() &&
                               found->second.placed.kind == level::PlacedAt::Kind::Static;
      // A static placement is skipped: the level draws it already.
      if (knownStatic) continue;
      if (const gfx::GpuMesh* vehicle = vehicleMesh(id, object)) {
        // The rotation is the spawner's: a simple object's own rotation travels in
        // its record and is not read yet.
        const level::PlacedAt& placed = resolved_[id].placed;
        Mat4 place = translation(drawAt);
        if (placed.hasRotation) {
          place = place * rotationYawPitchRoll(placed.rotation.x, placed.rotation.y,
                                               placed.rotation.z);
        }
        out.push_back(gfx::MeshRenderer::DrawItem{vehicle, place});
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
      const std::string* soldierName = remote_->templateNumbers.nameOf(object.templateId);
      const std::uint32_t kit = remote_->world.kitTemplateOf(id);
      const std::string* kitName = kit != 0 ? remote_->templateNumbers.nameOf(kit) : nullptr;
      Look* look = soldierName != nullptr
                       ? lookFor(*soldierName, kitName != nullptr ? *kitName : std::string())
                       : nullptr;
      if (look != nullptr) {
        const std::string key = *soldierName + "|" + (kitName != nullptr ? *kitName : "");
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
        }

        // The state the conditions read: the speed and the direction the server
        // itself reports for this soldier, turned into his own frame. The yaw a
        // ghost carries is the body's plus the aim's, which is the matrix the
        // movement is built from (docs/functions/soldier-physics.md).
        anim::State animState;
        const auto* newest = object.track.newest();
        const Vec3f velocity = newest != nullptr && newest->velocity ? *newest->velocity : Vec3f{};
        const float planar = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        animState.speed = planar;
        if (planar > 0.001f) {
          const float yawRadians = pose->bodyYaw * 3.14159265358979323846f / 180.0f;
          const float forward =
              (velocity.x * std::sin(yawRadians) + velocity.z * std::cos(yawRadians)) / planar;
          const float side =
              (velocity.x * std::cos(yawRadians) - velocity.z * std::sin(yawRadians)) / planar;
          animState.direction[0] = side;
          animState.direction[1] = 0.0f;
          animState.direction[2] = forward;
        }
        if (look->legs) drawn.legs.update(*look->legs, animState, frameStep);
        if (look->hasWeapon) drawn.weapon.update(look->weapon, animState, frameStep);

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
        addStages(drawn.legs);
        addStages(drawn.weapon);
        if (tracing) {
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

        if (!stages.empty()) drawn.pose = mesh::poseSkeleton(look->skeleton, stages);
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
}

void WorldView::release(gfx::MeshRenderer& renderer) {
  for (auto& mesh : vehicleMeshes_) renderer.release(mesh);
  vehicleMeshes_.clear();
  vehicleByName_.clear();
}

}  // namespace obf2::app
