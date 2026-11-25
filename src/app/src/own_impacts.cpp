#include "obf2/app/own_impacts.h"

#include "obf2/session/remote_world.h"

namespace obf2::app {
namespace {

// How far the view's line is followed: ours, **not measured** (the original's
// projectile flies until its contact or its `timeToLive`).
constexpr float kReach = 125.0f;
// Impacts waiting for the next frame at most: ours, **not measured**.
constexpr std::size_t kMaxPending = 128;
// The effect stands off its surface by this much, so that its particles do not
// start inside it: ours, **not measured**.
constexpr float kEffectLift = 0.05f;

}  // namespace

void OwnImpacts::shot(const session::RemoteWorld& remote, const game::ObjectTemplate* weapon, Vec3f eye,
                      Vec3f direction) {
  if (!weapon || !remote.collision) return;
  // The round's material: the weapon's `projectileTemplate`, its `material`.
  const auto projectileName = weapon->text("projectileTemplate");
  const auto* projectile = projectileName.empty() ? nullptr : remote.registry.find(projectileName);
  const auto* material = projectile ? projectile->property("material") : nullptr;
  const int projectileMaterial = material ? material->asInt().value_or(-1) : -1;
  if (projectileMaterial < 0) return;

  const Vec3f ray = direction * kReach;
  const auto wall = remote.collision->segmentContact(eye, ray, eye + ray * 0.5f, kReach * 0.5f);
  const auto ground = remote.terrain ? remote.terrain->castSegment(eye, eye + ray) : std::nullopt;
  if (ground && (!wall || length(ground->point - eye) < length(wall->point - eye))) {
    // The terrain's material map is not read (CLAUDE.md, "the soldier in the
    // air"): a round into the ground leaves nothing.
    ++impacts_;
    return;
  }
  if (!wall || !wall->worldMaterial) return;
  if (pending_.size() == kMaxPending) pending_.pop_front();
  pending_.push_back({wall->point, wall->normal, projectileMaterial, *wall->worldMaterial,
                      remote.roundResets});
}

void OwnImpacts::draw(const session::RemoteWorld& remote, Vec3f eye, const Mat4& view, float step,
                      std::vector<gfx::MeshRenderer::DrawItem>& items) {
  if (!files_ || !renderer_) return;
  for (auto& [name, effect] : effects_) {
    effect.listen(eye, view);
    effect.advance(step, remote.roundResets);
  }
  decals_.advance(step, remote.roundResets);
  for (const auto& impact : pending_) {
    if (impact.round != remote.roundResets) continue;
    ++impacts_;
    // The cell's effect (`setEffectTemplate`), when the registry knows it.
    const auto name = decals_.impactEffect(*files_, impact.projectileMaterial, impact.surfaceMaterial);
    if (!name.empty() && remote.registry.find(name)) {
      auto [entry, inserted] = effects_.try_emplace(std::string(name), name);
      if (inserted) {
        entry->second.setVoices(voices_);
        entry->second.advance(0.0f, remote.roundResets);
      }
      entry->second.observe(static_cast<std::uint16_t>(impacts_), impact.point + impact.normal * kEffectLift,
                            eye, view, *files_, remote.registry, *renderer_, true);
    }
    // The cell's mark (`setDecalTemplate`).
    const int before = decals_.count();
    decals_.add(*files_, remote.registry, *renderer_, impact.projectileMaterial, impact.surfaceMaterial,
                impact.point, impact.normal);
    if (impacts_ <= 5) {
      std::printf("  our round hit material %u (round's %d) at %.2f %.2f %.2f: effect '%.*s', %s\n",
                  static_cast<unsigned>(impact.surfaceMaterial), impact.projectileMaterial, impact.point.x,
                  impact.point.y, impact.point.z, static_cast<int>(name.size()), name.data(),
                  decals_.count() > before ? "marked" : "no mark");
    }
  }
  pending_.clear();
  for (auto& [name, effect] : effects_) effect.draw(items, view);
  decals_.draw(items);
}

void OwnImpacts::release(gfx::MeshRenderer& renderer) {
  for (auto& [name, effect] : effects_) effect.release(renderer);
  decals_.release(renderer);
}

}  // namespace obf2::app
