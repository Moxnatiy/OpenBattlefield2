#include "obf2/mesh/bf2_mesh.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace obf2::mesh {
namespace {

// A bounds-checked reader. Any read past the end of the buffer puts it into an
// error state for good — after that every read simply does nothing, so the
// parser can be written linearly, without a check after every step.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  bool ok() const { return ok_; }
  std::size_t offset() const { return pos_; }
  std::size_t remaining() const { return ok_ ? data_.size() - pos_ : 0; }

  void fail(std::string why) {
    if (ok_) {
      ok_ = false;
      error_ = std::move(why) + " (offset " + std::to_string(pos_) + ")";
    }
  }
  const std::string& error() const { return error_; }

  template <typename T>
  T read(const char* what) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    if (!ok_) return value;
    if (data_.size() - pos_ < sizeof(T)) {
      fail(std::string("file truncated: ") + what);
      return value;
    }
    std::memcpy(&value, data_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return value;
  }

  // An array read with a prior check that it fits into the file at all.
  // This is the main defence against corrupt counters: 4 billion vertices must
  // not cause an attempt to allocate 100 GB.
  template <typename T>
  bool readArray(T* dst, std::size_t count, const char* what) {
    if (!ok_) return false;
    if (count == 0) return true;
    if (count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
      fail(std::string("implausible size: ") + what);
      return false;
    }
    const std::size_t bytes = count * sizeof(T);
    if (data_.size() - pos_ < bytes) {
      fail(std::string("file truncated: ") + what);
      return false;
    }
    std::memcpy(dst, data_.data() + pos_, bytes);
    pos_ += bytes;
    return true;
  }

  // How many elements of size T can still physically be in the file.
  template <typename T>
  std::size_t capacity() const {
    return ok_ ? (data_.size() - pos_) / sizeof(T) : 0;
  }

  // A string: uint32 length + bytes, with no terminator.
  std::string readString(const char* what) {
    const auto length = read<std::uint32_t>(what);
    if (!ok_) return {};
    if (length > remaining()) {
      fail(std::string("string length exceeds the file: ") + what);
      return {};
    }
    std::string out(reinterpret_cast<const char*>(data_.data() + pos_), length);
    pos_ += length;
    return out;
  }

 private:
  std::span<const std::byte> data_;
  std::size_t pos_ = 0;
  bool ok_ = true;
  std::string error_;
};

Material readMaterial(Reader& r, std::uint32_t version, Kind kind) {
  Material material;
  if (kind != Kind::Skinned) material.alphaMode = r.read<std::uint32_t>("material.alphaMode");

  material.fxFile = r.readString("material.fxFile");
  material.technique = r.readString("material.technique");

  const auto mapCount = r.read<std::uint32_t>("material.mapCount");
  if (mapCount > r.remaining()) {  // every entry is at least the 4 bytes of a length
    r.fail("implausible material texture count");
    return material;
  }
  material.maps.reserve(std::min<std::size_t>(mapCount, 64));
  for (std::uint32_t i = 0; i < mapCount && r.ok(); ++i) {
    material.maps.push_back(r.readString("material.map"));
  }

  material.vertexStart = r.read<std::uint32_t>("material.vertexStart");
  material.indexStart = r.read<std::uint32_t>("material.indexStart");
  material.indexCount = r.read<std::uint32_t>("material.indexCount");
  material.vertexCount = r.read<std::uint32_t>("material.vertexCount");

  material.nodeIndex = r.read<std::uint32_t>("material.nodeIndex");
  r.read<std::uint16_t>("material.u5");
  r.read<std::uint16_t>("material.u6");

  if (kind != Kind::Skinned && version == 11) {
    r.readArray(&material.bounds, 1, "material.bounds");
    material.hasBounds = r.ok();
  }
  return material;
}

void readLodNodes(Reader& r, Lod& lod, std::uint32_t version, Kind kind) {
  r.readArray(&lod.min, 1, "lod.min");
  r.readArray(&lod.max, 1, "lod.max");
  if (version <= 6) r.readArray(&lod.pivot, 1, "lod.pivot");

  if (kind == Kind::Skinned) {
    const auto rigCount = r.read<std::uint32_t>("lod.rigCount");
    if (rigCount > r.remaining()) { r.fail("implausible rig count"); return; }
    lod.rigs.resize(rigCount);
    for (auto& rig : lod.rigs) {
      const auto boneCount = r.read<std::uint32_t>("rig.boneCount");
      if (boneCount > r.capacity<Bone>()) { r.fail("implausible bone count"); return; }
      rig.bones.resize(boneCount);
      r.readArray(rig.bones.data(), boneCount, "rig.bones");
    }
    return;
  }

  const auto nodeCount = r.read<std::uint32_t>("lod.nodeCount");
  // A BundledMesh writes the counter but stores no matrices — the parts'
  // transforms live in the .con (geometryPart), not in the mesh.
  if (kind == Kind::Bundled) return;
  if (nodeCount > r.capacity<Mat4>()) { r.fail("implausible node count"); return; }
  lod.nodes.resize(nodeCount);
  r.readArray(lod.nodes.data(), nodeCount, "lod.nodes");
}

}  // namespace

