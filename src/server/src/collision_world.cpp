#include "obf2/server/collision_world.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace obf2::server {
namespace {

// The triangle's closest point to a given one — the classic Voronoi-region
// case analysis. Needed to know how far and where to push a sphere out.
Vec3f closestPointOnTriangle(const Vec3f& point, const CollisionTriangle& triangle) {
  const Vec3f ab = triangle.b - triangle.a;
  const Vec3f ac = triangle.c - triangle.a;
  const Vec3f ap = point - triangle.a;

  const float d1 = dot(ab, ap);
  const float d2 = dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) return triangle.a;

  const Vec3f bp = point - triangle.b;
  const float d3 = dot(ab, bp);
  const float d4 = dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) return triangle.b;

  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
    const float v = d1 / (d1 - d3);
    return triangle.a + ab * v;
  }

  const Vec3f cp = point - triangle.c;
  const float d5 = dot(ab, cp);
  const float d6 = dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) return triangle.c;

  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
    const float w = d2 / (d2 - d6);
    return triangle.a + ac * w;
  }

  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return triangle.b + (triangle.c - triangle.b) * w;
  }

  const float denom = 1.0f / (va + vb + vc);
  const float v = vb * denom;
  const float w = vc * denom;
  return triangle.a + ab * v + ac * w;
}

// ---- The engine's sphere-against-mesh tests (docs/functions/soldier-physics.md,
// "Contacts with objects"). Linux server addresses. ----

constexpr float kEpsilon = 1.1920929e-7f;  // 0xb2bfb0

// The two axes a triangle test projects on (0x724c38, 0x723b8d): x and z when
// the normal is steep up (|n.y| >= 0.7, 0xb35e38), x and y when it faces z
// (|n.z| > 0.3, 0xb2f3d4), y and z otherwise. `third` is the axis left out.
void projectionAxes(const Vec3f& n, int& u, int& v, int& third) {
  if (std::abs(n.y) >= 0.7f) {
    u = 0, v = 2, third = 1;
  } else if (std::abs(n.z) > 0.3f) {
    u = 0, v = 1, third = 2;
  } else {
    u = 1, v = 2, third = 0;
  }
}

float axis(const Vec3f& p, int i) { return i == 0 ? p.x : (i == 1 ? p.y : p.z); }
void setAxis(Vec3f& p, int i, float value) {
  if (i == 0) p.x = value;
  else if (i == 1) p.y = value;
  else p.z = value;
}

// `checkFaceAndEdgeCollision` (0x724ae0), single sided.
bool faceAndEdge(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2, const Vec3f& n,
                 const Vec3f& from, const Vec3f& to, Vec3f& point, float& depth,
                 float& travel) {
  const Vec3f d = to - from;
  if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f) return false;
  const float endDepth = dot(to - v0, n);
  if (endDepth > 0.0f) return false;
  const float fromDepth = dot(from - v0, n);
  if (fromDepth < 0.0f) return false;
  const float dn = dot(n, d);
  if (dn >= 0.0f) return false;
  const float t = fromDepth / -dn;

  int u = 0, v = 0, third = 0;
  projectionAxes(n, u, v, third);
  const float pu = axis(from, u) + t * axis(d, u);
  const float pv = axis(from, v) + t * axis(d, v);
  const float e0 = (pu - axis(v0, u)) * (axis(v1, v) - axis(v0, v)) -
                   (pv - axis(v0, v)) * (axis(v1, u) - axis(v0, u));
  const float e1 = (pu - axis(v1, u)) * (axis(v2, v) - axis(v1, v)) -
                   (pv - axis(v1, v)) * (axis(v2, u) - axis(v1, u));
  const float e2 = (pu - axis(v2, u)) * (axis(v0, v) - axis(v2, v)) -
                   (pv - axis(v2, v)) * (axis(v0, u) - axis(v2, u));
  if (e0 < -kEpsilon && (e1 > kEpsilon || e2 > kEpsilon)) return false;
  if (e0 > kEpsilon && (e1 < -kEpsilon || e2 < -kEpsilon)) return false;

  setAxis(point, u, pu);
  setAxis(point, v, pv);
  setAxis(point, third, axis(from, third) + t * axis(d, third));
  depth = endDepth;
  travel = (t - 1.0f) * length(d);
  return true;
}

