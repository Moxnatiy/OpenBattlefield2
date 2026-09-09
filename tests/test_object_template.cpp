#include <map>
#include <string>

#include "check.h"
#include "obf2/game/object_template.h"

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

game::Registry runInto(MemoryFiles& files, const std::string& entry) {
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile(entry);
  return registry;
}

}  // namespace

static void testCreateAndProperties() {
  MemoryFiles files;
  files.files["a.con"] =
      "ObjectTemplate.create GenericFireArm ammokit\n"
      "ObjectTemplate.saveInSeparateFile 1\n"
      "ObjectTemplate.geometry ammokit\n"
      "ObjectTemplate.mapMaterial 0 Collision_Material 0\n";

  const game::Registry registry = runInto(files, "a.con");
  CHECK_EQ(registry.size(), std::size_t(1));
  CHECK_EQ(registry.stats().created, 1);

  const auto* object = registry.find("AMMOKIT");  // the lookup is case-insensitive
  CHECK(object != nullptr);
  if (object == nullptr) return;

  CHECK_EQ(object->className, std::string("GenericFireArm"));
  CHECK_EQ(object->text("geometry"), std::string_view("ammokit"));
  CHECK_EQ(object->text("Geometry"), std::string_view("ammokit"));
  CHECK(object->property("noSuchThing") == nullptr);
}

static void testTweakReopensTemplate() {
  // The game's main scenario: a .con creates a template, a .tweak reopens it
  // through activeSafe and adds to or overwrites its properties.
  MemoryFiles files;
  files.files["a.con"] =
      "ObjectTemplate.create GenericFireArm ammokit\n"
      "ObjectTemplate.geometry ammokit\n"
      "include a.tweak\n";
  files.files["a.tweak"] =
      "ObjectTemplate.activeSafe GenericFireArm ammokit\n"
      "ObjectTemplate.castsDynamicShadow 1\n"
      "ObjectTemplate.geometry ammokit_hi\n";

  const game::Registry registry = runInto(files, "a.con");
  CHECK_EQ(registry.size(), std::size_t(1));  // one template, not two
  CHECK_EQ(registry.stats().created, 1);
  CHECK_EQ(registry.stats().reopened, 1);

  const auto* object = registry.find("ammokit");
  CHECK(object != nullptr);
  if (object == nullptr) return;

  // The last assignment applies — the one from the .tweak.
  CHECK_EQ(object->text("geometry"), std::string_view("ammokit_hi"));
  CHECK_EQ(object->text("castsDynamicShadow"), std::string_view("1"));
  // But the history is kept: it is visible that the property was overwritten and from where.
  const auto* history = object->propertyHistory("geometry");
  CHECK(history != nullptr);
  if (history != nullptr) {
    CHECK_EQ(history->size(), std::size_t(2));
    CHECK_EQ((*history)[0].file, std::string("a.con"));
    CHECK_EQ((*history)[1].file, std::string("a.tweak"));
  }
}

static void testAccumulatingProperties() {
  // mapMaterial is called several times per template — every value has to remain
  // rather than overwrite the others.
  MemoryFiles files;
  files.files["a.con"] =
      "ObjectTemplate.create PlayerControlObject apc\n"
      "ObjectTemplate.mapMaterial 0 Armour 0\n"
      "ObjectTemplate.mapMaterial 1 glass 0\n"
      "ObjectTemplate.mapMaterial 2 cloth_penetrable 0\n";

  const game::Registry registry = runInto(files, "a.con");
  const auto* object = registry.find("apc");
  CHECK(object != nullptr);
  if (object == nullptr) return;

  const auto* history = object->propertyHistory("mapMaterial");
  CHECK(history != nullptr);
  if (history != nullptr) {
    CHECK_EQ(history->size(), std::size_t(3));
    CHECK_EQ((*history)[1].value(1), std::string_view("glass"));
  }
}

