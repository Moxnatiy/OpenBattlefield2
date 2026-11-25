// The line walk over a `.collisionmesh` lod's tree: `BspV8::getClosestFace`
// and what it calls (Linux server 0x70ff70, 0x72c640, 0x70fc40, 0x70f960).
// docs/functions/collision-bsp.md.
#include <cmath>
#include <limits>

#include "obf2/mesh/collision.h"

namespace obf2::mesh {
namespace {

float component(const Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

// x86 `maxss dst, src` / `minss dst, src`: the destination when it wins the
// comparison, else the source (so a NaN gives the source).
float maxss(float dst, float src) { return dst > src ? dst : src; }
float minss(float dst, float src) { return dst < src ? dst : src; }

// `rayRadiusIntersectsAABB` (0x72c640): the slab test against the box grown by
// `radius`. t0/t1 are always written once the three slabs are passed.
bool rayRadiusIntersectsAabb(const Vec3& p, const Vec3& d, float radius, const Vec3& lo,
                             const Vec3& hi, float& t0, float& t1) {
  const float low[3] = {lo.x - radius, lo.y - radius, lo.z - radius};
  const float high[3] = {hi.x + radius, hi.y + radius, hi.z + radius};
  const float inv[3] = {1.0f / d.x, 1.0f / d.y, 1.0f / d.z};
  float nearT = 0.0f, farT = 0.0f;
  for (int axis = 0; axis < 3; ++axis) {
    const float origin = component(p, axis);
    // inv < 0 swaps the near and the far plane (0x72c6fb, 0x72c727, 0x72c770).
    const bool forward = !(inv[axis] < 0.0f);
    const float a = ((forward ? low[axis] : high[axis]) - origin) * inv[axis];
    const float b = ((forward ? high[axis] : low[axis]) - origin) * inv[axis];
    if (axis == 0) {
      nearT = a;
      farT = b;
      continue;
    }
    if (nearT > b || a > farT) return false;
    nearT = maxss(a, nearT);
    farT = minss(b, farT);
  }
  t0 = nearT;
  t1 = farT;
  return 1.0f >= t0 && t1 >= 0.0f;
}

// The search context (0x70ff70's stack frame, offsets in the notes).
struct Context {
  const CollisionLayer& layer;
  const CollisionBsp& bsp;
  const BspFaceTest& test;
  Vec3 start;
  Vec3 motion;
  Vec3 end;              // start + motion (0x70f9a4)
  float epsilon = 0.0f;  // +0x20: 1e-4 / |motion|
  float radius = 0.0f;   // +0x24: 0 for a line
  bool flip = false;     // +0x28
  bool twoSided = false; // +0x29
  bool stopFirst = false;// +0x2a
  bool done = false;     // +0x4
  bool found = false;    // +0x38 not null
  BspClosestFace best;
  float bestDistance = std::numeric_limits<float>::max();  // +0x4c, 0x7f7fffff at 0x710085
};

// `findClosestFace` (0x70f960): one leaf, its references in order.
void leaf(Context& context, const CollisionBspNode& node, int side, std::uint32_t first,
          std::uint32_t count) {
  constexpr double kSideSlack = 1e-4;  // Linux 0xb97318 (double)
  const auto& refs = context.bsp.faceRefs;
  for (std::uint32_t i = first; i < first + count && i < refs.size(); ++i) {
    const std::uint16_t face = refs[i];
    if (face >= context.layer.faces.size() || face >= context.bsp.normals.size()) continue;
    Vec3 normal = context.bsp.normals[face];
    if (!context.twoSided) {
      const float d = normal.x * context.motion.x + normal.y * context.motion.y +
                      normal.z * context.motion.z;
      // 0x70fa10: (d >= 0.0, 0xb2d910) has to equal flip.
      if ((d >= 0.0f) != context.flip) continue;
    }
    if (context.flip) normal = Vec3{-normal.x, -normal.y, -normal.z};
    BspFaceHit hit;
    if (!context.test(face, normal, context.start, context.end, hit)) continue;
    if (!(context.bestDistance > hit.distance)) continue;
    const double at = component(hit.point, node.axis());
    const double split = node.split;
    if (side != 0 ? !(at > split - kSideSlack) : !(split + kSideSlack > at)) continue;
    context.found = true;
    context.best.face = face;
    context.best.hit = hit;
    context.bestDistance = hit.distance;
    if (context.stopFirst) {
      context.done = true;
      return;
    }
  }
}

// `getCollidingFaces(node, t0, t1)` (0x70fc40): near child first, the far one
// only when the segment reaches the plane.
void walk(Context& context, std::uint32_t index, float t0, float t1, int depth) {
  // A file whose children loop would recurse for ever; the engine trusts its
  // files. 4096 is ours, deeper than any tree a 16-bit face list can need.
  if (index >= context.bsp.nodes.size() || depth > 4096) return;
  const CollisionBspNode& node = context.bsp.nodes[index];
  const float a = maxss(t0 - context.epsilon, 0.0f);
  const float b = minss(1.0f, t1 + context.epsilon);
  if (a >= b) return;
  const int axis = node.axis();
  if (axis == 3) return;  // not in the files: the engine would read past the vector
  const Vec3 p{context.start.x + context.motion.x * a, context.start.y + context.motion.y * a,
               context.start.z + context.motion.z * a};
  const float span = component(context.motion, axis) * (b - a);
  const float at = component(p, axis);
  const int side = at >= node.split ? 1 : 0;

  const auto visit = [&](int s, float from, float to) {
    if (node.leaf(s)) {
      leaf(context, node, s, node.child[s], node.count(s));
    } else {
      walk(context, node.child[s], from, to, depth + 1);
    }
  };
  visit(side, t0, t1);
  if (context.done) return;

  const float direction = component(context.motion, axis);
  const float origin = component(context.start, axis);
  float t = t0;
  if (side == 0) {
    const float plane = node.split - context.radius;
    if (plane > at) {
      if (direction <= 0.0f) return;
      if (plane - at > span) return;
      t = (plane - origin) / direction;
    }
  } else {
    const float plane = context.radius + node.split;
    if (at >= plane) {
      if (direction >= 0.0f) return;
      if (span >= plane - at) return;
      t = (plane - origin) / direction;
    }
  }
  visit(1 - side, t, t1);
}

}  // namespace

std::optional<BspClosestFace> closestFace(const CollisionLayer& layer, const Vec3& start,
                                          const Vec3& motion, bool twoSided, bool flip,
                                          bool stopFirst, const BspFaceTest& test) {
  if (!layer.bsp || layer.bsp->nodes.empty()) return std::nullopt;
  const float squared = motion.x * motion.x + motion.y * motion.y + motion.z * motion.z;
  if (squared <= 1.4210855e-14f) return std::nullopt;  // Linux 0xb3e054
  Context context{layer,
                  *layer.bsp,
                  test,
                  start,
                  motion,
                  Vec3{start.x + motion.x, start.y + motion.y, start.z + motion.z},
                  1e-4f / std::sqrt(squared),  // 1e-4 at Linux 0xb355d0
                  0.0f,
                  flip,
                  twoSided,
                  stopFirst,
                  false,
                  false,
                  BspClosestFace{},
                  std::numeric_limits<float>::max()};

  float t0 = 0.0f, t1 = 1.0f;  // 0x7100ed, 0x7100f5
  if (!rayRadiusIntersectsAabb(start, motion, context.radius, layer.bsp->bounds.min,
                               layer.bsp->bounds.max, t0, t1)) {
    return std::nullopt;
  }
  t0 = 0.0f > t0 ? 0.0f : minss(1.0f, t0);  // 0x710112-0x710130
  t1 = t1 < 0.0f ? 0.0f : minss(1.0f, t1);  // 0x710134-0x71015b
  walk(context, 0, t0, t1, 0);
  if (!context.found) return std::nullopt;
  return context.best;
}

}  // namespace obf2::mesh
