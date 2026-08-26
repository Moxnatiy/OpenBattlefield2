#include "obf2/mesh/bf2_mesh.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace obf2::mesh {
namespace {

// Читач із перевіркою меж. Будь-яке читання за межі буфера переводить його в
// стан помилки назавжди — далі всі читання просто нічого не роблять, тож
// парсер можна писати лінійно, без перевірки після кожного кроку.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  bool ok() const { return ok_; }
  std::size_t offset() const { return pos_; }
  std::size_t remaining() const { return ok_ ? data_.size() - pos_ : 0; }

  void fail(std::string why) {
    if (ok_) {
      ok_ = false;
      error_ = std::move(why) + " (зсув " + std::to_string(pos_) + ")";
    }
  }
  const std::string& error() const { return error_; }

  template <typename T>
  T read(const char* what) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    if (!ok_) return value;
    if (data_.size() - pos_ < sizeof(T)) {
      fail(std::string("файл обірвано: ") + what);
      return value;
    }
    std::memcpy(&value, data_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return value;
  }

  // Читання масиву з попередньою перевіркою, що він узагалі влазить у файл.
  // Це головний захист від зіпсованих лічильників: 4 мільярди вершин не мають
  // спричиняти спробу виділити 100 ГБ.
  template <typename T>
  bool readArray(T* dst, std::size_t count, const char* what) {
    if (!ok_) return false;
    if (count == 0) return true;
    if (count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
      fail(std::string("нереальний розмір: ") + what);
      return false;
    }
    const std::size_t bytes = count * sizeof(T);
    if (data_.size() - pos_ < bytes) {
      fail(std::string("файл обірвано: ") + what);
      return false;
    }
    std::memcpy(dst, data_.data() + pos_, bytes);
    pos_ += bytes;
    return true;
  }

  // Скільки елементів розміру T ще фізично може бути у файлі.
  template <typename T>
  std::size_t capacity() const {
    return ok_ ? (data_.size() - pos_) / sizeof(T) : 0;
  }

  // Рядок: uint32 довжина + байти без термінатора.
  std::string readString(const char* what) {
    const auto length = read<std::uint32_t>(what);
    if (!ok_) return {};
    if (length > remaining()) {
      fail(std::string("довжина рядка більша за файл: ") + what);
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
  if (mapCount > r.remaining()) {  // кожен запис — щонайменше 4 байти довжини
    r.fail("нереальна кількість текстур матеріалу");
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
    if (rigCount > r.remaining()) { r.fail("нереальна кількість rig"); return; }
    lod.rigs.resize(rigCount);
    for (auto& rig : lod.rigs) {
      const auto boneCount = r.read<std::uint32_t>("rig.boneCount");
      if (boneCount > r.capacity<Bone>()) { r.fail("нереальна кількість кісток"); return; }
      rig.bones.resize(boneCount);
      r.readArray(rig.bones.data(), boneCount, "rig.bones");
    }
    return;
  }

  const auto nodeCount = r.read<std::uint32_t>("lod.nodeCount");
  // BundledMesh пише лічильник, але самих матриць не зберігає — трансформи
  // частин лежать у .con (geometryPart), а не в меші.
  if (kind == Kind::Bundled) return;
  if (nodeCount > r.capacity<Mat4>()) { r.fail("нереальна кількість вузлів"); return; }
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

  // Маркер гри: 1 = Battlefield Play4Free, у якого кілька полів зсунуто.
  mesh.isBfp4f = r.read<std::uint8_t>("gameMarker") == 1;

  const auto geometryCount = r.read<std::uint32_t>("geometryCount");
  if (geometryCount > r.remaining()) { r.fail("нереальна кількість geom"); }
  if (r.ok()) {
    mesh.geometries.resize(geometryCount);
    for (auto& geometry : mesh.geometries) {
      const auto lodCount = r.read<std::uint32_t>("geometry.lodCount");
      if (lodCount > r.remaining()) { r.fail("нереальна кількість lod"); break; }
      geometry.lods.resize(lodCount);  // вміст читається наприкінці файлу
    }
  }

  const auto attributeCount = r.read<std::uint32_t>("attributeCount");
  if (attributeCount > r.capacity<VertexAttribute>()) r.fail("нереальна кількість атрибутів");
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
      r.fail("некоректний формат вершини");
    } else if (mesh.vertexCount > r.remaining() / mesh.vertexStride) {
      r.fail("вершинний буфер не влазить у файл");
    } else {
      mesh.vertexData.resize(static_cast<std::size_t>(mesh.vertexCount) * mesh.floatsPerVertex());
      r.readArray(mesh.vertexData.data(), mesh.vertexData.size(), "vertexData");
    }
  }

