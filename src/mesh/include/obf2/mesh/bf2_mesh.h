#pragma once
// Формати мешів Refractor 2: .staticmesh / .bundledmesh / .skinnedmesh.
// Усі три — один і той самий контейнер, різниця лише в кількох гілках парсингу.
//
// Розкладка формату взята з Project Dalian (MIT, engine/formats/mesh) та
// BfMeshView; реалізація тут своя, з обов'язковою перевіркою меж — файли
// приходять з архівів користувача і довіри їм нема.
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

// Опис одного атрибута вершини. Значення usage збігаються з D3DDECLUSAGE
// часів DirectX 9: 0 = POSITION, 3 = NORMAL, 5 = TEXCOORD, 6 = TANGENT.
// flag != 0 означає, що атрибут вимкнений (у файлах трапляється 255).
struct VertexAttribute {
  std::uint16_t flag = 0;
  std::uint16_t offset = 0;   // байтовий зсув усередині вершини
  std::uint16_t vartype = 0;  // 0=float1 1=float2 2=float3 3=float4 4=d3dcolor
  std::uint16_t usage = 0;
};

struct Material {
  std::uint32_t alphaMode = 0;      // немає у skinned
  std::string fxFile;               // напр. "StaticMesh.fx"
  std::string technique;            // напр. "Base Detail"
  std::vector<std::string> maps;    // шляхи текстур
  std::uint32_t vertexStart = 0;
  std::uint32_t indexStart = 0;
  std::uint32_t indexCount = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t nodeIndex = 0;      // static: індекс у Lod::nodes
  Aabb bounds;
  bool hasBounds = false;           // тільки version == 11 і не skinned
};

struct Bone { std::uint32_t id = 0; Mat4 transform; };
struct Rig { std::vector<Bone> bones; };

struct Lod {
  Vec3 min, max, pivot;
  std::vector<Rig> rigs;        // тільки skinned
  std::vector<Mat4> nodes;      // static; bundled має лише лічильник
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
  std::uint32_t vertexFormat = 0;  // розмір компонента, завжди 4 (float)
  std::uint32_t vertexStride = 0;  // байтів на вершину
  std::uint32_t vertexCount = 0;
  std::vector<float> vertexData;   // «сирий» буфер, stride/format float-ів на вершину
  std::vector<std::uint16_t> indices;

  std::size_t floatsPerVertex() const {
    return vertexFormat == 0 ? 0 : vertexStride / vertexFormat;
  }
};

// Розпакована геометрія, готова до завантаження в GPU.
struct Vertex {
  Vec3 position;
  Vec3 normal;
  float uv[2]{};
};

// Діапазон індексів з однаковим матеріалом — один виклик малювання.
struct DrawRange {
  std::uint32_t indexStart = 0;
  std::uint32_t indexCount = 0;
  std::string fxFile;
  std::string technique;
  std::vector<std::string> maps;
};

struct RenderMesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<DrawRange> ranges;
  Aabb bounds;

  // BundledMesh: індекс частини для кожної вершини (башта, ствол, колеса —
  // усе в одному буфері, кожна частина у власних локальних координатах).
  // Порожній для static/skinned. Береться з атрибута BLENDINDICES, який
  // зберігається як D3DCOLOR — чотири байти в одному слоті.
  std::vector<std::uint8_t> vertexPart;
};

// Тип визначається розширенням файлу — інакше його з вмісту не дізнатися.
std::optional<Kind> kindFromExtension(std::string_view extension);
std::string_view kindName(Kind kind);

// nullopt + текст помилки замість винятку: пошкоджений меш — очікувана ситуація,
// а не виняткова.
std::optional<Mesh> load(std::span<const std::byte> bytes, Kind kind, std::string* error = nullptr);

std::optional<RenderMesh> extract(const Mesh& mesh, std::size_t geometryIndex = 0,
                                  std::size_t lodIndex = 0, std::string* error = nullptr);

}  // namespace obf2::mesh
