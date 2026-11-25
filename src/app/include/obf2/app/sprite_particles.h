#pragma once

// The original's screen-aligned sprite particle system (template type 0 in
// RendDX9.dll). Every formula and constant is in
// docs/functions/sprite-particles.md with its address.

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "obf2/app/hemi_map.h"
#include "obf2/core/math.h"
#include "obf2/game/object_template.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::app::particles {

// `a/b/c/d` from the data: a·t³ + b·t² + c·t + d.
struct Graph {
  std::array<float, 4> c{0, 0, 0, 1};
  float operator()(float t) const { return ((c[0] * t + c[1]) * t + c[2]) * t + c[3]; }
};

// The renderer's random numbers (RendDX9.dll 0x1009f860).
class Random {
 public:
  explicit Random(std::uint32_t seed) : state_(seed % 0x7fffffffu ? seed % 0x7fffffffu : 1u) {}
  std::uint32_t next();
  float symmetric();  // [-1, 1): q·2⁻²³ − 1 (0x1021fe20)
  float unit();       // (0, 1]: q·2⁻²⁴ (0x1020cf68)
  float fullTurn();   // q·π·2⁻²³ (0x1021db30)
  float halfTurn();   // q·π·2⁻²⁴ (0x1021fc8c)
  // Polar Box–Muller (0x1017a560): two draws per pair, the second value kept
  // for the next call. The original keeps that spare in one global
  // ([0x102619a0], flag [0x102619a4]) shared by every emitter; here it is
  // per generator.
  float normal();

 private:
  std::uint32_t state_;
  bool spare_ = false;
  float spareValue_ = 0;
};

// A `CRD_…/a/b/mirror` field (`emitRangeX` …), sampled by 0x1017a680:
// with `mirror` a first draw ≤ 0.5 flips the sign; then 0x1017a620 by type —
// 0 the value a, 1 uniform between a and b (0x1009f8f0), 2 exponential of
// mean a (0x1017a530), 3 a + b·normal (0x1017a560). The type numbers are the
// switch's; that CRD_NONE/UNIFORM/EXPONENTIAL/NORMAL are 0..3 is read from
// that arithmetic, not from the parser.
struct Crd {
  int type = 0;
  float a = 0, b = 0;
  bool mirror = false;
  float sample(Random& random) const;
  static Crd parse(std::string_view text);
};

