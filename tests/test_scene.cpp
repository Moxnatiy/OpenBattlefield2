#include <map>
#include <string>

#include "check.h"
#include "obf2/game/scene.h"

using namespace obf2;

namespace {

class MemoryFiles : public con::FileProvider {
 public:
  std::map<std::string, std::string> files;
  std::optional<std::string> loadText(std::string_view path) override {
    const auto it = files.find(std::string(path));
    return it == files.end() ? std::nullopt : std::optional<std::string>{it->second};
  }
};

game::Registry buildRegistry(const std::string& source) {
  MemoryFiles files;
  files.files["a.con"] = source;
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("a.con");
  return registry;
}

// Дерево за зразком реальної техніки: корпус -> башта -> ствол.
constexpr const char* kVehicle =
    "ObjectTemplate.create PlayerControlObject apc\n"
    "ObjectTemplate.geometry apc\n"
    "ObjectTemplate.addTemplate apc_turret\n"
    "ObjectTemplate.setPosition 0/1.6/1.0\n"
    "ObjectTemplate.create RotationalBundle apc_turret\n"
    "ObjectTemplate.geometryPart 2\n"
    "ObjectTemplate.addTemplate apc_barrel\n"
    "ObjectTemplate.setPosition 0/0.25/0.5\n"
    "ObjectTemplate.create RotationalBundle apc_barrel\n"
    "ObjectTemplate.geometryPart 3\n";

const game::PartPlacement* findPart(const game::ObjectInstance& instance, const char* name) {
  for (const auto& placement : instance.parts) {
    if (placement.templateName == name) return &placement;
  }
  return nullptr;
}

bool near(float value, float expected) { return value > expected - 0.001f && value < expected + 0.001f; }

}  // namespace

static void testTransformsAccumulate() {
  const game::Registry registry = buildRegistry(kVehicle);
  const auto instance = game::flattenObject(registry, "apc");
  CHECK(instance.has_value());
  if (!instance) return;

  CHECK_EQ(instance->geometryName, std::string("apc"));
  CHECK_EQ(instance->parts.size(), std::size_t(3));
  CHECK_EQ(instance->unresolved, 0);
  CHECK_EQ(instance->cycles, 0);
  CHECK_EQ(instance->maxDepth, 2);

  const auto* turret = findPart(*instance, "apc_turret");
  CHECK(turret != nullptr);
  if (turret != nullptr) {
    CHECK_EQ(turret->geometryPart, 2);
    CHECK(near(turret->transform.m[13], 1.6f));
    CHECK(near(turret->transform.m[14], 1.0f));
  }

  // Ствол має опинитися у сумі трансформів: 1.6+0.25 по Y, 1.0+0.5 по Z.
  const auto* barrel = findPart(*instance, "apc_barrel");
  CHECK(barrel != nullptr);
  if (barrel != nullptr) {
    CHECK_EQ(barrel->geometryPart, 3);
    CHECK(near(barrel->transform.m[13], 1.85f));
    CHECK(near(barrel->transform.m[14], 1.5f));
  }
}

static void testRotationAppliesToChildOffset() {
  // Поворот батька має повертати і зміщення нащадка, а не лише його власну
  // орієнтацію — інакше башта крутилася б на місці, а ствол лишався б збоку.
  const game::Registry registry = buildRegistry(
      "ObjectTemplate.create PlayerControlObject apc\n"
      "ObjectTemplate.addTemplate turret\n"
      "ObjectTemplate.setPosition 0/0/0\n"
      "ObjectTemplate.setRotation 90/0/0\n"
      "ObjectTemplate.create RotationalBundle turret\n"
      "ObjectTemplate.geometryPart 1\n"
      "ObjectTemplate.addTemplate barrel\n"
      "ObjectTemplate.setPosition 0/0/2\n"
      "ObjectTemplate.create RotationalBundle barrel\n"
      "ObjectTemplate.geometryPart 2\n");

  const auto instance = game::flattenObject(registry, "apc");
  CHECK(instance.has_value());
  if (!instance) return;

  // Поворот на 90° навколо Y переносить зміщення +2 по Z у +2 по X.
  const auto* barrel = findPart(*instance, "barrel");
  CHECK(barrel != nullptr);
  if (barrel != nullptr) {
    CHECK(near(barrel->transform.m[12], 2.0f));
    CHECK(near(barrel->transform.m[14], 0.0f));
  }
}

