#include "obf2/app/ambient_effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

#include "obf2/core/path.h"
#include "obf2/game/object_mesh.h"
#include "obf2/game/scene.h"
#include "obf2/texture/dds.h"

namespace obf2::app {
namespace {

float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

// RendDX9.dll 0x10175ac0 thresholds: no orientation below this speed
// ([0x1021f0a8]), and the straight up/down basis when both horizontal
// components of the direction are below the second ([0x1021f0e4]).
constexpr float kRestSpeed = 1.1920929e-7f;
constexpr float kVertical = 1e-4f;

}  // namespace

int AmbientEffects::spriteLook(const std::string& name, const game::ObjectTemplate& definition,
                               FileSystem& files, gfx::MeshRenderer& renderer) {
  if (const auto found = lookByTemplate_.find(name); found != lookByTemplate_.end()) return found->second;
  int& index = lookByTemplate_[name];
  index = -1;
  const auto bytes = files.read(normalizeAssetPath(definition.text("textureName")) + ".dds");
  if (!bytes) return index;
  const auto texture = texture::loadDds(*bytes);
  if (!texture) return index;
  auto* gpuTexture = renderer.uploadSharedTexture(*texture);
  if (!gpuTexture) return index;
  const auto t = particles::SpriteTemplate::fromDefinition(definition);
  const bool animated = t.animationEnable && t.frameCount > 0;
  const int count = animated ? t.frameCount : 1;
  const float width = animated ? t.frameWidth : 1.0f;
  const float height = animated ? t.frameHeight : 1.0f;
  Look look;
  for (int cell = 0; cell < count; ++cell) {
    auto uploaded = renderer.upload(
        particles::spriteQuad(static_cast<float>(cell % t.frameCountX) * width,
                              static_cast<float>(cell / t.frameCountX) * height, width, height,
                              t.pivotX, t.pivotY),
        {});
    if (!uploaded) break;
    auto& range = uploaded->ranges.front();
    range.texture = gpuTexture;
    range.alphaBlend = true;
    range.additiveBlend = t.additive;
    look.frames.push_back(std::move(*uploaded));
  }
  if (look.frames.empty()) return index;
  looks_.push_back(std::move(look));
  index = static_cast<int>(looks_.size()) - 1;
  return index;
}

int AmbientEffects::meshLook(const std::string& name, const game::ObjectTemplate& definition,
                             FileSystem& files, const game::Registry& registry,
                             gfx::MeshRenderer& renderer, const gfx::MeshRenderer::TextureResolver& resolve) {
  if (const auto found = lookByTemplate_.find(name); found != lookByTemplate_.end()) return found->second;
  int& index = lookByTemplate_[name];
  index = -1;
  Look look;
  look.mesh = true;
  look.directionalScale = definition.number("directionalScale").value_or(0);
  look.alignRotationToSpeed = definition.number("alignRotationToSpeed").value_or(0) != 0;
  // 0x10174740: template +0x90 == 1 (`blendMode Additive`, BF2.exe 0x64c9d0 →
  // renderer +0x1e0 0x101792c0, names at 0x976ed4: AlphaBlend, Additive) picks
  // Additive; else +0x1f2 (`writeZ`, 0x64cc10 → +0x1d8 0x1017a1b0) picks
  // DiffuseWithZWrite; else Diffuse.
  const bool additive = definition.text("blendMode") == "Additive";
  const bool writeZ = definition.number("writeZ").value_or(0) != 0;
  look.technique = additive ? 1 : writeZ ? 2 : 0;
  // psDiffuse: calcParticleLighting(hemi alpha, LerpAndLMapIntOffset, lightFactor).
  // The mesh template's LightmapIntensityOffset getter (vtable +0x1b4,
  // 0x101793b0) returns −1 ([0x101e14d0]) whatever the data says (its setter
  // +0x1b0, 0x101793a0, is empty), so saturate(clamp(height term, 0, 1) − 1) is
  // 0 and the lookup is the hemi map's alpha alone. The hemi map is not
  // sampled here (not done yet): its alpha is taken as 1, sunlit.
  // lightFactor (+0x1b8, 0x101794a0) is `lightAffectionFactor` (+0x1a8) when
  // ≥ 0, else the light manager's default (`defaultEffectLightAffectionFactor`).
  if (look.technique != 1) {
    const auto t = particles::SpriteTemplate::fromDefinition(definition);
    const float lightFactor =
        t.lightAffectionFactor >= 0 ? t.lightAffectionFactor : lighting_.defaultEffectLightAffectionFactor;
    const Vec3f diffuse = lighting_.effectSunColor;  // lerp(shadow, sun, 1)
    look.lighting = Vec3f{1, 1, 1} * (1.0f - lightFactor) + diffuse * lightFactor;
  }

  // `geometry mp_waterfall` is a `GeometryTemplate.create MeshParticleMesh`
  // in its own `.con`; the mesh lies beside it as for any object.
  const std::string geometry(definition.text("geometry"));
  const auto path = game::resolveGeometryPath(files, std::string(registry.geometryFile(geometry)), geometry);
  if (path.empty()) return index;
  auto source = game::loadMesh(files, path, 0, 0, false, true);
  if (!source || source->vertexPart.size() != source->vertices.size()) return index;

  // Instance slot 0: the triangles whose vertices carry BLENDINDICES 0, which
  // MeshParticleMesh.fx skins by `mOneBoneSkinning[0]`. Every slot is drawn
  // with one particle's matrix; here each particle is its own draw item.
  mesh::RenderMesh slot = *source;
  slot.indices.clear();
  for (auto& range : slot.ranges) {
    const auto start = static_cast<std::uint32_t>(slot.indices.size());
    for (std::uint32_t i = range.indexStart; i + 2 < range.indexStart + range.indexCount; i += 3) {
      const auto a = source->indices[i], b = source->indices[i + 1], c = source->indices[i + 2];
      if (source->vertexPart[a] == 0 && source->vertexPart[b] == 0 && source->vertexPart[c] == 0)
        slot.indices.insert(slot.indices.end(), {a, b, c});
    }
    range.indexStart = start;
    range.indexCount = static_cast<std::uint32_t>(slot.indices.size()) - start;
  }
  auto uploaded = renderer.upload(slot, resolve);
  if (!uploaded) return index;
  // MeshParticleMesh.fx: every technique alpha blends; Additive is ONE/ONE
  // with rgb ·= alpha. DiffuseWithZWrite also writes depth, which the
  // renderer's blended ranges do not: drawn as Diffuse.
  for (auto& range : uploaded->ranges) {
    range.alphaBlend = true;
    range.additiveBlend = look.technique == 1;
  }
  look.frames.push_back(std::move(*uploaded));
  looks_.push_back(std::move(look));
  index = static_cast<int>(looks_.size()) - 1;
  return index;
}

void AmbientEffects::load(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
                          const gfx::MeshRenderer::TextureResolver& resolve,
                          const std::vector<level::StaticObject>& placement, const level::Lighting& lighting) {
  lighting_ = lighting;
  // A template's tree is flattened once; most placements are buildings with no
  // particle system anywhere in them.
  std::unordered_map<std::string, std::optional<game::ObjectInstance>> trees;
  std::map<std::string, int> undrawn;
  std::size_t placed = 0, meshes = 0;
  for (const auto& object : placement) {
    auto [tree, fresh] = trees.try_emplace(object.templateName);
    if (fresh) {
      tree->second = game::flattenObject(registry, object.templateName);
      if (tree->second && std::ranges::none_of(tree->second->parts, [](const auto& part) {
            return part.className == "SpriteParticleSystem" || part.className == "MeshParticleSystem";
          })) {
        tree->second.reset();
      }
    }
    if (!tree->second) continue;

    // The same placement matrix `placeObjects` gives a building.
    Mat4 world = object.transform;
    if (!object.hasTransform) {
      world = translation(object.position);
      if (object.hasRotation)
        world = world * rotationYawPitchRoll(object.rotation.x, object.rotation.y, object.rotation.z);
    }
    bool any = false;
    for (const auto& part : tree->second->parts) {
      if (part.className == "Sound") {
        if (const auto* definition = registry.find(part.templateName))
          loadSound(*definition, files, transformPoint(world * part.transform, {}));
        continue;
      }
      const bool mesh = part.className == "MeshParticleSystem";
      if (!mesh && part.className != "SpriteParticleSystem") continue;
      const auto* definition = registry.find(part.templateName);
      if (!definition) continue;
      const int look = mesh ? meshLook(part.templateName, *definition, files, registry, renderer, resolve)
                            : spriteLook(part.templateName, *definition, files, renderer);
      if (look < 0) {
        ++undrawn[part.templateName];
        continue;
      }
      const Mat4 at = world * part.transform;
      auto tmpl = particles::SpriteTemplate::fromDefinition(*definition);
      // `isLocalSystem 0`: the particles live in the world, so the emitter's own
      // direction is turned once, by the placement and the part, and not after.
      tmpl.emitDirection = normalize(transformDirection(at, tmpl.emitDirection));
      for (int axis = 0; axis < 3; ++axis) {
        Vec3f unit{};
        (axis == 0 ? unit.x : axis == 1 ? unit.y : unit.z) = 1;
        tmpl.emitterAxes[axis] = transformDirection(at, unit);
      }
      const auto seed = static_cast<std::uint32_t>(emitters_.size()) * 7919u + 1u;
      emitters_.push_back(std::make_unique<Emitter>(tmpl, seed, transformPoint(at, {}),
                                                    static_cast<std::size_t>(look)));
      if (mesh) ++meshes;
      any = true;
    }
    if (any) ++placed;
  }
  std::printf("  ambient effects: %zu placements, %zu emitters (%zu mesh), %zu sounds\n", placed,
              emitters_.size(), meshes, sounds_.size());
  for (const auto& [name, count] : undrawn)
    std::printf("  ambient effects: %s x%d not drawn\n", name.c_str(), count);
}

// A bundle's `Sound` (`waterfall.wav`, `loopCount 0`, `is3dSound 1`). The
// falloff is SmokeFlare's (full gain to minDistance, halving every
// halfVolumeDistance − minDistance beyond it); BF2Audio.dll's own is not
// decoded, and neither are pan, `pitchEnvelope` or `reverbLevel`.
void AmbientEffects::loadSound(const game::ObjectTemplate& definition, FileSystem& files, Vec3f position) {
  const std::string names(definition.text("soundFilename"));
  if (names.empty() || definition.number("is3dSound").value_or(1) == 0) return;
  std::istringstream list(names);
  std::string name;
  std::getline(list, name, ',');
  const auto bytes = files.read(normalizeAssetPath(name));
  if (!bytes) return;
  SDL_AudioSpec format{};
  Uint8* pcm = nullptr;
  Uint32 size = 0;
  if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes->data(), bytes->size()), true, &format, &pcm, &size)) return;
  Sound sound;
  const SDL_AudioSpec mono{SDL_AUDIO_F32, 1, format.freq};
  Uint8* converted = nullptr;
  int length = 0;
  if (SDL_ConvertAudioSamples(&format, pcm, static_cast<int>(size), &mono, &converted, &length)) {
    const auto* input = reinterpret_cast<const float*>(converted);
    sound.mono.assign(input, input + length / sizeof(float));
  }
  SDL_free(converted);
  SDL_free(pcm);
  if (sound.mono.empty()) return;
  sound.rate = static_cast<int>(static_cast<float>(format.freq) * definition.number("pitch").value_or(1));
  sound.volume = definition.number("volume").value_or(1);
  sound.minDistance = definition.number("minDistance").value_or(10);
  sound.halfDistance = definition.number("halfVolumeDistance").value_or(20);
  sound.loop = definition.number("loopCount").value_or(1) == 0;
  sound.position = position;
  sounds_.push_back(std::move(sound));
}