// The type-0 template, field for field. Offsets are the renderer
// template's (docs/functions/sprite-particles.md, "Field → template offset").
struct SpriteTemplate {
  float preWarmTime = 0;                 // +0x0c, copied to instance +0xf8 by 0x1017ff20, run at 0x10180f23
  float emitFrequency = 0;               // +0x10
  Vec3f emitRadius;                      // +0x18
  int emitPrio = 0;                      // +0x60, index into manager+0x8bfc (0x10180893); not used yet (see update)
  // +0x64, `lodDistance` Low/Medium/High = 0/1/2; ctor default 2 (0x10176447).
  // Indexes the distance-fade table (0x10171000, 0x10171d20).
  int lodDistance = 2;
  bool isLooping = false;                // +0x6c
  bool additive = false;                 // +0x78 == 2 (`particleType Additive`)
  float randomTimeToLive = 0;            // +0x80
  float timeToLive = 1;                  // +0x84
  float gravity = 0;                     // +0x8c
  float airResistance = 0;               // +0x90
  float randomRotationSpeed = 0;         // +0x94
  float rotationSpeed = 0;               // +0x98
  float alphaCull = 0;                   // +0x9c
  // +0xa0, ctor 1 (0x10176515). Spawn copies it to record +0x08 (0x101802ef);
  // the vertex writer gives the two bottom corners min(shadow, intensity)
  // (0x10170070). SpriteDraw::shadowColor; the GPU path does not draw it yet.
  float selfShadowIntensity = 1;
  float windAffectionFactor = 0;         // +0xa4, scales the wind (0x1017f520)
  bool forceToWaterSurface = false;      // +0xa8, drawn y on the water (0x1017f520)
  // +0xac, ctor 0.03 (0x101764c5). Not used yet: no type-0 function reads it;
  // only its getter 0x101776e0 (vtable 0x1021f428), whose caller is not found.
  float waterSurfaceOffset = 0.03f;
  bool animationPlayOnce = false;        // +0xcc
  bool animationRandomizedStartFrame = false;  // +0xcd
  float animationSpeed = 0;              // +0xd0
  bool animationEnable = false;          // +0xd4
  float frameWidth = 1, frameHeight = 1; // +0xd8, +0xdc
  int frameCount = 0;                    // +0xe0
  int frameCountX = 1;                   // +0xe4
  bool collisionEnable = false;          // +0xec, the plane ring (0x10180120, 0x1017f520)
  float collisionSpeedAlongNormal = 0;   // +0xf0, bounce factor along the normal
  float collisionSpeedAlongPlane = 0;    // +0xf4, factor along the plane
  Graph rotationGraph, gravityGraph, airResistanceGraph, emitSpeedGraph;  // +0xfc..+0x138
  // +0x144, TemplateParameters m_uvRange…z (upload 0x1016fe00); Particles.fx:62
  // adds it to the hemi-altitude term of calcParticleLighting. `lighting()`.
  float lightMapIntensityOffset = 0;
  // +0x1f8, set through virtual +0x18c (setter 0x10176320); the ctor init
  // 0x101763e0 sets it to −1. `ObjectTemplate.LightAffectionFactor`: BF2.exe
  // registers it at 0x56d208, its setter 0x56d2c6 calls the forwarder
  // 0x64f650, which calls slot +0x18c. The finaliser 0x10177160 copies it to
  // +0x168, m_color1AndLightFactor.a, or, when it is below 0, LightManager
  // (DAT_1024b914) virtual +0x270 — the value the console command
  // `Lightmanager.defaultEffectLightAffectionFactor` reads back (its getter
  // 0x100be3ba calls the same slot).
  float lightAffectionFactor = -1;
  Graph colorBlendGraph, transparencyGraph, sizeGraph;                    // +0x17c..+0x1a8
  float pivotX = 0, pivotY = 0;          // +0x1ac
  float randomSize = 0;                  // +0x1b4
  float randomIntensity = 0;             // +0x1b8
  float randomRotation = 0;              // +0x1bc
  float randomSpeed = 0;                 // +0x1c0
  Vec3f randomDirectionAngle;            // +0x1c4, degrees
  float emitSpeed = 0;                   // +0x1d0
  float emitTime = 0;                    // +0x1d4
  float emitDelay = 0;                   // +0x1d8
  Vec3f color1{1, 1, 1}, color2{1, 1, 1};  // +0x1dc, +0x1e8
  Vec3f emitDirection{0, 1, 0};          // +0x20c, normalized by the setter
  // Wind time scale (update 0x10180ba0, 0x10180d70..0x10180e33). Setters
  // 0x10177830 (slot +0x268) and 0x10177810 (slot +0x260); the ctor sets 0
  // (0x101764cf) and 10 (0x101764d5). Their BF2.exe field names are not
  // traced: no forwarder in 0x640000..0x660000 calls either slot.
  float windTimeScaleFactor = 0;    // +0xb0, purpose named here, not by the game
  float windTimeScaleSpeed = 10;    // +0xb4, same
  bool alignRotationToSpeed = false;     // +0x21c, angle from screen velocity (0x10172450)
  float particleMaxSize = 1;             // Particles.fx m_uvRange…ParticleMaxSize.w
  // +0x29c, setter 0x10177870. The near-culling constants have no setter;
  // the ctor 0x101763e0 writes them (0x1017648b..0x1017652a).
  bool nearCullingEnable = false;
  float nearCullDistance = 6.0f;        // +0x288 (0x1017648b)
  float nearCullDistanceScale = 0.5f;   // +0x28c (0x10176495)
  float nearCullAge = 0.15f;            // +0x290, age factor (0x1017649f)
  float nearCullAgeScale = 4.0f;        // +0x294 (0x101764a9)
  float nearCullAlpha = 0.01f;          // +0x298, below this not drawn (0x1017652a)
  // `emitterType` (+0x264; type 1 +0x214): the index into BF2.exe's table
  // 0x976ec4 Standard, RadialDirection, Radial, Box. 0 and 3 are ported
  // (0x101813d0, 0x101816f0); 1 and 2 (0x10181500) emit as 0.
  int emitterType = 0;
  std::array<Crd, 3> emitRange;          // +0x24/+0x38/+0x4c (type 1 +0x20/+0x34/+0x48)
  // `isLocalSystem 0`: the spawn offset is turned by the emitter's matrix
  // (0x10079d00 in 0x101801c0 / 0x101850e0). Rows of that rotation.
  std::array<Vec3f, 3> emitterAxes{Vec3f{1, 0, 0}, Vec3f{0, 1, 0}, Vec3f{0, 0, 1}};

  // MeshParticleSystem (template type 1, ParticleSystemManager 0x100d5280):
  // its own spawn (0x101850e0) and integrator (0x10184450).
  bool meshType = false;
  Vec3f randomRotation3, randomRotationSpeed3, rotationSpeed3;  // +0x174, +0xac, +0xb8, degrees
  // lightAffectionFactor above is type 1 +0x1a8 for a mesh system; < 0 takes
  // the renderer's default there too (0x101794a0).

  // +0x14: 1/emitTime, or 0.2 when emitTime ≤ 0 (0x10177160).
  float minimumRate() const { return emitTime > 0 ? 1.0f / emitTime : 0.2f; }

