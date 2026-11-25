#include "obf2/app/bullet_decals.h"
#include "obf2/app/decal_projection.h"

#include <algorithm>
#include <limits>

#include "obf2/core/path.h"
#include "obf2/mesh/primitives.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

const BulletDecals::MaterialPair* BulletDecals::materialPair(
    FileSystem& files, int projectileMaterial, std::uint16_t surfaceMaterial) {
  if (!loaded_) {
    loaded_ = true;
    // Use the primary slot for the initial contact. Other slots can describe
    // ricochet or exit effects and need information about the projectile path.
    MaterialPair* current = nullptr;
    con::Interpreter interpreter(files, [&](const con::Command& command) {
      if (command.lowerPath == "materialmanager.createcell") {
        const int projectile = command.argInt(0).value_or(-1);
        const int surface = command.argInt(1).value_or(-1);
        current = nullptr;
        if (projectile >= 0 && surface >= 0) {
          current = &materials_.insert_or_assign({projectile, surface}, MaterialPair{}).first->second;
        }
      } else if (current && command.argInt(0) == 0) {
        if (command.lowerPath == "materialmanager.setdecaltemplate") current->decal = command.argStr(1);
        if (command.lowerPath == "materialmanager.seteffecttemplate") current->effect = command.argStr(1);
        if (command.lowerPath == "materialmanager.setsoundtemplate") current->sound = command.argStr(1);
      }
    });
    interpreter.runFile("common/material/materialManagerSettings.con");
  }
  const auto material = materials_.find({projectileMaterial, surfaceMaterial});
  return material == materials_.end() ? nullptr : &material->second;
}

std::string_view BulletDecals::impactSound(FileSystem& files, int projectileMaterial,
                                           std::uint16_t surfaceMaterial) {
  const auto* material = materialPair(files, projectileMaterial, surfaceMaterial);
  return material ? std::string_view(material->sound) : std::string_view{};
}

std::string_view BulletDecals::impactEffect(FileSystem& files, int projectileMaterial,
                                            std::uint16_t surfaceMaterial) {
  const auto* material = materialPair(files, projectileMaterial, surfaceMaterial);
  return material ? std::string_view(material->effect) : std::string_view{};
}

