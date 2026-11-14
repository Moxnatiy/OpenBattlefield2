#include "obf2/app/first_person.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "obf2/core/path.h"
#include "obf2/game/object_mesh.h"
#include "obf2/game/soldier_model.h"
#include "obf2/mesh/skinning.h"

namespace obf2::app {
namespace {

constexpr float kToRadians = 3.14159265358979323846f / 180.0f;

// The inverse of a rotation and a translation: the rotation transposed, and the
// translation turned back by it.
Mat4 rigidInverse(const Mat4& m) {
  Mat4 out = Mat4::identity();
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) out.m[column * 4 + row] = m.m[row * 4 + column];
  }
  for (int row = 0; row < 3; ++row) {
    out.m[12 + row] = -(out.m[0 * 4 + row] * m.m[12] + out.m[1 * 4 + row] * m.m[13] +
                        out.m[2 * 4 + row] * m.m[14]);
  }
  return out;
}

std::string modelKey(const std::string& soldier, const std::string& kit, int item) {
  return soldier + "|" + kit + "|" + std::to_string(item);
}

}  // namespace

Vec3f soldierEye(const Vec3f& feet, float pivotHeight, float bodyYawDegrees,
                 const Vec3f& poseCamera) {
  const Vec3f pivot{feet.x, feet.y + pivotHeight, feet.z};
  return transformPoint(translation(pivot) * rotationY(bodyYawDegrees * kToRadians), poseCamera);
}

Mat4 firstPersonCamera(const Vec3f& eye, float yawDegrees, float pitchDegrees) {
  // rotationX turns +Z downwards for a positive angle, and our pitch is positive
  // upwards (lookDirection), hence the minus.
  return translation(eye) * rotationY(yawDegrees * kToRadians) *
         rotationX(-pitchDegrees * kToRadians);
}

Mat4 firstPersonRoot(const mesh::Skeleton& skeleton, const Mat4& camera, const Vec3f& sway) {
  const Mat4 swayed = camera * translation(sway);
  if (skeleton.bones.empty()) return swayed;
  const std::vector<mesh::Mat4> rest = mesh::poseSkeleton(skeleton, nullptr, 0);
  Mat4 first;
  std::copy(std::begin(rest[0].m), std::end(rest[0].m), std::begin(first.m));
  return swayed * rigidInverse(first);
}

Mat4 firstPersonProjection(float aspect) {
  return perspective(kFirstPersonFov, aspect, kFirstPersonNear, kFirstPersonFar);
}

void FirstPersonView::init(FileSystem& files, const game::Registry& registry,
                           gfx::MeshRenderer& renderer, TextureResolver resolve) {
  files_ = &files;
  registry_ = &registry;
  renderer_ = &renderer;
  resolve_ = std::move(resolve);
}

const mesh::BoneAnimation* FirstPersonView::clipAt(const std::string& path) {
  const std::string key = normalizeAssetPath(path);
  auto found = clips_.find(key);
  if (found == clips_.end()) {
    std::optional<mesh::BoneAnimation> clip;
    if (const auto bytes = files_->read(key)) clip = mesh::loadBoneAnimation(*bytes);
    found = clips_.emplace(key, std::move(clip)).first;
  }
  return found->second ? &*found->second : nullptr;
}

float FirstPersonView::playLength(const std::string& path) const {
  if (model_ == nullptr) return 0.0f;
  for (const auto* system : {&model_->weapon, &model_->body}) {
    if (*system) {
      if (const float set = (*system)->lengthOf(path); set > 0.0f) return set;
    }
  }
  return 0.0f;
}