  static SpriteTemplate fromDefinition(const game::ObjectTemplate& definition);
};

struct Particle {
  Vec3f position, velocity;  // record +0x48, +0x60
  Vec3f drawPosition;        // record +0x18: position, y on the water if forced (0x1017f520)
  float age = 0, ttl = 1, ageFactor = 0;
  float sizeTerm = 0, intensityTerm = 0;
  float angle = 0, spin = 0, frame = 0;
  float intensity = 1;   // instance+0x15c, 1 in every record sampled
  float alphaBase = 1;   // instance+0x160, 1 in every record sampled
  float shadow = 1;      // record +0x08, selfShadowIntensity (0x101802ef)
  // record +0x2c, the distance alpha. Spawn writes it (0x101802e0) from a
  // register not traced; the port starts it at the alpha base.
  float alpha = 1;
  Vec3f angles, spins;   // type 1 record +0xa8, +0x9c (degrees, degrees per second)
};

// The renderer's rotation table (0x10171a58): 4096 steps of a turn, sin in
// the low short and cos in the high one, each ·32767 through _ftol
// (0x101c4c64, truncation). The angle is rounded into it with ·651.899
// (0x1021ef10) and masked to 12 bits (0x101726e6..0x101726f5).
struct Rotation {
  float sin = 0, cos = 1;  // already divided by 32767 (Particles.fx OneOverShort)
};
Rotation quantizedRotation(float angle);

// What one sprite looks like this frame (Particles.fx vsParticle). The quad
// stays screen-aligned; `rotation` turns its texcoords (Particles.fx:78).
struct SpriteDraw {
  Vec3f position;
  float halfSize = 0;
  float angle = 0;
  Rotation rotation;
  Vec3f color;          // top corners
  Vec3f shadowColor;    // bottom corners: intensity min(shadow, intensity)
  float alpha = 0;
  int frame = 0;
  int nextFrame = 0;        // ParticleHigh's second frame
  float frameBlend = 0.0f;  // and its weight, frac(frame)
  // Particles.fx:62: saturate(clamp((y − hemiShadowAltitude)/10) + offset), the
  // lmOffset that calcParticleLighting adds to the hemi map's alpha.
  float lightMapOffset = 0;
  // calcParticleLighting for this sprite: ParticleHigh with the hemi map's
  // alpha as lm, or ParticleMedium's lm = 1 without one. Constant over the
  // sprite, so it multiplies both colours as psParticle multiplies the texel.
  Vec3f light{1, 1, 1};
};

// The level's effect lighting (Sky.con `Lightmanager.effectSunColor`,
// `effectShadowColor`, `defaultEffectLightAffectionFactor`). The defaults
// make calcParticleLighting 1.
struct EffectLighting {
  Vec3f sun{1, 1, 1};
  Vec3f shadow{1, 1, 1};
  float defaultLightAffectionFactor = 0;
};

// calcParticleLighting (Shaders_client.zip:FXCommon.fx:56-62). ParticleMedium
// passes lm = 1 (Particles.fx:153); ParticleHigh the hemi map's alpha (:165).
Vec3f lighting(float lm, float lmOffset, float lightFactor, Vec3f effectSunColor,
               Vec3f effectShadowColor);

// What the particle manager supplies (RendDX9.dll, manager DAT_1024b900).
struct World {
  // +0x8c0c virtual +0x3c: the wind at a point. Consulted only while the
  // global flag +0x8c10 is set; leave empty for "off".
  std::function<Vec3f(Vec3f)> wind;
  // +0x8c14 virtual +0x94: the water height under a point; empty when absent.
  std::function<float(Vec3f)> waterHeight;
};

// A collision plane: dot(n, p) + d. The instance keeps its current one at
// +0x208 and a ring of ten older ones. BF2.exe builds it from a surface
// normal and point as (n, −n·p) (0x5a6bd0).
struct Plane {
  Vec3f normal;
  float d = 0;
};

// The camera the collectors 0x10171000 / 0x10171d20 see.
// The collectors' camera virtual +0x34 is the camera's near plane (vtable
// 0x1021c968, field +0x28), which the renderer sets from its +0x590 every
// frame: 0.041 (0x1002dd16, docs/functions/render-camera.md).
inline constexpr float kWorldNearPlane = 0.041f;

struct View {
  Mat4 view;            // world → view, left-handed (z into the screen)
  // Camera virtual +0x34, the depth below which nothing is collected. Which
  // camera value it is is not established.
  float nearClip = 0;
  // The level's base hemi map (HemiMapManager data, 0x101721c0): its alpha is
  // ParticleHigh's lm and its height `HemiShadowAltitude`. Null draws
  // ParticleMedium (lm = 1).
  const HemiMap* hemi = nullptr;
  EffectLighting lighting;
};