// `checkShiftedFaceCollision` (0x724e40).
bool shiftedFace(const CollisionTriangle& tri, const Vec3f& from, const Vec3f& to, float radius,
                 Vec3f& point, float& depth, float& travel) {
  const Vec3f d = to - from;
  if (dot(d, d) < kEpsilon) return false;
  const Vec3f shift = tri.normal * radius;
  if (!faceAndEdge(tri.a + shift, tri.b + shift, tri.c + shift, tri.normal, from, to, point,
                   depth, travel)) {
    return false;
  }
  point = point - shift;
  return true;
}

// `classifyPointToTriEdges` (0x723b70): 7 when inside every edge.
int classifyToEdges(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2, const Vec3f& n,
                    const Vec3f& p) {
  int u = 0, v = 0, third = 0;
  projectionAxes(n, u, v, third);
  const auto edge = [&](const Vec3f& a, const Vec3f& b, const Vec3f& q) {
    return (axis(q, u) - axis(a, u)) * (axis(b, v) - axis(a, v)) -
           (axis(q, v) - axis(a, v)) * (axis(b, u) - axis(a, u));
  };
  int bits = 0;
  if (edge(v2, v0, p) >= 0.0f) bits |= 1;
  if (edge(v0, v1, p) >= 0.0f) bits |= 2;
  if (edge(v1, v2, p) >= 0.0f) bits |= 4;
  // The winding: the centroid (1/3, 0xb4a5d8) against the edge v1 -> v2.
  const Vec3f centroid = (v0 + v1 + v2) * 0.33333334f;
  if (edge(v1, v2, centroid) < 0.0f) bits ^= 7;
  return bits;
}

// `checkSphereLineCollision` (0x7251c0). `side` is 0 past `a`, 1 past `b`.
bool sphereLine(const Vec3f& c, float r, const Vec3f& a, const Vec3f& b, Vec3f& point,
                Vec3f& normal, float& depth, int& side) {
  const Vec3f line = b - a;
  const float len = length(line);
  if (!(len > 0.0f)) return false;
  const float s = dot(c - a, line) / len;
  if (s < 0.0f) {
    side = 0;
    return false;
  }
  if (s > len) {
    side = 1;
    return false;
  }
  point = a + line * (s / len);
  normal = c - point;
  const float distanceSquared = dot(normal, normal);
  if (distanceSquared > r * r) return false;
  const float distance = std::sqrt(distanceSquared);
  normal = normal * (1.0f / distance);
  depth = distance - r;
  return true;
}

// `checkSpherePointCollision` (0x723a70).
bool spherePoint(const Vec3f& c, float r, const Vec3f& p, Vec3f& point, Vec3f& normal,
                 float& depth) {
  const Vec3f d = c - p;
  const float distanceSquared = dot(d, d);
  if (distanceSquared > r * r) return false;
  point = p;
  const float distance = std::sqrt(distanceSquared);
  normal = d * (1.0f / distance);
  depth = distance - r;
  return true;
}

// `checkSphereTriCollision` (0x7254a0).
bool sphereTri(const CollisionTriangle& tri, const Vec3f& c, float r, Vec3f& point,
               Vec3f& normal, float& depth) {
  const Vec3f& n = tri.normal;
  const float dist = dot(c - tri.a, n);
  if (std::abs(dist) > r) return false;
  const Vec3f p = c - n * dist;
  const int inside = classifyToEdges(tri.a, tri.b, tri.c, n, p);
  if (inside == 7) {
    point = p;
    normal = n;
    depth = dist - r;
    return true;
  }
  const Vec3f* v[3] = {&tri.a, &tri.b, &tri.c};
  int previous = 2;
  int vertex = -1;
  for (int i = 0; i < 3; ++i) {
    if ((inside & (1 << i)) == 0) {
      int side = -1;
      if (sphereLine(c, r, *v[i], *v[previous], point, normal, depth, side)) return true;
      if (side != -1) vertex = side == 0 ? i : previous;
    }
    previous = i;
  }
  if (vertex == -1) return false;
  return spherePoint(c, r, *v[vertex], point, normal, depth);
}