void AmbientEffects::listen(Vec3f eye) {
  for (auto& sound : sounds_) {
    const float distance = length(sound.position - eye);
    const float gain = sound.volume * volume_ *
                       std::exp2(-std::max(0.0f, distance - sound.minDistance) /
                                 std::max(1.0f, sound.halfDistance - sound.minDistance));
    if (!sound.stream) {
      if (gain < 1e-3f) continue;
      if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
      const SDL_AudioSpec format{SDL_AUDIO_F32, 1, sound.rate};
      sound.stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, nullptr, nullptr));
      if (!sound.stream) continue;
      SDL_ResumeAudioStreamDevice(sound.stream.get());
    }
    SDL_SetAudioStreamGain(sound.stream.get(), gain);
    // Keep at least one copy queued so the loop has no gap.
    const int bytes = static_cast<int>(sound.mono.size() * sizeof(float));
    if (sound.loop && SDL_GetAudioStreamQueued(sound.stream.get()) < bytes)
      SDL_PutAudioStreamData(sound.stream.get(), sound.mono.data(), bytes);
  }
}

void AmbientEffects::advance(float seconds) {
  for (auto& e : emitters_) {
    e->lastStep = seconds;
    e->emitter.update(seconds, e->position);
  }
}

// RendDX9.dll 0x10175ac0, branch 2: the velocity's basis, stretched along it by
// `directionalScale`, then the particle's size on all three axes. Our Mat4 is
// laid out as D3D's rows, so rows X, Y, D, position go straight in.
void AmbientEffects::drawMesh(Emitter& e, const Look& look, std::vector<gfx::MeshRenderer::DrawItem>& items) {
  const auto& t = e.tmpl;
  for (auto& p : e.emitter.particles()) {
    const float age = p.ageFactor;
    const float alpha = clamp01(t.transparencyGraph(age)) * p.alphaBase;
    if (alpha <= 0) continue;
    Vec3f x{1, 0, 0}, y{0, 1, 0}, d{0, 0, 1};
    float stretch = 1;
    const float speed = length(p.velocity);
    if (!look.alignRotationToSpeed && look.directionalScale == 0) {
      // Branch 1: angles += clamp01(rotationGraph(age)) × frame time × spin,
      // then 0x10078e20(−z, −y, −x) = Rz(−z)·Rx(−y)·Ry(−x) in degrees, each
      // in D3D row layout, multiplied this·arg (0x1007c9b0).
      p.angles = p.angles + p.spins * (clamp01(t.rotationGraph(age)) * e.lastStep);
      constexpr float kDegrees = 0.017453292f;  // 0x10238738
      const auto rows = [](float az, float ay, float ax) {
        const float cz = std::cos(az), sz = std::sin(az), cy = std::cos(ay), sy = std::sin(ay);
        const float cx = std::cos(ax), sx = std::sin(ax);
        const float A[3][3]{{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
        const float B[3][3]{{1, 0, 0}, {0, cy, -sy}, {0, sy, cy}};
        const float C[3][3]{{cx, 0, sx}, {0, 1, 0}, {-sx, 0, cx}};
        float BC[3][3]{}, M[3][3]{};
        for (int i = 0; i < 3; ++i)
          for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) BC[i][j] += B[i][k] * C[k][j];
        for (int i = 0; i < 3; ++i)
          for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) M[i][j] += A[i][k] * BC[k][j];
        return std::array<Vec3f, 3>{Vec3f{M[0][0], M[0][1], M[0][2]}, Vec3f{M[1][0], M[1][1], M[1][2]},
                                    Vec3f{M[2][0], M[2][1], M[2][2]}};
      }(-p.angles.z * kDegrees, -p.angles.y * kDegrees, -p.angles.x * kDegrees);
      x = rows[0];
      y = rows[1];
      d = rows[2];
    } else if (speed >= kRestSpeed) {
      d = p.velocity * (1.0f / speed);
      if (std::abs(d.x) >= kVertical || std::abs(d.z) >= kVertical) {
        x = normalize(cross(Vec3f{0, 1, 0}, d));
        y = cross(d, x);
      } else if (d.y <= 0) {
        x = {1, 0, 0}; d = {0, -1, 0}; y = {0, 0, 1};
      } else {
        x = {1, 0, 0}; d = {0, 1, 0}; y = {0, 0, -1};
      }
      if (look.directionalScale > 0) stretch = speed * look.directionalScale;
    } else {
      x = {1, 0, 0}; y = {0, 1, 0}; d = {0, 0, 1};
    }
    const float size = clamp01(t.sizeGraph(age)) * t.particleMaxSize + p.sizeTerm;
    Mat4 m = translation(p.position);
    const Vec3f rx = x * size, ry = y * size, rz = d * (stretch * size);
    m.m[0] = rx.x; m.m[1] = rx.y; m.m[2] = rx.z;
    m.m[4] = ry.x; m.m[5] = ry.y; m.m[6] = ry.z;
    m.m[8] = rz.x; m.m[9] = rz.y; m.m[10] = rz.z;
    // MeshParticleMesh.fx: lerp(color1, color2, min(colorBlendGraph(age), 1)).
    const float blend = std::min(t.colorBlendGraph(age), 1.0f);
    Vec3f color = t.color2 * blend + t.color1 * (1.0f - blend);
    color = {color.x * look.lighting.x, color.y * look.lighting.y, color.z * look.lighting.z};
    auto& item = items.emplace_back(&look.frames.front(), m);
    item.particleOpacity = alpha;
    item.particleColor[0] = color.x;
    item.particleColor[1] = color.y;
    item.particleColor[2] = color.z;
  }
}

void AmbientEffects::draw(std::vector<gfx::MeshRenderer::DrawItem>& items, const Mat4& view) {
  // The camera near plane is not wired here (SmokeFlare::draw passes
  // particles::kWorldNearPlane). No hemi map or effect lighting is passed:
  // these sprites keep light 1.
  particles::View camera{view, 0.0f, nullptr, {}};
  for (auto& e : emitters_) {
    const auto& look = looks_[e->look];
    if (look.mesh) {
      drawMesh(*e, look, items);
    } else {
      scratch_.clear();
      e->emitter.collect(e->lastStep, scratch_, &camera);
      particles::emitSprites(scratch_, look.frames, e->tmpl, view, items);
    }
    // Spin and frames advance once per simulated frame, not once per view.
    e->lastStep = 0.0f;
  }
}

void AmbientEffects::release(gfx::MeshRenderer& renderer) {
  emitters_.clear();
  sounds_.clear();
  for (auto& look : looks_)
    for (auto& mesh : look.frames) renderer.release(mesh);
  looks_.clear();
  lookByTemplate_.clear();
}

}  // namespace obf2::app