// Distance fade near distance and 1/length by lodDistance (particle manager
// +0x8bec and +0x8be0, measured at effects quality 2,
// retail-corrugated-grenade-11/trial/particle-fade.csv).
inline constexpr std::array<float, 3> kFadeNear{57.375f, 114.75f, 191.25f};
inline constexpr std::array<float, 3> kFadeScale{0.0987654f, 0.0493827f, 0.0296296f};

class SpriteEmitter {
 public:
  // The instance's cap (+0x220), set by its constructor (RendDX9.dll
  // 0x1017fb00): round(span · emitFrequency + 10), span = timeToLive +
  // randomTimeToLive, or min(emitTime, that) when emitTime is not 0 and the
  // system does not loop (10 at 0x101e2b00). Emission (0x10180790) spawns only
  // while the live count (+0x28) is below it.
  static std::size_t particleCap(const SpriteTemplate& t);

  SpriteEmitter(const SpriteTemplate& tmpl, std::uint32_t seed) : tmpl_(&tmpl), random_(seed) {}

  void setWorld(const World* world) { world_ = world; }
  // Instance slot +0x48 (0x101800e0): the plane particles collide with now
  // (kept at +0x208), its normal normalized (0x10077e90), d kept. BF2.exe sets
  // it once, when an effect bundle is spawned on a surface (EffectBundle
  // 0x641ba0 forwards it to every particle system of the bundle).
  void setCollisionPlane(Plane plane);

  // 0x10180ba0. `emitting` false stops new particles (the bundle has gone).
  void update(float dt, Vec3f emitterPosition, bool emitting = true);
  // The collector (0x10171d20 sorted, 0x10171000 additive), 0x10172450 and
  // the vertex shader: distance fade, near culling, spin or alignment, frame,
  // then the look. Without a view there is no fade, culling or alignment.
  void collect(float dt, std::vector<SpriteDraw>& out, const View* view = nullptr);
  bool finished() const { return stopped_ && particles_.empty(); }
  // Instance +0x130: max(clamp01(|wind|² / max(+0xb4², 1)) · +0xb0, 1) · time
  // scale, with the wind at the emitter (0x10180d70..0x10180e33). No reader
  // in the type-0 path is found; the type-4 per-frame 0x1016f190 multiplies
  // by it (0x1016f54b).
  float windTimeScale() const { return windTimeScale_; }
  const std::vector<Particle>& particles() const { return particles_; }
  std::vector<Particle>& particles() { return particles_; }

 private:
  void spawn(Vec3f emitterPosition);
  void spawnMesh(Vec3f emitterPosition);
  Vec3f shapeOffset();
  // 0x1017f520 (type 1: 0x10184450): false when the particle died.
  bool integrate(Particle& particle, float step, int steps) const;
  const Plane& collisionPlane(float age) const;
  bool integrateMesh(Particle& particle, float step, int steps) const;

  const SpriteTemplate* tmpl_;
  const World* world_ = nullptr;
  Random random_;
  std::vector<Particle> particles_;
  // Collision: +0x208 current plane, +0x168 ring of ten, +0x218 its current
  // index, +0x219 the next, +0x21c the clock since the last push (0x10180120).
  // The ctor 0x1017fb00 sets the current plane and all ten ring slots to
  // (0, 1, 0, 0), the y = 0 ground plane, and index, next and clock to 0.
  static constexpr Plane kGroundPlane{{0, 1, 0}, 0};
  Plane plane_ = kGroundPlane;
  std::array<Plane, 10> ring_{kGroundPlane, kGroundPlane, kGroundPlane, kGroundPlane, kGroundPlane,
                              kGroundPlane, kGroundPlane, kGroundPlane, kGroundPlane, kGroundPlane};
  int ringCurrent_ = 0, ringNext_ = 0;
  float ringClock_ = 0;
  float windTimeScale_ = 1;
  bool started_ = false, stopped_ = false;
  float carry_ = 0, now_ = 0, previousNow_ = 0, lastEmit_ = 0;
  float delay_ = 0, remaining_ = 0;
  float preWarm_ = 0;  // instance +0xf8, spent once the delay has run out
                       // (0x10180f23)
  Vec3f previousPosition_, position_;
};

// A quad of half extent 1 over one atlas cell (x, y, width, height in UV),
// less the pivot. Particles.fx's corner displacement and texture window.
mesh::RenderMesh spriteQuad(float x, float y, float width, float height, float pivotX, float pivotY);

// One camera-facing draw item per sprite, each on its frame's quad, with its
// texcoord rotation, self-shadow colour and ParticleHigh frame blend.
void emitSprites(const std::vector<SpriteDraw>& sprites, const std::vector<gfx::GpuMesh>& frames,
                 const SpriteTemplate& t, const Mat4& view, std::vector<gfx::MeshRenderer::DrawItem>& items);

}  // namespace obf2::app::particles