bool FirstPersonView::setWeapon(const std::string& soldier, const std::string& kit, int item) {
  if (files_ == nullptr || item < 0 || soldier.empty()) {
    model_ = nullptr;
    key_.clear();
    return false;
  }
  const std::string key = modelKey(soldier, kit, item);
  if (key == key_) return ready();

  // The weapon put away keeps its magazines: each weapon of the kit is an object of
  // its own with its own `AmmoComp` (`ObjectTemplate` per item), so coming back to
  // it finds what it had.
  if (!key_.empty()) ammoByKey_[key_] = ammo_;
  key_ = key;
  body_ = anim::Player{};
  weapon_ = anim::Player{};
  // A new weapon is a new soldier or a switch; the spring's state is the
  // soldier's, but there is nothing to carry over to an empty template either.
  spring_.reset();
  // A new weapon is a new fire component: its own rates, from the first.
  onceMessages_ = 0;
  heldMessages_ = 0;
  fire_.setWeapon(game::FireTemplate{});
  ammo_.setWeapon(game::AmmoTemplate{});
  zoom_.setWeapon(std::nullopt);
  shift_.setWeapon(game::ShiftTemplate{});
  altHeldBefore_ = false;
  // A weapon taken out is enabled (`GenericFireArm::enableWeapon`, Linux 0x5e6640)
  // and may be used `delayToUse` ticks later: +0x310 = tick + the template's +0x228
  // (seconds × 30, truncated). The tick is the next action's.
  useDelayTicks_ = 0;
  awaitUse_ = true;
  if (const auto chosen = game::soldierModel(*registry_, soldier, kit, item);
      chosen && chosen->weapon1p) {
    if (const game::ObjectTemplate* weapon = registry_->find(chosen->weapon1p->templateName)) {
      fire_.setWeapon(game::fireOf(*weapon));
      ammo_.setWeapon(game::ammoOf(*weapon));
      if (const auto kept = ammoByKey_.find(key); kept != ammoByKey_.end()) ammo_ = kept->second;
      zoom_.setWeapon(game::zoomOf(*weapon));
      shift_.setWeapon(game::shiftOf(*weapon));
      // `ObjectTemplate.delayToUse`.
      if (const auto delay = weapon->number("delaytouse")) {
        useDelayTicks_ = static_cast<int>(*delay * 30.0f);
      }
    }
  }
  const auto length = [this](const std::string& path) {
    if (const float set = playLength(path); set > 0.0f) return set;
    const mesh::BoneAnimation* clip = clipAt(path);
    return clip != nullptr ? clip->duration() : 0.0f;
  };
  body_.setLengthOf(length);
  weapon_.setLengthOf(length);

  model_ = &buildModel(key, soldier, kit, item);
  return model_->ready;
}

bool FirstPersonView::prepare(const std::string& soldier, const std::string& kit, int item) {
  if (files_ == nullptr || item < 0 || soldier.empty()) return false;
  const std::string key = modelKey(soldier, kit, item);
  if (models_.count(key) != 0) return false;
  buildModel(key, soldier, kit, item);
  return true;
}

FirstPersonView::Model& FirstPersonView::buildModel(const std::string& key,
                                                    const std::string& soldier,
                                                    const std::string& kit, int item) {
  if (const auto found = models_.find(key); found != models_.end()) return found->second;

  Model model;
  const auto soldierModel = game::soldierModel(*registry_, soldier, kit, item);
  const auto loadPart = [&](const game::SoldierPart& part) {
    const std::string path =
        game::resolveGeometryPath(*files_, part.templateFile, part.geometryName);
    return path.empty() ? std::nullopt : game::loadMesh(*files_, path, part.geometry, 0, false);
  };
  std::optional<mesh::RenderMesh> bind;
  std::optional<mesh::Skeleton> skeleton;
  if (soldierModel && soldierModel->weapon1p && !soldierModel->skeleton1p.empty()) {
    bind = loadPart(soldierModel->body1p);
    // The weapon's parts on `mesh1` and up of the first-person skeleton
    // (`kFirstWeaponBone1p`), sixteen at most.
    if (bind) {
      if (auto weapon = loadPart(*soldierModel->weapon1p)) {
        mesh::bindPartsToBones(*weapon, mesh::kFirstWeaponBone1p, mesh::kMaxWeaponParts1p);
        mesh::appendSkinned(*bind, *weapon);
      }
    }
    if (const auto bytes = files_->read(normalizeAssetPath(soldierModel->skeleton1p))) {
      skeleton = mesh::loadSkeleton(*bytes);
    }
  }
  if (bind && skeleton) {
    model.skeleton = std::move(*skeleton);
    if (!soldierModel->animationSystem1p.empty()) {
      model.body = anim::System::load(*files_, normalizeAssetPath(soldierModel->animationSystem1p));
    }
    if (!soldierModel->weaponAnimationSystem1p.empty()) {
      model.weapon =
          anim::System::load(*files_, normalizeAssetPath(soldierModel->weaponAnimationSystem1p));
    }
    if (const auto uploaded = renderer_->upload(*bind, resolve_)) model.gpu = *uploaded;
    model.ready = model.gpu.vertices != nullptr;
    // The weapon's zoom lod: the arms again with the weapon's mesh at that level.
    if (const game::ObjectTemplate* weaponTemplate =
            registry_->find(soldierModel->weapon1p->templateName)) {
      if (const auto zoom = game::zoomOf(*weaponTemplate); zoom && zoom->zoomLod > 0) {
        game::SoldierPart zoomPart = *soldierModel->weapon1p;
        const std::string path =
            game::resolveGeometryPath(*files_, zoomPart.templateFile, zoomPart.geometryName);
        auto arms = loadPart(soldierModel->body1p);
        auto weapon = path.empty() ? std::nullopt
                                   : game::loadMesh(*files_, path, zoomPart.geometry,
                                                    zoom->zoomLod, false);
        if (arms && weapon) {
          mesh::bindPartsToBones(*weapon, mesh::kFirstWeaponBone1p, mesh::kMaxWeaponParts1p);
          mesh::appendSkinned(*arms, *weapon);
          if (const auto uploaded = renderer_->upload(*arms, resolve_)) {
            model.gpuZoom = *uploaded;
            model.zoomLod = zoom->zoomLod;
          }
        }
      }
    }
    std::printf("  first person %s: %zu vertices, body system %s, weapon system %s, %s\n",
                key.c_str(), bind->vertices.size(), model.body ? "yes" : "no",
                model.weapon ? "yes" : "no", model.ready ? "on the gpu" : "not uploaded");
  } else {
    std::printf("  first person %s: not assembled (model %d, mesh %d, skeleton %d)\n", key.c_str(),
                soldierModel ? 1 : 0, bind ? 1 : 0, skeleton ? 1 : 0);
  }
  return models_.emplace(key, std::move(model)).first->second;
}

