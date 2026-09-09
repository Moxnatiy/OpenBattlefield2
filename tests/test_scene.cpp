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

// A tree modelled on a real vehicle: hull -> turret -> barrel.
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

  // The barrel has to end up in the sum of the transforms: 1.6+0.25 along Y, 1.0+0.5 along Z.
  const auto* barrel = findPart(*instance, "apc_barrel");
  CHECK(barrel != nullptr);
  if (barrel != nullptr) {
    CHECK_EQ(barrel->geometryPart, 3);
    CHECK(near(barrel->transform.m[13], 1.85f));
    CHECK(near(barrel->transform.m[14], 1.5f));
  }
}

static void testRotationAppliesToChildOffset() {
  // The parent's rotation has to rotate the child's offset too, not only its own
  // orientation — otherwise the turret would spin in place while the barrel stayed aside.
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

  // A 90° rotation about Y turns an offset of +2 along Z into +2 along X.
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
  CHECK_EQ(transforms.size(), std::size_t(2));  // the hull has no geometryPart

  // Three vertices: the hull (part 0), the turret (2) and the barrel (3).
  mesh::RenderMesh target;
  target.vertices = {
      mesh::Vertex{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      mesh::Vertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      mesh::Vertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
  };
  target.vertexPart = {0, 2, 3};
  target.indices = {0, 1, 2};

  const std::size_t moved = game::applyPartTransforms(target, transforms);
  CHECK_EQ(moved, std::size_t(2));  // the hull stayed in place

  CHECK(near(target.vertices[0].position.x, 1.0f));  // part 0 is not in the map
  CHECK(near(target.vertices[1].position.y, 1.6f));
  CHECK(near(target.vertices[2].position.y, 1.85f));
  CHECK(near(target.vertices[2].position.z, 1.5f));

  // The bounds have to be recomputed for the assembled geometry.
  CHECK(near(target.bounds.max.y, 1.85f));
  CHECK(near(target.bounds.min.y, 0.0f));
}

static void testMismatchedPartArrayIsIgnored() {
  // For static/skinned meshes vertexPart is empty — there is nothing to assemble.
  mesh::RenderMesh target;
  target.vertices.resize(3);
  std::unordered_map<int, Mat4> transforms;
  transforms[0] = translation(Vec3f{5.0f, 0.0f, 0.0f});
  CHECK_EQ(game::applyPartTransforms(target, transforms), std::size_t(0));
  CHECK(near(target.vertices[0].position.x, 0.0f));
}

static void testCycleGuard() {
  // A cycle A -> B -> A in the data must not spin the walk out to the depth limit.
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
  // The same template in different branches, on the other hand, is normal: six identical wheels.
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
  CHECK(!game::flattenObject(registry, "noSuchThing").has_value());
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