void BulletDecals::add(FileSystem& files, const game::Registry& registry,
                       gfx::MeshRenderer& renderer, int projectileMaterial,
                       std::uint16_t surfaceMaterial, Vec3f point, Vec3f normal,
                       const Receiver* receiver) {
  renderer_ = &renderer;
  lastPlacement_.reset();
  const auto* material = materialPair(files, projectileMaterial, surfaceMaterial);
  if (!material || material->decal.empty()) return;

  auto [entry, inserted] = decals_.try_emplace(material->decal);
  Decal& decal = entry->second;
  if (inserted) {
    const auto* definition = registry.find(material->decal);
    if (!definition || definition->className != "Decal") return;
    // Property bindings: decal_placement.h. The renderer constructor's
    // defaults are not read; absent properties fall back to zero/off here.
    auto& values = decal.values;
    values.size = definition->number("size").value_or(0.0f);
    values.randomSize = definition->number("randomSize").value_or(0.0f);
    values.rotation = definition->number("rotation").value_or(0.0f);
    values.randomRotation = definition->number("randomRotation").value_or(0.0f);
    values.animationEnable = definition->number("animationEnable").value_or(0.0f) != 0.0f;
    values.randomizedStartFrame =
        definition->number("animationRandomizedStartFrame").value_or(0.0f) != 0.0f;
    if (values.size <= 0.0f) return;
    std::string texturePath = normalizeAssetPath(definition->text("decalTextureName"));
    if (texturePath.empty()) return;
    if (!texturePath.ends_with(".dds")) texturePath += ".dds";
    const auto bytes = files.read(texturePath);
    const auto texture = bytes ? texture::loadDds(*bytes) : std::nullopt;
    if (!texture) return;
    auto* image = renderer.uploadSharedTexture(*texture);
    if (!image) return;

    // The retail decals, e.g. `Effects/decals/small/decal_s_rock.tweak`,
    // specify their atlas grid and their frame size in the template. Frame size
    // (RendDX9.dll 0x101af9c0): the relative width/height when animationEnable
    // is set, the whole texture (1, 1) when it is not. `animationFrameWidth`/
    // `Height` in pixels (setters 0x101afa40/0x101afae0, pixels / texture size)
    // are not read: no stock small decal uses them. Defaults: not measured.
    const bool atlas = values.animationEnable;
    decal.width = atlas ? definition->number("setAnimationFrameWidthRelative").value_or(1.0f) : 1.0f;
    decal.height = atlas ? definition->number("setAnimationFrameHeightRelative").value_or(1.0f) : 1.0f;
    // Local bound of 64 prebuilt frames: not measured.
    values.frameCountX = static_cast<std::uint32_t>(std::clamp(static_cast<int>(
        definition->number("animationFrameCountX").value_or(1.0f)), 1, 64));
    values.frameCount = static_cast<std::uint32_t>(std::clamp(static_cast<int>(
        definition->number("animationFrameCount").value_or(1.0f)), 1, 64));
    const std::uint32_t frames = atlas ? values.frameCount : 1u;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
      const auto offset = decalFrameOffset(frame, values.frameCountX, decal.width, decal.height);
      const float u = offset.u;
      const float v = offset.v;
      if (u + decal.width > 1.0f || v + decal.height > 1.0f) break;
      auto quad = mesh::buildScreenQuad({});
      for (auto& vertex : quad.vertices) {
        vertex.uv[0] = u + vertex.uv[0] * decal.width;
        vertex.uv[1] = v + vertex.uv[1] * decal.height;
      }
      if (auto mesh = renderer.upload(quad, {})) {
        auto& range = mesh->ranges.front();
        range.texture = image;
        range.alphaBlend = true;
        range.bundledAlpha = true;
        decal.frames.push_back(std::move(*mesh));
      }
    }
  }
  if (decal.frames.empty()) return;

  // Size, rotation and start frame: RendDX9.dll 0x101ae980/0x101ae450,
  // decal_placement.h. Both paths below consume the same draws; Retail draws
  // rotation and size only on the projected path (0x101acdf0 is not read).
  const auto placement = placeDecal(decal.values, random_);
  lastPlacement_ = placement;
  // A frame past the frames that fit the texture: not measured. Skip the mark.
  if (placement.frame >= decal.frames.size()) return;
  const float angle = placement.radians;
  const std::size_t variant = placement.frame;

  // Retail surface projection and clipping: not measured. Place a flat quad.
  const Vec3f up = std::abs(normal.y) > 0.9f ? Vec3f{0, 0, 1} : Vec3f{0, 1, 0};
  const Vec3f tangent = normalize(cross(up, normal));
  const Vec3f bitangent = cross(normal, tangent);
  // Lift the mark off its surface by Retail's projection separation, 0.01 m
  // (RendDX9.dll 0x101adc70..8d, live value; decal_projection.h). The earlier
  // float-epsilon lift (0.15 mm at map coordinates) lost the depth test to the
  // terrain: a grenade's scorch on FuShe gravel never showed
  // (evidence/retail-captures/decal-ab-20261122/grenade-scorch-retail2-mac19.png).
  point = point + normal * 0.01f;
  Mat4 plane = translation(point);
  const Vec3f x = tangent * placement.halfExtent;
  const Vec3f y = bitangent * placement.halfExtent;
  plane.m[0] = x.x;
  plane.m[1] = x.y;
  plane.m[2] = x.z;
  plane.m[4] = y.x;
  plane.m[5] = y.y;
  plane.m[6] = y.z;
  plane.m[8] = normal.x;
  plane.m[9] = normal.y;
  plane.m[10] = normal.z;
  // This global local limit remains a fallback. Retail provisions 32/64 per
  // receiver (RendDX9.dll 0x101acc80) and recycles the oldest when
  // count + 1 >= limit (0x101ae9c4), so 31/63 stay live. Which receivers get
  // the 64 (null owner) is not measured, so the per-receiver ring is not ported.
  constexpr std::size_t kMaxMarks = 128;
  if (marks_.size() == kMaxMarks) { releaseMark(marks_.front()); marks_.pop_front(); }
  if (receiver && receiver->geometry) {
    const auto projected = projectStaticDecal(*receiver->geometry, receiver->transform,
        receiver->origin, receiver->ray, placement.halfExtent, angle,
        decalFrameOffset(placement.frame, decal.values.frameCountX, decal.width, decal.height).u,
        decalFrameOffset(placement.frame, decal.values.frameCountX, decal.width, decal.height).v,
        decal.width, decal.height);
    if (projected) {
      // A bound receiver that clips to no geometry must not fall back to an
      // unclipped proxy quad (RendDX9.dll0x101ae450 zero-output branch).
      if (projected->indices.empty()) return;
      if (auto uploaded = renderer.upload(*projected, {})) {
        auto owned = std::make_unique<gfx::GpuMesh>(std::move(*uploaded));
        owned->ranges.front().texture = decal.frames[variant].ranges.front().texture;
        owned->ranges.front().alphaBlend = true;
        owned->ranges.front().bundledAlpha = true;
        auto item = gfx::MeshRenderer::DrawItem(owned.get(), Mat4::identity());
        marks_.push_back({item, {}, std::move(owned)});
        ++count_;
        return;
      }
    }
  }
  marks_.push_back({gfx::MeshRenderer::DrawItem(&decal.frames[variant],
                   plane * rotationZ(angle)), {}, {}});
  ++count_;
}

void BulletDecals::advance(float step, int round) {
  if (round_ != round) {
    for (auto& mark : marks_) releaseMark(mark);
    marks_.clear();
    round_ = round;
  }
  for (auto& mark : marks_) mark.lifetime.advance(step);
  std::erase_if(marks_, [this](Mark& mark) {
    if (!mark.lifetime.expired()) return false;
    releaseMark(mark);
    return true;
  });
}

void BulletDecals::releaseMark(Mark& mark) {
  if (mark.projected && renderer_) renderer_->release(*mark.projected);
}

void BulletDecals::draw(std::vector<gfx::MeshRenderer::DrawItem>& items) const {
  for (const auto& mark : marks_) {
    auto& item = items.emplace_back(mark.item);
    item.particleOpacity = mark.lifetime.opacity();
  }
}

void BulletDecals::release(gfx::MeshRenderer& renderer) {
  for (auto& mark : marks_) releaseMark(mark);
  marks_.clear();
  for (auto& [name, decal] : decals_) {
    for (auto& frame : decal.frames) renderer.release(frame);
  }
  decals_.clear();
}

}  // namespace obf2::app
