#include "obf2/app/first_person.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

bool FirstPersonView::setWeapon(const std::string& soldier, const std::string& kit, int item) {
  if (files_ == nullptr || item < 0 || soldier.empty()) {
    model_ = nullptr;
    key_.clear();
    return false;
  }
  const std::string key = modelKey(soldier, kit, item);
  if (key == key_) return ready();

  key_ = key;
  body_ = anim::Player{};
  weapon_ = anim::Player{};
  // A new weapon is a new soldier or a switch; the spring's state is the
  // soldier's, but there is nothing to carry over to an empty template either.
  spring_.reset();
  const auto length = [this](const std::string& path) {
    const mesh::BoneAnimation* clip = clipAt(path);
    return clip != nullptr ? clip->duration() : 0.0f;
  };
  body_.setLengthOf(length);
  weapon_.setLengthOf(length);

  if (const auto found = models_.find(key); found != models_.end()) {
    model_ = &found->second;
    return model_->ready;
  }

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
    std::printf("  first person %s: %zu vertices, body system %s, weapon system %s, %s\n",
                key.c_str(), bind->vertices.size(), model.body ? "yes" : "no",
                model.weapon ? "yes" : "no", model.ready ? "on the gpu" : "not uploaded");
  } else {
    std::printf("  first person %s: not assembled (model %d, mesh %d, skeleton %d)\n", key.c_str(),
                soldierModel ? 1 : 0, bind ? 1 : 0, skeleton ? 1 : 0);
  }
  model_ = &models_.emplace(key, std::move(model)).first->second;
  return model_->ready;
}

void FirstPersonView::update(const anim::State& state, float step,
                             const anim::CameraSpring::Input& spring) {
  if (!ready()) return;
  if (model_->body) body_.update(*model_->body, state, step);
  if (model_->weapon) weapon_.update(*model_->weapon, state, step);

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
      stages.push_back(
          mesh::PoseStage{clip, playing.time * mesh::kAnimationFramesPerSecond, playing.weight});
    }
  }
  pose_ = stages.empty() ? mesh::poseSkeleton(model_->skeleton, nullptr, 0)
                         : mesh::poseSkeleton(model_->skeleton, stages);
}

void FirstPersonView::collect(std::vector<gfx::MeshRenderer::DrawItem>& out, const Mat4& camera) {
  if (!ready() || pose_.empty()) return;
  const Vec3f sway = springTemplate_.use ? spring_.offset(springTemplate_) : Vec3f{};
  gfx::MeshRenderer::DrawItem item{&model_->gpu, firstPersonRoot(model_->skeleton, camera, sway)};
  item.pose = &pose_;
  out.push_back(item);
}

void FirstPersonView::release(gfx::MeshRenderer& renderer) {
  for (auto& [key, model] : models_) {
    if (model.gpu.vertices != nullptr) renderer.release(model.gpu);
  }
  models_.clear();
  model_ = nullptr;
  key_.clear();
}

}  // namespace obf2::app
