#include "obf2/mesh/collision.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace obf2::mesh {
namespace {

// A bounds-checked reader — the same approach as in the other parsers:
// the files come from the user's archives and are not to be trusted.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

  void fail(std::string why) {
    if (ok_) {
      ok_ = false;
      error_ = std::move(why) + " (offset " + std::to_string(position_) + ")";
    }
  }

  std::uint32_t dword(const char* what) { return read<std::uint32_t>(what); }
  std::uint16_t word(const char* what) { return read<std::uint16_t>(what); }
  std::uint8_t byte(const char* what) { return read<std::uint8_t>(what); }

  Vec3 vec3(const char* what) {
    Vec3 value;
    value.x = read<float>(what);
    value.y = read<float>(what);
    value.z = read<float>(what);
    return value;
  }

  void skip(std::size_t bytes, const char* what) {
    if (!ok_) return;
    if (data_.size() - position_ < bytes) {
      fail(std::string("file truncated: ") + what);
      return;
    }
    position_ += bytes;
  }

  // How many elements of size T are physically left.
  template <typename T>
  std::size_t capacity() const {
    return ok_ ? (data_.size() - position_) / sizeof(T) : 0;
  }

 private:
  template <typename T>
  T read(const char* what) {
    T value{};
    if (!ok_) return value;
    if (data_.size() - position_ < sizeof(T)) {
      fail(std::string("file truncated: ") + what);
      return value;
    }
    std::memcpy(&value, data_.data() + position_, sizeof(T));
    position_ += sizeof(T);
    return value;
  }

  std::span<const std::byte> data_;
  std::size_t position_ = 0;
  bool ok_ = true;
  std::string error_;
};

// `BspV8::load` (Linux 0x7107b0): bounds, the nodes, the face references.
CollisionBsp readBsp(Reader& reader) {
  CollisionBsp bsp;
  bsp.bounds.min = reader.vec3("bsp.min");
  bsp.bounds.max = reader.vec3("bsp.max");

  const std::uint32_t nodeCount = reader.dword("bsp.nodeCount");
  if (nodeCount > reader.capacity<std::uint32_t>() / 4) {
    reader.fail("implausible bsp node count");
    return bsp;
  }
  bsp.nodes.resize(nodeCount);
  for (CollisionBspNode& node : bsp.nodes) {
    std::uint32_t bits = reader.dword("bsp.node.split");
    std::memcpy(&node.split, &bits, sizeof(float));
    node.flags = reader.dword("bsp.node.flags");
    node.child[0] = reader.dword("bsp.node.child0");
    node.child[1] = reader.dword("bsp.node.child1");
  }

  const std::uint32_t faceRefCount = reader.dword("bsp.faceRefCount");
  if (faceRefCount > reader.capacity<std::uint16_t>()) {
    reader.fail("implausible bsp reference count");
    return bsp;
  }
  bsp.faceRefs.resize(faceRefCount);
  for (std::uint16_t& ref : bsp.faceRefs) ref = reader.word("bsp.faceRef");
  return bsp;
}