// The closest distance between the segments p0..p1 and q0..q1 —
// `getClosestDistanceBetweenLines` (0x723cf0) only serves as the early out of the
// edge test, so any exact segment distance gives it the same answer.
float segmentDistance(const Vec3f& p0, const Vec3f& p1, const Vec3f& q0, const Vec3f& q1) {
  const Vec3f d1 = p1 - p0;
  const Vec3f d2 = q1 - q0;
  const Vec3f r = p0 - q0;
  const float a = dot(d1, d1);
  const float e = dot(d2, d2);
  const float f = dot(d2, r);
  float s = 0.0f;
  float t = 0.0f;
  if (a <= kEpsilon && e <= kEpsilon) {
    return length(r);
  }
  if (a <= kEpsilon) {
    t = std::clamp(f / e, 0.0f, 1.0f);
  } else {
    const float c = dot(d1, r);
    if (e <= kEpsilon) {
      s = std::clamp(-c / a, 0.0f, 1.0f);
    } else {
      const float b = dot(d1, d2);
      const float denominator = a * e - b * b;
      s = denominator != 0.0f ? std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
      t = (b * s + f) / e;
      if (t < 0.0f) {
        t = 0.0f;
        s = std::clamp(-c / a, 0.0f, 1.0f);
      } else if (t > 1.0f) {
        t = 1.0f;
        s = std::clamp((b - c) / a, 0.0f, 1.0f);
      }
    }
  }
  return length((p0 + d1 * s) - (q0 + d2 * t));
}

// Where the point `p + t v` enters and leaves the capsule of `radius` around the
// segment `a .. a + edge` — `getIntersectionOfCapsAndEdgeInternal` (0x725b90): the
// cylinder's roots within the segment, then each end cap's beyond it. A line meets
// a capsule at its entry and its exit, and those are the two the engine returns.
int capsuleRoots(const Vec3f& p, const Vec3f& v, const Vec3f& a, const Vec3f& edge, float radius,
                 float roots[2]) {
  const float edgeLength = length(edge);
  if (edgeLength <= kEpsilon) return 0;
  const float speed = length(v);
  if (speed <= kEpsilon) return 0;
  const Vec3f axisDir = edge * (1.0f / edgeLength);
  const Vec3f dir = v * (1.0f / speed);
  const Vec3f w = p - a;
  // Along the axis, and across it.
  const float along = dot(w, axisDir);
  const float dirAlong = dot(dir, axisDir);
  const Vec3f wPerp = w - axisDir * along;
  const Vec3f dirPerp = dir - axisDir * dirAlong;

  float found[6];
  int count = 0;
  const auto add = [&](float s) {  // s in distance units along `dir`
    if (count < 6) found[count++] = s;
  };
  // The cylinder, when the motion is not along the axis (0.9999999, 0x725d00).
  if (std::abs(dirAlong) < 0.9999999f) {
    const float qa = dot(dirPerp, dirPerp);
    const float qb = dot(dirPerp, wPerp);
    const float qc = dot(wPerp, wPerp) - radius * radius;
    const float disc = qb * qb - qa * qc;
    if (disc >= 0.0f) {
      const float root = std::sqrt(disc);
      for (const float s : {(-qb - root) / qa, (-qb + root) / qa}) {
        const float at = along + s * dirAlong;
        if (at >= 0.0f && at <= edgeLength) add(s);
      }
    }
  }
  // The two caps.
  for (const float end : {0.0f, edgeLength}) {
    const Vec3f toCentre = w - axisDir * end;
    const float b = dot(dir, toCentre);
    const float c = dot(toCentre, toCentre) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) continue;
    const float root = std::sqrt(disc);
    for (const float s : {-b - root, -b + root}) {
      const float at = along + s * dirAlong;
      if (end == 0.0f ? at <= 0.0f : at >= edgeLength) add(s);
    }
  }
  if (count == 0) return 0;
  float entry = found[0];
  float exit = found[0];
  for (int i = 1; i < count; ++i) {
    entry = std::min(entry, found[i]);
    exit = std::max(exit, found[i]);
  }
  roots[0] = entry / speed;
  roots[1] = exit / speed;
  return entry == exit ? 1 : 2;
}

