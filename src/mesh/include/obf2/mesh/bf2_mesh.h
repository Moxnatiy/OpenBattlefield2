#pragma once
// Refractor 2 mesh formats: .staticmesh / .bundledmesh / .skinnedmesh.
// All three are one and the same container; the difference is a few parsing branches.
//
// The format's layout comes from Project Dalian (MIT, engine/formats/mesh) and
// BfMeshView; the implementation here is our own, with mandatory bounds checks —
// the files come from the user's archives and are not to be trusted.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obf2::mesh {

enum class Kind { Static, Bundled, Skinned };

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Aabb { Vec3 min, max; };
struct Mat4 { float m[16]{}; };

// The description of one vertex attribute. The usage values match D3DDECLUSAGE
// from DirectX 9: 0 = POSITION, 3 = NORMAL, 5 = TEXCOORD, 6 = TANGENT.
// flag != 0 means the attribute is disabled (255 occurs in the files).
struct VertexAttribute {
  std::uint16_t flag = 0;
  std::uint16_t offset = 0;   // byte offset within the vertex
  std::uint16_t vartype = 0;  // 0=float1 1=float2 2=float3 3=float4 4=d3dcolor
  std::uint16_t usage = 0;
};

struct Material {
  std::uint32_t alphaMode = 0;      // absent in skinned
  std::string fxFile;               // e.g. "StaticMesh.fx"
  std::string technique;            // e.g. "Base Detail"
  std::vector<std::string> maps;    // texture paths
  std::uint32_t vertexStart = 0;
  std::uint32_t indexStart = 0;
  std::uint32_t indexCount = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t nodeIndex = 0;      // static: an index into Lod::nodes
  Aabb bounds;
  bool hasBounds = false;           // version == 11 only, and not skinned
};

struct Bone { std::uint32_t id = 0; Mat4 transform; };
struct Rig { std::vector<Bone> bones; };

struct Lod {
  Vec3 min, max, pivot;
  std::vector<Rig> rigs;        // skinned only
  std::vector<Mat4> nodes;      // static; bundled has only the counter
  std::vector<Material> materials;
};

struct Geometry { std::vector<Lod> lods; };

struct Header {
  std::uint32_t u1 = 0;
  std::uint32_t version = 0;
  std::uint32_t u3 = 0, u4 = 0, u5 = 0;
};

struct Mesh {
  Header header;
  Kind kind = Kind::Static;
  bool isBfp4f = false;
  std::vector<Geometry> geometries;
  std::vector<VertexAttribute> attributes;
  std::uint32_t vertexFormat = 0;  // the component's size, always 4 (float)
  std::uint32_t vertexStride = 0;  // bytes per vertex
  std::uint32_t vertexCount = 0;
  std::vector<float> vertexData;   // the raw buffer, stride/format floats per vertex
  std::vector<std::uint16_t> indices;

  std::size_t floatsPerVertex() const {
    return vertexFormat == 0 ? 0 : vertexStride / vertexFormat;
  }
};

// Unpacked geometry, ready to upload to the GPU.
struct Vertex {
  Vec3 position;
  Vec3 normal;
  // TEXCOORD0: the base map's unwrap, unique per surface.
  float uv[2]{};
  // TEXCOORD1: the tiling set the detail map is sampled with. When the mesh has
  // only one set this is a copy of `uv`.
  float uv2[2]{};
  // TEXCOORD2: the unwrap the baked light map is sampled with, unique per
  // surface like the base map's but laid out for the level's atlas. Zero when
  // the mesh has no such set.
  float uv3[2]{};
};

// A range of indices sharing one material — one draw call.
struct DrawRange {
  std::uint32_t indexStart = 0;
  std::uint32_t indexCount = 0;
  std::string fxFile;
  std::string technique;
  std::vector<std::string> maps;
  // The material's `alphaMode`, straight from the file. It is what tells the
  // engine whether to alpha-test the surface: `AlphaTestEnable = <AlphaTest>`
  // in the technique (`Shaders_client.zip:RaShaderSTM.fx:583`) is a bool the
  // engine sets per material. Absent in skinned meshes, where it stays 0.
  std::uint32_t alphaMode = 0;

  // The rig number for skinning — it matches the material's number.
  int rig = -1;

  // For terrain the second slot is the baked lighting, while for ordinary meshes
  // it holds a detail texture the renderer does not use yet. The flag
  // distinguishes those cases instead of guessing from the file's name.
  bool lightmapInSecondSlot = false;
};

// A vertex's binding to the skeleton. BF2 takes exactly two bones: the vertex
// holds one weight and a pair of ids packed into a D3DCOLOR (the first two bytes).
// The ids are indices into the material's **rig**, not into the skeleton.
struct SkinBinding {
  std::uint8_t boneA = 0;
  std::uint8_t boneB = 0;
  float weight = 1.0f;  // the share for boneA; boneB gets the remaining 1 - weight
};

struct RenderMesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<DrawRange> ranges;
  Aabb bounds;

  // BundledMesh: the part index for every vertex (turret, barrel, wheels —
  // all in one buffer, each part in its own local coordinates).
  // Empty for static/skinned. Taken from the BLENDINDICES attribute, which is
  // stored as a D3DCOLOR — four bytes in one slot.
  std::vector<std::uint8_t> vertexPart;

  // SkinnedMesh: every vertex's binding, plus one rig per material
  // (their count always equals the number of materials).
  std::vector<SkinBinding> skin;
  std::vector<Rig> rigs;
};

// The type is decided by the file's extension — it cannot be told from the contents.
std::optional<Kind> kindFromExtension(std::string_view extension);
std::string_view kindName(Kind kind);

// nullopt plus an error string instead of an exception: a damaged mesh is an
// expected situation, not an exceptional one.
std::optional<Mesh> load(std::span<const std::byte> bytes, Kind kind, std::string* error = nullptr);

std::optional<RenderMesh> extract(const Mesh& mesh, std::size_t geometryIndex = 0,
                                  std::size_t lodIndex = 0, std::string* error = nullptr);

}  // namespace obf2::mesh