// The face normals 0x70f2e0 builds: (v3 − v2) × (v2 − v1); left as it is when
// already of unit length within 1.1920929e-7 (0xb2bfb0; 1.0 at 0xb2f3bc), zero
// when its squared length is under that, else divided by its length.
std::vector<Vec3> faceNormals(const CollisionLayer& layer) {
  constexpr float kEpsilon = 1.1920929e-7f;  // Linux 0xb2bfb0
  std::vector<Vec3> normals(layer.faces.size());
  for (std::size_t i = 0; i < layer.faces.size(); ++i) {
    const CollisionFace& face = layer.faces[i];
    if (face.a >= layer.vertices.size() || face.b >= layer.vertices.size() ||
        face.c >= layer.vertices.size()) {
      continue;  // a corrupt face: zero
    }
    const Vec3& v1 = layer.vertices[face.a];
    const Vec3& v2 = layer.vertices[face.b];
    const Vec3& v3 = layer.vertices[face.c];
    const Vec3 e1{v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
    const Vec3 e2{v3.x - v2.x, v3.y - v2.y, v3.z - v2.z};
    Vec3 n{e1.z * e2.y - e1.y * e2.z, e1.x * e2.z - e1.z * e2.x, e1.y * e2.x - e1.x * e2.y};
    const float squared = n.x * n.x + n.y * n.y + n.z * n.z;
    if (std::abs(squared - 1.0f) < kEpsilon) {
      normals[i] = n;
    } else if (std::abs(squared) < kEpsilon) {
      normals[i] = Vec3{};
    } else {
      const float scale = 1.0f / std::sqrt(squared);
      normals[i] = Vec3{n.x * scale, n.y * scale, n.z * scale};
    }
  }
  return normals;
}

CollisionLayer readLayer(Reader& reader, std::uint32_t versionMinor, std::uint32_t index) {
  CollisionLayer layer;

  // The layer type field appeared in version 0.9. The earlier 0.8 has none at
  // all, and the layer is decided by order: 0 projectiles, 1 vehicles, 2 soldier.
  // Without this branch 241 of the game's files parsed shifted and gave rubbish.
  if (versionMinor >= 9) {
    layer.type = static_cast<ColType>(reader.dword("col.type"));
  } else {
    layer.type = static_cast<ColType>(index);
  }

  const std::uint32_t faceCount = reader.dword("col.faceCount");
  if (faceCount > reader.capacity<std::uint16_t>() / 4) {
    reader.fail("implausible face count");
    return layer;
  }
  layer.faces.resize(faceCount);
  for (CollisionFace& face : layer.faces) {
    face.a = reader.word("face.a");
    face.b = reader.word("face.b");
    face.c = reader.word("face.c");
    face.material = reader.word("face.material");
  }

  const std::uint32_t vertexCount = reader.dword("col.vertexCount");
  if (vertexCount > reader.capacity<Vec3>()) {
    reader.fail("implausible vertex count");
    return layer;
  }
  layer.vertices.resize(vertexCount);
  for (Vec3& vertex : layer.vertices) vertex = reader.vec3("vertex");

  // We do not need the vertex materials yet, but they have to be skipped.
  reader.skip(static_cast<std::size_t>(vertexCount) * 2, "col.vertexMaterials");

  layer.bounds.min = reader.vec3("col.min");
  layer.bounds.max = reader.vec3("col.max");

  // The marker is ASCII '1' when a tree follows.
  const std::uint8_t marker = reader.byte("col.bspMarker");
  if (marker == '1') {
    layer.bsp = readBsp(reader);
    layer.bsp->normals = faceNormals(layer);
  }

  // Face adjacency data appeared in version 0.10.
  if (versionMinor >= 10) {
    const std::uint32_t adjacencyCount = reader.dword("col.adjacencyCount");
    if (adjacencyCount > reader.capacity<std::uint32_t>()) {
      reader.fail("implausible adjacency count");
      return layer;
    }
    reader.skip(static_cast<std::size_t>(adjacencyCount) * 4, "col.adjacency");
  }

  return layer;
}

}  // namespace

const CollisionLayer* CollisionMesh::layer(ColType type) const {
  for (const CollisionLayer& candidate : layers) {
    if (candidate.type == type) return &candidate;
  }
  return nullptr;
}

const CollisionLayer* CollisionMesh::validLayer(std::size_t part, std::size_t geometry,
                                                ColType type) const {
  if (part >= parts.size() || geometry >= parts[part].geometries.size()) return nullptr;
  const CollisionGeometry& lods = parts[part].geometries[geometry];
  const auto slot = static_cast<std::size_t>(type);
  if (slot >= 5) return nullptr;
  // isLodValid (0x7194e0) compares the slot unsigned, so -1 is past the end.
  const int lod = lods.table[slot];
  if (lod < 0 || static_cast<std::size_t>(lod) >= lods.lods.size()) return nullptr;
  const int index = lods.lods[static_cast<std::size_t>(lod)];
  return index < 0 ? nullptr : &layers[static_cast<std::size_t>(index)];
}

float CollisionMesh::maxBoundingRadius(std::size_t part) const {
  if (part >= parts.size()) return 0.0f;
  const auto& geometries = parts[part].geometries;
  for (std::size_t geometry = 0; geometry < geometries.size(); ++geometry) {
    const std::size_t lodCount = geometries[geometry].lods.size();
    if (lodCount == 0) continue;
    for (std::size_t type = 0; type < lodCount; ++type) {
      const CollisionLayer* layer = validLayer(part, geometry, static_cast<ColType>(type));
      if (layer == nullptr) continue;
      // calcRadius (0x72cb50): the largest squared length of the eight corners.
      float largest = 0.0f;
      for (int corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? layer->bounds.max.x : layer->bounds.min.x;
        const float y = (corner & 2) ? layer->bounds.max.y : layer->bounds.min.y;
        const float z = (corner & 4) ? layer->bounds.max.z : layer->bounds.min.z;
        largest = std::max(largest, x * x + y * y + z * z);
      }
      return std::sqrt(largest);
    }
  }
  return 0.0f;
}