// The moving sphere against a triangle's edges, `checkExpandedEdgesCollision`
// (0x7266e0): each edge's capsule of the radius against the centre's path
// (`getIntersectionOfCapsAndEdgeNew` 0x726540 keeps the roots within the motion,
// after a segment distance over the radius rules the edge out); the earliest meeting
// of all three edges wins. The contact point is the edge's closest point to the
// centre there, the normal from it to the centre, the depth how far the motion's
// end is along that normal, the travel minus the distance still to go.
bool movingSphereEdges(const CollisionTriangle& tri, const Vec3f& from, const Vec3f& to,
                       float radius, Vec3f& point, Vec3f& normal, float& depth, float& travel) {
  const Vec3f motion = to - from;
  const Vec3f* v[3] = {&tri.a, &tri.b, &tri.c};
  float best = 3.4028235e+38f;
  const Vec3f* bestA = nullptr;
  const Vec3f* bestB = nullptr;
  Vec3f centre{};
  int previous = 2;
  for (int i = 0; i < 3; ++i) {
    const Vec3f& a = *v[i];
    const Vec3f& b = *v[previous];
    previous = i;
    if (segmentDistance(from, to, a, b) > radius) continue;
    float roots[2];
    const int all = capsuleRoots(from, motion, a, b - a, radius, roots);
    float kept[2];
    int count = 0;
    for (int k = 0; k < all; ++k) {
      if (roots[k] >= 0.0f && roots[k] <= 1.0f) kept[count++] = roots[k];
    }
    if (count == 0) continue;
    const float t = (count == 2 && kept[1] <= kept[0]) ? kept[1] : kept[0];
    if (t < best) {
      best = t;
      bestA = &a;
      bestB = &b;
      centre = from + motion * t;
    }
  }
  if (bestA == nullptr) return false;

  const Vec3f edge = *bestB - *bestA;
  const float edgeSquared = dot(edge, edge);
  Vec3f dir = edge;
  if (std::abs(edgeSquared - 1.0f) >= kEpsilon) {
    dir = std::abs(edgeSquared) >= kEpsilon ? edge * (1.0f / std::sqrt(edgeSquared)) : Vec3f{};
  }
  const float s = dot(centre - *bestA, dir);
  if (s <= 0.0f) point = *bestA;
  else if (s * s < edgeSquared) point = *bestA + dir * s;
  else point = *bestB;
  normal = centre - point;
  const float nn = dot(normal, normal);
  if (std::abs(nn - 1.0f) >= kEpsilon) {
    normal = std::abs(nn) >= kEpsilon ? normal * (1.0f / std::sqrt(nn)) : Vec3f{};
  }
  depth = dot(normal, to - centre);
  travel = -length(to - centre);
  return true;
}

}  // namespace

