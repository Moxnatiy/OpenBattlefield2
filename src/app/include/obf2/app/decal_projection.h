#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::app {

// Recovered finite-input primitive: RendDX9.dll 0x101ad510/0x101af1a0.
// Positions and UVs are clipped together; distance zero is inside.
struct DecalVertex { Vec3f position; float u, v; };
inline std::optional<std::vector<DecalVertex>> clipDecalPolygon(
    std::vector<DecalVertex> polygon, const std::array<Vec3f, 3>& triangle) {
  const Vec3f normal = normalize(cross(triangle[1] - triangle[0], triangle[2] - triangle[0]));
  for (int edge = 0; edge < 3 && !polygon.empty(); ++edge) {
    const auto start = triangle[edge];
    const Vec3f plane = normalize(cross(triangle[(edge + 1) % 3] - start, normal));
    std::vector<DecalVertex> result;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
      const auto& a = polygon[i];
      const auto& b = polygon[(i + 1) % polygon.size()];
      const float da = dot(a.position - start, plane), db = dot(b.position - start, plane);
      if (da <= 0.0f) result.push_back(a);
      if ((da > 0.0f) != (db > 0.0f)) {
        const float denominator = da - db;
        if (std::abs(denominator) <= std::numeric_limits<float>::epsilon()) return std::nullopt;
        const float t = da / denominator;
        result.push_back({a.position + (b.position - a.position) * t,
                          a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t});
      }
    }
    polygon = std::move(result);
  }
  return polygon;
}

// Static-owner adapter. The ray is tested only against the placed receiver's
// rendered mesh, never whichever object happens to be nearest the proxy hit.
// SimpleObject ownership: BF2.exe 0x627ac0. The adapter's render-triangle seed
// and seam adjacency are local plumbing; general Retail contact-interface
// selection is not measured. Unsupported owners retain the existing fallback.
inline std::optional<mesh::RenderMesh> projectStaticDecal(
    const mesh::RenderMesh& receiver, const Mat4& transform, Vec3f origin, Vec3f ray,
    float size, float rotation, float u, float v, float width, float height) {
  struct Face { std::array<Vec3f, 3> p; Vec3f normal; std::array<int, 3> neighbor{-1, -1, -1}; };
  std::vector<Face> faces;
  faces.reserve(receiver.indices.size() / 3);
  using Point = std::tuple<float, float, float>;
  using Edge = std::pair<Point, Point>;
  std::map<Edge, std::pair<int, int>> edges;
  const auto key = [](Vec3f p) { return Point{p.x, p.y, p.z}; };
  int seed = -1;
  float nearest = std::numeric_limits<float>::max();
  for (std::size_t index = 0; index + 2 < receiver.indices.size(); index += 3) {
    Face face;
    for (int j = 0; j < 3; ++j) {
      const auto vi = receiver.indices[index + j];
      if (vi >= receiver.vertices.size()) return std::nullopt;
      const auto p = receiver.vertices[vi].position;
      face.p[j] = transformPoint(transform, {p.x, p.y, p.z});
    }
    face.normal = normalize(cross(face.p[1] - face.p[0], face.p[2] - face.p[0]));
    const auto id = static_cast<int>(faces.size());
    faces.push_back(face);
    for (int j = 0; j < 3; ++j) {
      auto a = key(face.p[j]), b = key(face.p[(j + 1) % 3]);
      if (b < a) std::swap(a, b);
      const auto [it, inserted] = edges.emplace(Edge{a, b}, std::pair{id, j});
      if (!inserted) {
        const auto [other, edge] = it->second;
        faces[id].neighbor[j] = other;
        faces[other].neighbor[edge] = id;
      }
    }
    const float denominator = dot(ray, face.normal);
    if (denominator >= 0.0f) continue;
    const float t = dot(face.p[0] - origin, face.normal) / denominator;
    if (t < 0.0f || t > 1.0f || t >= nearest) continue;
    const Vec3f hit = origin + ray * t;
    bool inside = true;
    for (int j = 0; j < 3; ++j) {
      if (dot(cross(face.p[(j + 1) % 3] - face.p[j], hit - face.p[j]), face.normal) < 0.0f)
        inside = false;
    }
    if (inside) { nearest = t; seed = id; }
  }
  if (seed < 0) return std::nullopt;
  const Vec3f normal = faces[seed].normal;
  const Vec3f direction = normal * -1.0f;
  const Vec3f center = origin + ray * nearest;
  const Vec3f up = std::abs(normal.y) > 0.9f ? Vec3f{0, 0, 1} : Vec3f{0, 1, 0};
  const Vec3f tangent = normalize(cross(up, normal)), bitangent = cross(normal, tangent);
  const float c = std::cos(rotation), s = std::sin(rotation);
  const Vec3f x = (tangent * c + bitangent * s) * size;
  const Vec3f y = (bitangent * c - tangent * s) * size;
  // Prototype +/-1 bounds: RendDX9.dll 0x1023aef8, builder 0x101ae450.
  const std::vector<DecalVertex> quad = {
    {center - x - y, u, v + height}, {center + x - y, u + width, v + height},
    {center + x + y, u + width, v}, {center - x + y, u, v}};
  mesh::RenderMesh output;
  std::vector<bool> visited(faces.size());
  // Initial traversal depth 4: RendDX9.dll 0x1023af4c ->0x101ae450.
  std::vector<std::pair<int, int>> pending{{seed, 4}};
  while (!pending.empty()) {
    const auto [id, depth] = pending.back(); pending.pop_back();
    if (visited[id]) continue;
    visited[id] = true;
    const auto& face = faces[id];
    // Angle gate 0x101ad860; clipAngle 60 degrees, read live with the M4.
    if (dot(face.normal, normal) < std::cos(60.0f * 0.01745329238474369f)) continue;
    const auto clipped = clipDecalPolygon(quad, face.p);
    if (!clipped) continue;
    for (std::size_t i = 1; i + 1 < clipped->size(); ++i) {
      for (const auto j : {std::size_t{0}, i, i + 1}) {
        const auto& source = (*clipped)[j];
        // Orthogonal projection 0x101adc21..67, then -separation*direction
        // at 0x101adc70..8d. Live separation 0.01, not a proxy inflation.
        const Vec3f p = source.position - face.normal * dot(face.normal, source.position - face.p[0])
                        - direction * 0.01f;
        mesh::Vertex vertex;
        vertex.position = {p.x, p.y, p.z};
        vertex.normal = {face.normal.x, face.normal.y, face.normal.z};
        vertex.uv[0] = source.u; vertex.uv[1] = source.v;
        output.indices.push_back(static_cast<std::uint32_t>(output.vertices.size()));
        output.vertices.push_back(vertex);
      }
      // Recovered per-decal output guard 0x101adab0, not a mark budget.
      if (output.vertices.size() > 127) return mesh::RenderMesh{};
    }
    bool unchanged = clipped->size() == quad.size();
    if (unchanged) {
      for (std::size_t i = 0; i < quad.size(); ++i) {
        const auto d = (*clipped)[i].position - quad[i].position;
        if (d.x != 0 || d.y != 0 || d.z != 0) unchanged = false;
      }
    }
    if (depth > 0 && !unchanged) {
      for (int next : face.neighbor) if (next >= 0 && !visited[next]) pending.emplace_back(next, depth - 1);
    }
  }
  if (!output.indices.empty()) output.ranges.emplace_back().indexCount = static_cast<std::uint32_t>(output.indices.size());
  return output;
}

}  // namespace obf2::app