std::optional<Kind> kindFromExtension(std::string_view extension) {
  if (extension == "staticmesh") return Kind::Static;
  if (extension == "bundledmesh") return Kind::Bundled;
  if (extension == "skinnedmesh") return Kind::Skinned;
  return std::nullopt;
}

std::string_view kindName(Kind kind) {
  switch (kind) {
    case Kind::Static: return "staticmesh";
    case Kind::Bundled: return "bundledmesh";
    case Kind::Skinned: return "skinnedmesh";
  }
  return "?";
}

std::optional<Mesh> load(std::span<const std::byte> bytes, Kind kind, std::string* error) {
  Reader r(bytes);
  Mesh mesh;
  mesh.kind = kind;

  r.readArray(&mesh.header, 1, "header");

  // The game marker: 1 = Battlefield Play4Free, which has several fields shifted.
  mesh.isBfp4f = r.read<std::uint8_t>("gameMarker") == 1;

  const auto geometryCount = r.read<std::uint32_t>("geometryCount");
  if (geometryCount > r.remaining()) { r.fail("implausible geom count"); }
  if (r.ok()) {
    mesh.geometries.resize(geometryCount);
    for (auto& geometry : mesh.geometries) {
      const auto lodCount = r.read<std::uint32_t>("geometry.lodCount");
      if (lodCount > r.remaining()) { r.fail("implausible lod count"); break; }
      geometry.lods.resize(lodCount);  // the contents are read at the end of the file
    }
  }

  const auto attributeCount = r.read<std::uint32_t>("attributeCount");
  if (attributeCount > r.capacity<VertexAttribute>()) r.fail("implausible attribute count");
  if (r.ok()) {
    mesh.attributes.resize(attributeCount);
    r.readArray(mesh.attributes.data(), attributeCount, "attributes");
  }

  mesh.vertexFormat = r.read<std::uint32_t>("vertexFormat");
  mesh.vertexStride = r.read<std::uint32_t>("vertexStride");
  mesh.vertexCount = r.read<std::uint32_t>("vertexCount");

  if (r.ok()) {
    if (mesh.vertexFormat == 0 || mesh.vertexStride == 0 ||
        mesh.vertexStride % mesh.vertexFormat != 0) {
      r.fail("malformed vertex format");
    } else if (mesh.vertexCount > r.remaining() / mesh.vertexStride) {
      r.fail("the vertex buffer does not fit into the file");
    } else {
      mesh.vertexData.resize(static_cast<std::size_t>(mesh.vertexCount) * mesh.floatsPerVertex());
      r.readArray(mesh.vertexData.data(), mesh.vertexData.size(), "vertexData");
    }
  }

  const auto indexCount = r.read<std::uint32_t>("indexCount");
  if (indexCount > r.capacity<std::uint16_t>()) r.fail("the index buffer does not fit into the file");
  if (r.ok()) {
    mesh.indices.resize(indexCount);
    r.readArray(mesh.indices.data(), indexCount, "indices");
  }

  if (kind != Kind::Skinned) r.read<std::uint32_t>("u2");

  for (auto& geometry : mesh.geometries) {
    for (auto& lod : geometry.lods) readLodNodes(r, lod, mesh.header.version, kind);
  }
  for (auto& geometry : mesh.geometries) {
    for (auto& lod : geometry.lods) {
      const auto materialCount = r.read<std::uint32_t>("lod.materialCount");
      if (materialCount > r.remaining()) { r.fail("implausible material count"); break; }
      lod.materials.resize(materialCount);
      for (auto& material : lod.materials) {
        material = readMaterial(r, mesh.header.version, kind);
        if (!r.ok()) break;
      }
    }
  }

  if (!r.ok()) {
    if (error) *error = r.error();
    return std::nullopt;
  }
  return mesh;
}