  const auto indexCount = r.read<std::uint32_t>("indexCount");
  if (indexCount > r.capacity<std::uint16_t>()) r.fail("індексний буфер не влазить у файл");
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
      if (materialCount > r.remaining()) { r.fail("нереальна кількість матеріалів"); break; }
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

  if (geometryIndex >= mesh.geometries.size()) return fail("немає такого geom");
  const Geometry& geometry = mesh.geometries[geometryIndex];
  if (lodIndex >= geometry.lods.size()) return fail("немає такого lod");
  const Lod& lod = geometry.lods[lodIndex];

  const std::size_t stride = mesh.floatsPerVertex();
  if (stride == 0) return fail("нульовий stride вершини");

  // Зсуви потрібних каналів. Беремо перший TEXCOORD: у BF2 їх до трьох
  // (база, детейл, лайтмапа), і для геометрії досить нульового.
  bool hasPosition = false, hasNormal = false, hasUv = false, hasPart = false;
  std::size_t positionFloat = 0, normalFloat = 0, uvFloat = 0, partFloat = 0;
  std::size_t weightFloat = 0;
  bool hasWeight = false;
  for (const VertexAttribute& attribute : mesh.attributes) {
    if (attribute.flag != 0) continue;  // 255 = канал вимкнено
    const std::size_t index = attribute.offset / sizeof(float);
    // usage кодується як (номер каналу << 8) | призначення, тому TEXCOORD1
    // це 0x105, а TEXCOORD2 (лайтмапа) — 0x205. Нам треба нульовий.
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
  if (!hasPosition) return fail("у меші немає POSITION");

  RenderMesh out;
  out.bounds = Aabb{lod.min, lod.max};
  out.vertices.resize(mesh.vertexCount);
  if (hasPart && mesh.kind == Kind::Bundled) out.vertexPart.resize(mesh.vertexCount);
  // Скінінг: пара кісток і вага. Риґи копіюємо як є — вони прив'язують
  // номери в риґу до номерів кісток скелета.
  if (mesh.kind == Kind::Skinned && hasPart && hasWeight) {
    out.skin.resize(mesh.vertexCount);
    out.rigs = lod.rigs;
  }

  for (std::uint32_t i = 0; i < mesh.vertexCount; ++i) {
    const std::size_t base = static_cast<std::size_t>(i) * stride;
    Vertex& vertex = out.vertices[i];

    if (!out.vertexPart.empty() && base + partFloat < mesh.vertexData.size()) {
      // D3DCOLOR: чотири байти, запхані у float-слот. Номер частини лежить
      // у молодшому байті; старший використовується під анімовані UV.
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

  // Індекси в матеріалі відлічуються від його vertexStart, тому зводимо все
  // до одного плоского буфера з абсолютними індексами.
  for (std::size_t materialIndex = 0; materialIndex < lod.materials.size(); ++materialIndex) {
    const Material& material = lod.materials[materialIndex];
    if (material.indexCount == 0) continue;
    if (material.indexStart > mesh.indices.size() ||
        mesh.indices.size() - material.indexStart < material.indexCount) {
      continue;  // зіпсований діапазон — пропускаємо матеріал, не весь меш
    }

    DrawRange range;
    range.indexStart = static_cast<std::uint32_t>(out.indices.size());
    range.indexCount = material.indexCount;
    // Риґи йдуть по одному на матеріал — це видно на всіх скелетних мешах
    // гри: кількість риґів у lod завжди дорівнює кількості матеріалів.
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