void CollisionWorld::sphereContacts(const Vec3f& from, const Vec3f& motion, float radius,
                                    std::vector<MeshContact>& out) const {
  const Vec3f to = from + motion;
  // `g_coll_use_fast_sphere_at_length` 0.01 (0x718dd2); the resting sphere's
  // capsule is 1.5 radii (0xb2efc0).
  const bool resting = dot(motion, motion) <= 0.01f * 0.01f;
  const float reach = (resting ? radius * 1.5f : radius) + length(motion) * 0.5f;
  const Vec3f middle = from + motion * 0.5f;

  // Per object, in the order the grid gives them: whether any face was met
  // decides the edges (0x71acf1).
  struct PerObject {
    std::uint32_t object;
    bool faceHit;
    std::vector<const CollisionTriangle*> faces;
  };
  std::vector<PerObject> objects;
  forEachNearby(middle, reach, [&](const CollisionTriangle& tri) {
    auto it = std::find_if(objects.begin(), objects.end(),
                           [&](const PerObject& o) { return o.object == tri.object; });
    if (it == objects.end()) {
      objects.push_back({tri.object, false, {}});
      it = objects.end() - 1;
    }
    it->faces.push_back(&tri);
  });
  for (const Movable& movable : movables_) {
    if (!movable.alive) continue;
    if (middle.x + reach < movable.minimum.x || middle.x - reach > movable.maximum.x ||
        middle.y + reach < movable.minimum.y || middle.y - reach > movable.maximum.y ||
        middle.z + reach < movable.minimum.z || middle.z - reach > movable.maximum.z) {
      continue;
    }
    for (const CollisionTriangle& tri : movable.triangles) {
      auto it = std::find_if(objects.begin(), objects.end(),
                             [&](const PerObject& o) { return o.object == tri.object; });
      if (it == objects.end()) {
        objects.push_back({tri.object, false, {}});
        it = objects.end() - 1;
      }
      it->faces.push_back(&tri);
    }
  }

  for (PerObject& object : objects) {
    for (const CollisionTriangle* tri : object.faces) {
      MeshContact contact;
      contact.material = tri->material;
      if (resting) {
        if (!sphereTri(*tri, to, radius, contact.point, contact.normal, contact.depth)) continue;
        if (contact.depth > 0.0f) continue;
        contact.travel = contact.depth;
      } else {
        if (!shiftedFace(*tri, from, to, radius, contact.point, contact.depth, contact.travel)) {
          continue;
        }
        if (contact.depth > 0.0f) continue;
        contact.normal = tri->normal;
      }
      out.push_back(contact);
      object.faceHit = true;
    }
    // `g_coll_use_cyl` 1 (0x718d7e).
    if (resting || object.faceHit) continue;
    for (const CollisionTriangle* tri : object.faces) {
      MeshContact contact;
      contact.material = tri->material;
      if (!movingSphereEdges(*tri, from, to, radius, contact.point, contact.normal, contact.depth,
                             contact.travel)) {
        continue;
      }
      if (!(contact.depth < 0.0f)) continue;
      // An edge never pushes down (0x71bb24): a normal below the horizon loses its
      // height, and one with nothing left is dropped.
      if (contact.normal.y < 0.0f) {
        contact.normal.y = 0.0f;
        const float nn = dot(contact.normal, contact.normal);
        if (nn < kEpsilon) continue;
        if (std::abs(nn - 1.0f) >= kEpsilon) contact.normal = contact.normal * (1.0f / std::sqrt(nn));
      }
      out.push_back(contact);
    }
  }
}

std::uint64_t CollisionWorld::cellKey(int x, int y, int z) {
  // Shift into positive numbers and pack 21 bits per axis: that is enough for a
  // map of millions of cells.
  const std::uint64_t ux = static_cast<std::uint64_t>(x + 0x100000) & 0x1FFFFF;
  const std::uint64_t uy = static_cast<std::uint64_t>(y + 0x100000) & 0x1FFFFF;
  const std::uint64_t uz = static_cast<std::uint64_t>(z + 0x100000) & 0x1FFFFF;
  return (ux << 42) | (uy << 21) | uz;
}

namespace {

// The layer's faces in world space, degenerate and corrupt ones left out.
void worldTriangles(const mesh::CollisionLayer& layer, const Mat4& transform,
                    std::uint32_t object, std::vector<CollisionTriangle>& out) {
  for (const mesh::CollisionFace& face : layer.faces) {
    if (face.a >= layer.vertices.size() || face.b >= layer.vertices.size() ||
        face.c >= layer.vertices.size()) {
      continue;  // a corrupt face — skipped rather than fatal
    }
    CollisionTriangle triangle;
    auto toWorld = [&](const mesh::Vec3& v) {
      return transformPoint(transform, Vec3f{v.x, v.y, v.z});
    };
    triangle.a = toWorld(layer.vertices[face.a]);
    triangle.b = toWorld(layer.vertices[face.b]);
    triangle.c = toWorld(layer.vertices[face.c]);
    // The normal follows the engine's left-handed convention, that is the
    // opposite of the usual right-handed cross product. That is visible in
    // Dalian Plant's own data: with this sign 12081 triangles face up against
    // 5999 down, which is what one expects of a world of roads, roofs and stairs.
    const Vec3f normal = cross(triangle.c - triangle.a, triangle.b - triangle.a);
    const float area = length(normal);
    if (area < 1e-6f) continue;  // a degenerate triangle
    triangle.normal = normal * (1.0f / area);
    triangle.object = object;
    triangle.material = face.material;
    out.push_back(triangle);
  }
}

}  // namespace