bool FirstPersonView::onAction(std::uint32_t tick, bool fireHeld, bool reloadPressed,
                               bool altHeld) {
  if (!ready()) return false;
  lastTick_ = tick;
  if (awaitUse_) {
    useAt_ = tick + static_cast<std::uint32_t>(useDelayTicks_);
    awaitUse_ = false;
  }
  const int stateBefore = ammo_.state();
  if (reloadPressed) ammo_.requestReload(tick);
  // The reload's message 2 (`DefaultAnimationComp::reload`, Linux 0x60aad0): once
  // for state 3, held for 4, let go for 0 and 6.
  switch (ammo_.update(tick)) {
    case game::ReloadMessage::Once:
      onceMessages_ |= 2u;
      ++reloadsStarted_;
      break;
    case game::ReloadMessage::Hold: heldMessages_ |= 2u; break;
    case game::ReloadMessage::Clear: heldMessages_ &= ~2u; break;
    case game::ReloadMessage::None: break;
  }
  // `GenericFireArm::handleUpdate` runs the ammunition's update and its signals
  // first (0x5e7a24..0x5e7b44) and the zoom's after (0x5e7ba3..0x5e7be1): a reload
  // that has begun tells the zoom component, which goes back to level 0
  // (`DefaultZoomComp::reload`, 0x629b70).
  if (stateBefore < 2 && ammo_.state() >= 2) zoom_.onReload(tick);
  // The zoom key's press (`TemplateMessage` 7 → `toggle`), then the component's tick.
  if (altHeld && !altHeldBefore_) zoom_.press(tick);
  altHeldBefore_ = altHeld;
  if (zoom_.update(tick)) ++zoomChanges_;
  // Message 4, held while zoomed and let go at level 0 (`AnimationComp::
  // updateOnZooming`, Linux 0x609e80, on by default — `animation.animateOnZoom`'s
  // constructor value 1, 0x609f7f), handed the level by `GenericFireArm::handleUpdate`
  // on every update (0x5e7be1..0x5e7c1a).
  if (zoom_.zoomed()) {
    heldMessages_ |= 4u;
  } else {
    heldMessages_ &= ~4u;
  }
  // The bolt-action cycle (`DefaultAnimationComp::updateComponent`): once the fire
  // component is out of its cooldown a shift asked for starts, message 32 once.
  if (shift_.update(tick, fire_.busy(tick))) onceMessages_ |= 32u;
  // A held button the weapon cannot answer is still a held button for the
  // trigger's "held from before" (`FireComp::requestFire`, Linux 0x5f5158). The
  // components that may refuse a round: the ammunition and the animation's shift
  // (`GenericFireArm::requestFire`, 0x5e4370, asks each; 0x60a710).
  // (And not before the weapon's `delayToUse` is over: `busyExcept` 0x5e35f8.)
  if (!fire_.tick(tick, fireHeld && ammo_.canFire() && shift_.canFire() && tick >= useAt_)) {
    return false;
  }
  ammo_.onRound(tick);
  shift_.onRoundComplete(ammo_.roundsInMag());
  zoom_.onRound();  // `fireComplete`: zoom out after a round, with `zoomOutAfterFire`
  onceMessages_ |= 1u;  // a round: `postFire` (Linux 0x60a7e9)
  return true;
}

