#include "obf2/mesh/primitives.h"

#include "obf2/core/math.h"

namespace obf2::mesh {

RenderMesh buildBox(const Vec3& size, const std::string& map) {
  const float hx = size.x * 0.5f;
  const float hz = size.z * 0.5f;
  const float top = size.y;

  RenderMesh out;
  out.bounds = Aabb{Vec3{-hx, 0.0f, -hz}, Vec3{hx, top, hz}};

  // Six faces separately: a cube's normals differ across an edge, so vertices
  // are not shared between faces.
  struct Face {
    Vec3 normal;
    Vec3 corner[4];
  };
  const Face faces[6] = {
      {{0, 0, -1}, {{-hx, 0, -hz}, {hx, 0, -hz}, {hx, top, -hz}, {-hx, top, -hz}}},
      {{0, 0, 1}, {{hx, 0, hz}, {-hx, 0, hz}, {-hx, top, hz}, {hx, top, hz}}},
      {{-1, 0, 0}, {{-hx, 0, hz}, {-hx, 0, -hz}, {-hx, top, -hz}, {-hx, top, hz}}},
      {{1, 0, 0}, {{hx, 0, -hz}, {hx, 0, hz}, {hx, top, hz}, {hx, top, -hz}}},
      {{0, 1, 0}, {{-hx, top, -hz}, {hx, top, -hz}, {hx, top, hz}, {-hx, top, hz}}},
      {{0, -1, 0}, {{-hx, 0, hz}, {hx, 0, hz}, {hx, 0, -hz}, {-hx, 0, -hz}}},
  };

  for (const Face& face : faces) {
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (int i = 0; i < 4; ++i) {
      Vertex vertex;
      vertex.position = face.corner[i];
      vertex.normal = face.normal;
      vertex.uv[0] = (i == 1 || i == 2) ? 1.0f : 0.0f;
      vertex.uv[1] = (i >= 2) ? 1.0f : 0.0f;
      out.vertices.push_back(vertex);
    }
    out.indices.insert(out.indices.end(),
                       {base, base + 1, base + 2, base, base + 2, base + 3});
  }

  DrawRange range;
  range.indexStart = 0;
  range.indexCount = static_cast<std::uint32_t>(out.indices.size());
  range.maps.push_back(map);
  out.ranges.push_back(range);
  return out;
}

RenderMesh buildScreenQuad(const std::string& map) {
  const Vec3f light = normalize(Vec3f{0.4f, 0.9f, 0.35f});
  const Vec3 normal{light.x, light.y, light.z};

  RenderMesh quad;
  quad.vertices = {
      Vertex{{-1.0f, -1.0f, 0.0f}, normal, {0.0f, 1.0f}},
      Vertex{{1.0f, -1.0f, 0.0f}, normal, {1.0f, 1.0f}},
      Vertex{{-1.0f, 1.0f, 0.0f}, normal, {0.0f, 0.0f}},
      Vertex{{1.0f, 1.0f, 0.0f}, normal, {1.0f, 0.0f}},
  };
  quad.indices = {0, 1, 2, 2, 1, 3};

  DrawRange range;
  range.indexCount = static_cast<std::uint32_t>(quad.indices.size());
  if (!map.empty()) range.maps.push_back(map);
  quad.ranges.push_back(std::move(range));
  return quad;
}

}  // namespace obf2::mesh