std::uint32_t CollisionWorld::addMovable(std::vector<CollisionPiece> pieces,
                                         const Mat4& transform) {
  Movable movable;
  movable.pieces = std::move(pieces);
  for (std::size_t i = 0; i < movable.pieces.size(); ++i) movable.objects.push_back(layers_++);
  movable.alive = true;
  placeTriangles(movable, transform);
  movables_.push_back(std::move(movable));
  return static_cast<std::uint32_t>(movables_.size() - 1);
}

void CollisionWorld::placeMovable(std::uint32_t handle, const Mat4& transform) {
  if (handle >= movables_.size() || !movables_[handle].alive) return;
  placeTriangles(movables_[handle], transform);
}

void CollisionWorld::removeMovable(std::uint32_t handle) {
  if (handle >= movables_.size()) return;
  movables_[handle].alive = false;
  movables_[handle].triangles.clear();
}

std::size_t CollisionWorld::movableCount() const {
  return static_cast<std::size_t>(std::count_if(movables_.begin(), movables_.end(),
                                                [](const Movable& m) { return m.alive; }));
}

void CollisionWorld::placeTriangles(Movable& movable, const Mat4& transform) {
  movable.triangles.clear();
  for (std::size_t i = 0; i < movable.pieces.size(); ++i) {
    const CollisionPiece& piece = movable.pieces[i];
    if (piece.layer == nullptr) continue;
    worldTriangles(*piece.layer, transform * piece.local, movable.objects[i], movable.triangles);
  }
  const float big = std::numeric_limits<float>::max();
  movable.minimum = Vec3f{big, big, big};
  movable.maximum = Vec3f{-big, -big, -big};
  for (const CollisionTriangle& t : movable.triangles) {
    for (const Vec3f& p : {t.a, t.b, t.c}) {
      movable.minimum = Vec3f{std::min(movable.minimum.x, p.x), std::min(movable.minimum.y, p.y),
                              std::min(movable.minimum.z, p.z)};
      movable.maximum = Vec3f{std::max(movable.maximum.x, p.x), std::max(movable.maximum.y, p.y),
                              std::max(movable.maximum.z, p.z)};
    }
  }
}

void CollisionWorld::addLayer(const mesh::CollisionLayer& layer, const Mat4& transform) {
  const std::uint32_t object = layers_++;
  const std::size_t first = triangles_.size();
  worldTriangles(layer, transform, object, triangles_);
  for (std::size_t i = first; i < triangles_.size(); ++i) {
    const CollisionTriangle& triangle = triangles_[i];
    const auto index = static_cast<std::uint32_t>(i);

    // A triangle goes into every cell its bounding box crosses.
    const Vec3f minimum{std::min({triangle.a.x, triangle.b.x, triangle.c.x}),
                        std::min({triangle.a.y, triangle.b.y, triangle.c.y}),
                        std::min({triangle.a.z, triangle.b.z, triangle.c.z})};
    const Vec3f maximum{std::max({triangle.a.x, triangle.b.x, triangle.c.x}),
                        std::max({triangle.a.y, triangle.b.y, triangle.c.y}),
                        std::max({triangle.a.z, triangle.b.z, triangle.c.z})};

    for (int x = static_cast<int>(std::floor(minimum.x / cellSize_));
         x <= static_cast<int>(std::floor(maximum.x / cellSize_)); ++x) {
      for (int y = static_cast<int>(std::floor(minimum.y / cellSize_));
           y <= static_cast<int>(std::floor(maximum.y / cellSize_)); ++y) {
        for (int z = static_cast<int>(std::floor(minimum.z / cellSize_));
             z <= static_cast<int>(std::floor(maximum.z / cellSize_)); ++z) {
          cells_[cellKey(x, y, z)].push_back(index);
        }
      }
    }
  }
}