std::optional<RenderMesh> extract(const Mesh& mesh, std::size_t geometryIndex,
                                  std::size_t lodIndex, std::string* error) {
  auto fail = [error](const char* why) -> std::optional<RenderMesh> {
    if (error) *error = why;
    return std::nullopt;
  };

  if (geometryIndex >= mesh.geometries.size()) return fail("no such geom");
  const Geometry& geometry = mesh.geometries[geometryIndex];
  if (lodIndex >= geometry.lods.size()) return fail("no such lod");
  const Lod& lod = geometry.lods[lodIndex];

  const std::size_t stride = mesh.floatsPerVertex();
  if (stride == 0) return fail("zero vertex stride");

  // The offsets of the channels we need. We take the first TEXCOORD: BF2 has up
  // to three (base, detail, light map), and geometry needs only the zeroth.
  bool hasPosition = false, hasNormal = false, hasUv = false, hasPart = false;
  std::size_t positionFloat = 0, normalFloat = 0, uvFloat = 0, partFloat = 0;
  std::size_t weightFloat = 0;
  bool hasWeight = false;
  for (const VertexAttribute& attribute : mesh.attributes) {
    if (attribute.flag != 0) continue;  // 255 = the channel is disabled
    const std::size_t index = attribute.offset / sizeof(float);
    // usage is encoded as (channel number << 8) | purpose, so TEXCOORD1 is
    // 0x105 and TEXCOORD2 (the light map) is 0x205. We want the zeroth.
    switch (attribute.usage) {
      case 0: positionFloat = index; hasPosition = true; break;
      case 1: weightFloat = index; hasWeight = true; break;
      case 2: partFloat = index; hasPart = true; break;
      case 3: normalFloat = index; hasNormal = true; break;
      case 5:
        if (!hasUv) { uvFloat = index; hasUv = true; }
        break;
      default: break;
    }
  }
  if (!hasPosition) return fail("the mesh has no POSITION");

  RenderMesh out;
  out.bounds = Aabb{lod.min, lod.max};
  out.vertices.resize(mesh.vertexCount);
  if (hasPart && mesh.kind == Kind::Bundled) out.vertexPart.resize(mesh.vertexCount);
  // Skinning: a pair of bones and a weight. The rigs are copied as they are —
  // they bind rig indices to the skeleton's bone indices.
  if (mesh.kind == Kind::Skinned && hasPart && hasWeight) {
    out.skin.resize(mesh.vertexCount);
    out.rigs = lod.rigs;
  }

  for (std::uint32_t i = 0; i < mesh.vertexCount; ++i) {
    const std::size_t base = static_cast<std::size_t>(i) * stride;
    Vertex& vertex = out.vertices[i];

    if (!out.vertexPart.empty() && base + partFloat < mesh.vertexData.size()) {
      // D3DCOLOR: four bytes stuffed into a float slot. The part number sits in
      // the low byte; the high one is used for animated UVs.
      std::uint32_t packed = 0;
      std::memcpy(&packed, &mesh.vertexData[base + partFloat], sizeof(packed));
      out.vertexPart[i] = static_cast<std::uint8_t>(packed & 0xFFu);
    }

    if (!out.skin.empty() && base + partFloat < mesh.vertexData.size() &&
        base + weightFloat < mesh.vertexData.size()) {
      std::uint32_t packed = 0;
      std::memcpy(&packed, &mesh.vertexData[base + partFloat], sizeof(packed));
      out.skin[i].boneA = static_cast<std::uint8_t>(packed & 0xFFu);
      out.skin[i].boneB = static_cast<std::uint8_t>((packed >> 8) & 0xFFu);
      out.skin[i].weight = mesh.vertexData[base + weightFloat];
    }

    if (base + positionFloat + 2 < mesh.vertexData.size()) {
      vertex.position = {mesh.vertexData[base + positionFloat],
                         mesh.vertexData[base + positionFloat + 1],
                         mesh.vertexData[base + positionFloat + 2]};
    }
    if (hasNormal && base + normalFloat + 2 < mesh.vertexData.size()) {
      vertex.normal = {mesh.vertexData[base + normalFloat],
                       mesh.vertexData[base + normalFloat + 1],
                       mesh.vertexData[base + normalFloat + 2]};
    }
    if (hasUv && base + uvFloat + 1 < mesh.vertexData.size()) {
      vertex.uv[0] = mesh.vertexData[base + uvFloat];
      vertex.uv[1] = mesh.vertexData[base + uvFloat + 1];
    }
  }

  // A material's indices are counted from its vertexStart, so everything is
  // reduced to one flat buffer with absolute indices.
  for (std::size_t materialIndex = 0; materialIndex < lod.materials.size(); ++materialIndex) {
    const Material& material = lod.materials[materialIndex];
    if (material.indexCount == 0) continue;
    if (material.indexStart > mesh.indices.size() ||
        mesh.indices.size() - material.indexStart < material.indexCount) {
      continue;  // a corrupt range — skip the material, not the whole mesh
    }

    DrawRange range;
    range.indexStart = static_cast<std::uint32_t>(out.indices.size());
    range.indexCount = material.indexCount;
    // The rigs come one per material — that is visible on every skinned mesh in
    // the game: a lod's rig count always equals its material count.
    if (!out.rigs.empty() && materialIndex < out.rigs.size()) {
      range.rig = static_cast<int>(materialIndex);
    }
    range.fxFile = material.fxFile;
    range.technique = material.technique;
    range.maps = material.maps;

    for (std::uint32_t i = 0; i < material.indexCount; ++i) {
      const std::uint32_t absolute =
          material.vertexStart + mesh.indices[material.indexStart + i];
      if (absolute >= out.vertices.size()) { range.indexCount = i; break; }
      out.indices.push_back(absolute);
    }
    if (range.indexCount > 0) out.ranges.push_back(std::move(range));
  }

  return out;
}

}  // namespace obf2::mesh