std::optional<CollisionMesh> loadCollisionMesh(std::span<const std::byte> bytes,
                                               std::string* error) {
  Reader reader(bytes);
  CollisionMesh mesh;
  mesh.versionMajor = reader.dword("versionMajor");
  mesh.versionMinor = reader.dword("versionMinor");

  const std::uint32_t geometryPartCount = reader.dword("geometryPartCount");
  if (geometryPartCount > 4096) {
    reader.fail("implausible part count");
  }

  for (std::uint32_t part = 0; part < geometryPartCount && reader.ok(); ++part) {
    const std::uint32_t geometryCount = reader.dword("geometryCount");
    if (geometryCount > 4096) {
      reader.fail("implausible geom count");
      break;
    }
    for (std::uint32_t geometry = 0; geometry < geometryCount && reader.ok(); ++geometry) {
      const std::uint32_t layerCount = reader.dword("colCount");
      if (layerCount > 64) {
        reader.fail("implausible layer count");
        break;
      }
      CollisionGeometry lods;
      lods.colCount = layerCount;
      // Before 0.9 the vector is the lod count up front (0x71f600).
      if (mesh.versionMinor < 9) lods.lods.assign(layerCount, -1);
      for (std::uint32_t index = 0; index < layerCount && reader.ok(); ++index) {
        mesh.layers.push_back(readLayer(reader, mesh.versionMinor, index));
        const auto type = static_cast<std::uint32_t>(mesh.layers.back().type);
        if (type > 4) {
          // The engine writes the slot at +0x18 + type * 4 unchecked; a type past
          // the five slots is not in the game's files.
          reader.fail("collision lod type past the engine's table");
          break;
        }
        lods.table[type] = static_cast<int>(type);
        // An AI lod is skipped whole with keepAINav off, its slot back to -1.
        if (type == static_cast<std::uint32_t>(ColType::Ai)) {
          lods.table[type] = -1;
          if (mesh.versionMinor < 9) lods.lods[index] = -1;
          continue;
        }
        if (mesh.versionMinor >= 9) lods.lods.resize(type + 1, -1);
        lods.lods[type] = static_cast<int>(mesh.layers.size() - 1);
      }
      if (lods.table[2] == -1) lods.table[2] = lods.table[1];
      if (lods.table[3] == -1) lods.table[3] = lods.table[2];
      if (mesh.parts.size() == part) mesh.parts.emplace_back();
      mesh.parts[part].geometries.push_back(std::move(lods));
    }
    if (mesh.parts.size() == part) mesh.parts.emplace_back();
  }

  if (!reader.ok()) {
    if (error) *error = reader.error();
    return std::nullopt;
  }

  // CollisionManager::load (Linux 0x712e00) after every part is read. A part
  // whose geoms hold no lod at all fails its load (0x71f600 returns whether any
  // geom had a lod); the parts from the first failure after a success on are
  // dropped, unless that failure is the very first part.
  int firstFailed = -1;
  for (std::size_t part = 0; part < mesh.parts.size(); ++part) {
    bool any = false;
    for (const CollisionGeometry& geometry : mesh.parts[part].geometries) {
      // `local_22d |= colCount != 0`: a geom counts even when its only lod was
      // an AI one that was skipped.
      any = any || geometry.colCount != 0;
    }
    if (any) {
      firstFailed = -1;
    }
    else if (firstFailed == -1) {
      firstFailed = static_cast<int>(part);
    }
  }
  if (firstFailed > 0) mesh.parts.resize(static_cast<std::size_t>(firstFailed));

  // No part with a geom 0 that holds lods (`isGeomValid(0)`, 0x718f00): every
  // part's geoms move down by one and the last goes
  // (`setUseCollisionAsFirstPerson`, 0x719670). A vehicle's file keeps an empty
  // first-person geom 0, so its third-person geom becomes 0 and its wreck 1.
  bool geometryZero = false;
  for (const CollisionPart& part : mesh.parts) {
    geometryZero = geometryZero || (!part.geometries.empty() && !part.geometries[0].lods.empty());
  }
  if (!geometryZero) {
    for (CollisionPart& part : mesh.parts) {
      if (!part.geometries.empty()) part.geometries.erase(part.geometries.begin());
    }
  }
  return mesh;
}

}  // namespace obf2::mesh