void CollisionWorld::forEachNearby(
    const Vec3f& position, float radius,
    const std::function<void(const CollisionTriangle&)>& visit) const {
  const int x0 = static_cast<int>(std::floor((position.x - radius) / cellSize_));
  const int x1 = static_cast<int>(std::floor((position.x + radius) / cellSize_));
  const int y0 = static_cast<int>(std::floor((position.y - radius) / cellSize_));
  const int y1 = static_cast<int>(std::floor((position.y + radius) / cellSize_));
  const int z0 = static_cast<int>(std::floor((position.z - radius) / cellSize_));
  const int z1 = static_cast<int>(std::floor((position.z + radius) / cellSize_));

  // One triangle may lie in several cells, so we make sure not to process it twice.
  std::vector<std::uint32_t> seen;
  for (int x = x0; x <= x1; ++x) {
    for (int y = y0; y <= y1; ++y) {
      for (int z = z0; z <= z1; ++z) {
        const auto cell = cells_.find(cellKey(x, y, z));
        if (cell == cells_.end()) continue;
        for (const std::uint32_t index : cell->second) {
          if (std::find(seen.begin(), seen.end(), index) != seen.end()) continue;
          seen.push_back(index);
          visit(triangles_[index]);
        }
      }
    }
  }
}

int CollisionWorld::resolveSphere(Vec3f& position, float radius) const {
  int pushes = 0;

  // Several passes: being pushed out of one wall can drive you into another,
  // and in a corner at least two are needed.
  constexpr int kIterations = 4;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    bool moved = false;

    forEachNearby(position, radius, [&](const CollisionTriangle& triangle) {
      const Vec3f closest = closestPointOnTriangle(position, triangle);
      const Vec3f away = position - closest;
      const float distance = length(away);
      if (distance >= radius || distance < 1e-6f) return;

      // Push out exactly far enough to touch the surface.
      position = position + away * ((radius - distance) / distance);
      moved = true;
      ++pushes;
    });

    if (!moved) break;
  }
  return pushes;
}

void CollisionWorld::normalStats(std::size_t* up, std::size_t* down) const {
  std::size_t upCount = 0, downCount = 0;
  for (const CollisionTriangle& triangle : triangles_) {
    if (triangle.normal.y > 0.5f) ++upCount;
    else if (triangle.normal.y < -0.5f) ++downCount;
  }
  if (up != nullptr) *up = upCount;
  if (down != nullptr) *down = downCount;
}

bool CollisionWorld::groundHeight(const Vec3f& from, float maxDrop, float minNormalY,
                                  float* outHeight) const {
  // A strictly downward ray. The cells are walked with the same index as for a
  // sphere: we take everything nearby horizontally over the whole search depth.
  const float bottom = from.y - maxDrop;
  bool found = false;
  float best = bottom;

  const Vec3f middle{from.x, (from.y + bottom) * 0.5f, from.z};
  forEachNearby(middle, maxDrop * 0.5f + 0.5f, [&](const CollisionTriangle& triangle) {
    if (triangle.normal.y < minNormalY) return;  // a wall, not a floor

    // The intersection of the vertical ray with the triangle's plane.
    if (std::abs(triangle.normal.y) < 1e-6f) return;
    const float distance = dot(triangle.normal, triangle.a - from);
    const float t = distance / (-triangle.normal.y);
    if (t < 0.0f || t > maxDrop) return;

    const Vec3f hit{from.x, from.y - t, from.z};

    // Whether it is inside the triangle: computed in the XZ plane through the
    // signs of the cross products. For a sloped surface that is enough, because
    // the normal is not horizontal.
    const auto side = [](const Vec3f& p, const Vec3f& a, const Vec3f& b) {
      return (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
    };
    const float s1 = side(hit, triangle.a, triangle.b);
    const float s2 = side(hit, triangle.b, triangle.c);
    const float s3 = side(hit, triangle.c, triangle.a);
    const bool negative = s1 < 0.0f || s2 < 0.0f || s3 < 0.0f;
    const bool positive = s1 > 0.0f || s2 > 0.0f || s3 > 0.0f;
    if (negative && positive) return;

    if (!found || hit.y > best) {
      best = hit.y;
      found = true;
    }
  });

  if (found && outHeight != nullptr) *outHeight = best;
  return found;
}

}  // namespace obf2::server
