#include "obf2/mesh/collision.h"

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

// The BSP is meant for fast lookup inside one mesh; we build our own index over
// the level, so the tree is merely skipped.
void skipBsp(Reader& reader) {
  reader.skip(12, "bsp.min");
  reader.skip(12, "bsp.max");

  const std::uint32_t nodeCount = reader.dword("bsp.nodeCount");
  if (nodeCount > reader.capacity<std::uint32_t>()) {
    reader.fail("implausible bsp node count");
    return;
  }
  // A node: a plane (float) plus three double words.
  reader.skip(static_cast<std::size_t>(nodeCount) * 16, "bsp.nodes");

  const std::uint32_t faceRefCount = reader.dword("bsp.faceRefCount");
  if (faceRefCount > reader.capacity<std::uint16_t>()) {
    reader.fail("implausible bsp reference count");
    return;
  }
  reader.skip(static_cast<std::size_t>(faceRefCount) * 2, "bsp.faceRefs");
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
  if (marker == '1') skipBsp(reader);

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
      for (std::uint32_t index = 0; index < layerCount && reader.ok(); ++index) {
        mesh.layers.push_back(readLayer(reader, mesh.versionMinor, index));
      }
    }
  }

  if (!reader.ok()) {
    if (error) *error = reader.error();
    return std::nullopt;
  }
  return mesh;
}

}  // namespace obf2::mesh