static void testComponents() {
  MemoryFiles files;
  files.files["a.con"] =
      "ObjectTemplate.create GenericFireArm ammokit\n"
      "ObjectTemplate.createComponent ReplenishingAmmoComp\n"
      "ObjectTemplate.ammo.magSize 1\n"
      "ObjectTemplate.ammo.reloadTime 0.7\n"
      "ObjectTemplate.weaponHud.hudName WEAPON_NAME_ammobag\n";

  const game::Registry registry = runInto(files, "a.con");
  const auto* object = registry.find("ammokit");
  CHECK(object != nullptr);
  if (object == nullptr) return;

  // createComponent and the reference ObjectTemplate.<something>.<property> are
  // two different ways of making a sub-object, and both have to work.
  CHECK_EQ(object->components.size(), std::size_t(3));

  const auto* ammo = object->component("Ammo");  // the case does not matter
  CHECK(ammo != nullptr);
  if (ammo != nullptr) {
    CHECK_EQ(ammo->properties.size(), std::size_t(2));
    const auto it = ammo->properties.find("reloadtime");
    CHECK(it != ammo->properties.end());
    if (it != ammo->properties.end()) {
      const auto reload = it->second.back().asFloat();
      CHECK(reload.has_value() && *reload > 0.69f && *reload < 0.71f);
    }
  }
  CHECK(object->component("weaponHud") != nullptr);
}

static void testChildTemplatesWithPositions() {
  // A vehicle's hierarchy: setPosition after addTemplate applies to the LAST child
  // added, not to the template itself.
  MemoryFiles files;
  files.files["a.con"] =
      "ObjectTemplate.create PlayerControlObject apc\n"
      "ObjectTemplate.addTemplate apc_hudPass\n"
      "ObjectTemplate.setPosition 0/0.0763/0\n"
      "ObjectTemplate.addTemplate apc_Turret\n"
      "ObjectTemplate.setPosition 0/1.6324/0.9962\n"
      "ObjectTemplate.setRotation 0/0/90\n"
      "ObjectTemplate.create RotationalBundle apc_Turret\n"
      "ObjectTemplate.geometryPart 2\n";

  const game::Registry registry = runInto(files, "a.con");
  CHECK_EQ(registry.size(), std::size_t(2));

  const auto* apc = registry.find("apc");
  CHECK(apc != nullptr);
  if (apc == nullptr) return;

  CHECK_EQ(apc->children.size(), std::size_t(2));
  CHECK_EQ(apc->children[0].name, std::string("apc_hudPass"));
  CHECK(apc->children[0].hasPosition);
  CHECK(!apc->children[0].hasRotation);
  CHECK(apc->children[1].position.y > 1.63f && apc->children[1].position.y < 1.64f);
  CHECK(apc->children[1].hasRotation);

  // A child's position must not settle as the parent's property.
  CHECK(apc->property("setPosition") == nullptr);

  // The child resolves in the registry as a template in its own right.
  const auto* turret = registry.find(apc->children[1].name);
  CHECK(turret != nullptr);
  if (turret != nullptr) CHECK_EQ(turret->className, std::string("RotationalBundle"));
}

static void testCommandsBeforeAnyCreate() {
  // A command before the first create belongs nowhere — it has to be counted rather
  // than quietly attributed to a random template.
  MemoryFiles files;
  files.files["a.con"] =
      "ObjectTemplate.geometry orphan\n"
      "ObjectTemplate.create SimpleObject real\n"
      "ObjectTemplate.geometry real\n";

  const game::Registry registry = runInto(files, "a.con");
  CHECK_EQ(registry.stats().orphanCommands, 1);
  CHECK_EQ(registry.size(), std::size_t(1));
  const auto* object = registry.find("real");
  CHECK(object != nullptr);
  if (object != nullptr) CHECK_EQ(object->text("geometry"), std::string_view("real"));
}

static void testOtherTargetsIgnored() {
  MemoryFiles files;
  files.files["a.con"] =
      "GeometryTemplate.create BundledMesh ammokit\n"
      "CollisionManager.createTemplate ammokit\n"
      "ObjectTemplate.create GenericFireArm ammokit\n";

  const game::Registry registry = runInto(files, "a.con");
  CHECK_EQ(registry.size(), std::size_t(1));
  CHECK_EQ(registry.stats().propertiesSet, 0LL);
}

TEST_MAIN({
  testCreateAndProperties();
  testTweakReopensTemplate();
  testAccumulatingProperties();
  testComponents();
  testChildTemplatesWithPositions();
  testCommandsBeforeAnyCreate();
  testOtherTargetsIgnored();
})