void FirstPersonView::update(const anim::State& state, float step,
                             const anim::CameraSpring::Input& spring) {
  if (!ready()) return;
  // The walk sees the held mask and this update's once-mask together; the
  // once-mask is gone after it (`AnimationSystem::update`, 0x7f7390: +0x8 |= +0x10
  // before the walk, +0x8 &= ~+0x10 and +0x10 = 0 after).
  anim::State walked = state;
  walked.messages |= onceMessages_ | heldMessages_;
  // +0xc, the previous walk's held mask: `previous = held` after the once-mask
  // is taken off (0x7f7390). A switch trigger plays its bundle only when its bit
  // differs from it; without it the zoom's transition started on every frame the
  // zoom was held.
  walked.previousMessages = previousHeld_;
  previousHeld_ = state.messages | heldMessages_;
  onceMessages_ = 0;
  if (model_->body) body_.update(*model_->body, walked, step);
  // The idle time the `IdleTrigger` reads: it grows while exactly one player is
  // alive in the system — only the base pose — and is zero with two or more
  // (`AnimationSystem::update`, 0x7f7390; docs/functions/animation-system.md).
  // Without it the weapon's `idle1`/`idle2` never played.
  weaponIdle_ = weapon_.playing().size() == 1 ? weaponIdle_ + step : 0.0f;
  walked.idleTime = weaponIdle_;
  if (model_->weapon) weapon_.update(*model_->weapon, walked, step);
  // The measure for every bundle: the frames it played at over half its weight
  // (several at once — a plain trigger's `still` stays on under `run`).
  {
    std::set<std::string> counted;
    for (const anim::PlayingAnimation& playing : weapon_.playing()) {
      if (playing.weight > 0.5f && counted.insert(playing.bundle).second) {
        ++bundleFrames_[playing.bundle];
      }
    }
  }
  // The measure: frames the weapon's `fire` bundle is on.
  for (const anim::PlayingAnimation& playing : weapon_.playing()) {
    if ((playing.bundle == "fire" || playing.bundle == "zoom_fire") && playing.weight > 0.0f) {
      ++fireClipFrames_;
      break;
    }
  }
  for (const anim::PlayingAnimation& playing : weapon_.playing()) {
    if (playing.bundle == "shift" && playing.weight > 0.0f) {
      ++shiftClipFrames_;
      break;
    }
  }
  for (const anim::PlayingAnimation& playing : weapon_.playing()) {
    if (playing.bundle == "reload" && playing.weight > 0.0f) {
      ++reloadClipFrames_;
      break;
    }
  }

  // The soldier's spring, the weapon's merged over it — each only when its
  // system has one (`cameraSpring.use`).
  springTemplate_ = anim::CameraSpringTemplate{};
  if (model_->body && model_->body->cameraSpring().use) {
    springTemplate_ = model_->body->cameraSpring();
  }
  if (model_->weapon && model_->weapon->cameraSpring().use) {
    springTemplate_.merge(model_->weapon->cameraSpring());
    springTemplate_.use = true;
  }
  if (springTemplate_.use) spring_.update(springTemplate_, spring);

  // The soldier's system first and the weapon's over it, as in third person: a
  // clip touches only its own bones.
  std::vector<mesh::PoseStage> stages;
  for (const anim::Player* player : {&body_, &weapon_}) {
    for (const anim::PlayingAnimation& playing : player->playing()) {
      if (playing.weight <= 0.001f) continue;
      const mesh::BoneAnimation* clip = clipAt(playing.path);
      if (clip == nullptr || clip->frameCount == 0) continue;
      // A clip given its own length plays all its frames over it
      // (`anim::System::lengthOf`); otherwise at the file's 24 a second.
      const float set = playLength(playing.path);
      const float frameAt = set > 0.0f
                                ? playing.time / set * static_cast<float>(clip->frameCount)
                                : playing.time * mesh::kAnimationFramesPerSecond;
      stages.push_back(mesh::PoseStage{clip, frameAt, playing.weight});
    }
  }
  pose_ = stages.empty() ? mesh::poseSkeleton(model_->skeleton, nullptr, 0)
                         : mesh::poseSkeleton(model_->skeleton, stages);
}

void FirstPersonView::collect(std::vector<gfx::MeshRenderer::DrawItem>& out, const Mat4& camera) {
  if (!ready() || pose_.empty()) return;
  const Vec3f sway = springTemplate_.use ? spring_.offset(springTemplate_) : Vec3f{};
  // Zoomed in far enough, the weapon is its zoom lod (`DefaultZoomComp::setZoomLod`).
  const bool scoped = model_->gpuZoom.vertices != nullptr && zoom_.lod() > 0;
  gfx::MeshRenderer::DrawItem item{scoped ? &model_->gpuZoom : &model_->gpu,
                                   firstPersonRoot(model_->skeleton, camera, sway)};
  item.pose = &pose_;
  out.push_back(item);
}

void FirstPersonView::release(gfx::MeshRenderer& renderer) {
  for (auto& [key, model] : models_) {
    if (model.gpu.vertices != nullptr) renderer.release(model.gpu);
    if (model.gpuZoom.vertices != nullptr) renderer.release(model.gpuZoom);
  }
  models_.clear();
  model_ = nullptr;
  key_.clear();
}

}  // namespace obf2::app