static void testApplyPartTransforms() {
  const game::Registry registry = buildRegistry(kVehicle);
  const auto instance = game::flattenObject(registry, "apc");
  CHECK(instance.has_value());
  if (!instance) return;

  const auto transforms = game::partTransformMap(*instance);
  CHECK_EQ(transforms.size(), std::size_t(2));  // корпус без geometryPart

  // Три вершини: корпус (частина 0), башта (2) і ствол (3).
  mesh::RenderMesh target;
  target.vertices = {
      mesh::Vertex{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      mesh::Vertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      mesh::Vertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
  };
  target.vertexPart = {0, 2, 3};
  target.indices = {0, 1, 2};

  const std::size_t moved = game::applyPartTransforms(target, transforms);
  CHECK_EQ(moved, std::size_t(2));  // корпус лишився на місці

  CHECK(near(target.vertices[0].position.x, 1.0f));  // частини 0 в мапі немає
  CHECK(near(target.vertices[1].position.y, 1.6f));
  CHECK(near(target.vertices[2].position.y, 1.85f));
  CHECK(near(target.vertices[2].position.z, 1.5f));

  // Габарити мають перерахуватися під складену геометрію.
  CHECK(near(target.bounds.max.y, 1.85f));
  CHECK(near(target.bounds.min.y, 0.0f));
}

static void testMismatchedPartArrayIsIgnored() {
  // Для static/skinned мешів vertexPart порожній — складати нічого.
  mesh::RenderMesh target;
  target.vertices.resize(3);
  std::unordered_map<int, Mat4> transforms;
  transforms[0] = translation(Vec3f{5.0f, 0.0f, 0.0f});
  CHECK_EQ(game::applyPartTransforms(target, transforms), std::size_t(0));
  CHECK(near(target.vertices[0].position.x, 0.0f));
}

static void testCycleGuard() {
  // Цикл A -> B -> A у даних не має розкручувати обхід до межі глибини.
  const game::Registry registry = buildRegistry(
      "ObjectTemplate.create Bundle a\n"
      "ObjectTemplate.addTemplate b\n"
      "ObjectTemplate.create Bundle b\n"
      "ObjectTemplate.addTemplate a\n");

  const auto instance = game::flattenObject(registry, "a");
  CHECK(instance.has_value());
  if (!instance) return;
  CHECK_EQ(instance->parts.size(), std::size_t(2));
  CHECK_EQ(instance->cycles, 1);
}

static void testRepeatedSiblingsAreKept() {
  // А от той самий шаблон у різних гілках — норма: шість однакових коліс.
  const game::Registry registry = buildRegistry(
      "ObjectTemplate.create PlayerControlObject car\n"
      "ObjectTemplate.addTemplate wheel\n"
      "ObjectTemplate.setPosition -1/0/1\n"
      "ObjectTemplate.addTemplate wheel\n"
      "ObjectTemplate.setPosition 1/0/1\n"
      "ObjectTemplate.create Bundle wheel\n"
      "ObjectTemplate.geometryPart 1\n");

  const auto instance = game::flattenObject(registry, "car");
  CHECK(instance.has_value());
  if (!instance) return;
  CHECK_EQ(instance->parts.size(), std::size_t(3));
  CHECK_EQ(instance->cycles, 0);
}

static void testUnknownRoot() {
  const game::Registry registry = buildRegistry(kVehicle);
  CHECK(!game::flattenObject(registry, "немаєТакого").has_value());
}

TEST_MAIN({
  testTransformsAccumulate();
  testRotationAppliesToChildOffset();
  testApplyPartTransforms();
  testMismatchedPartArrayIsIgnored();
  testCycleGuard();
  testRepeatedSiblingsAreKept();
  testUnknownRoot();
})
