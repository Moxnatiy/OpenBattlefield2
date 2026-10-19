// What a soldier is drawn from (`soldier_model.h`). The text is the game's own,
// cut down: `objects/soldiers/mec/mec_light_soldier.con` with its `.tweak`,
// `objects/kits/mec/MEC_Specops.con`, and the weapons' `itemIndex` from their tweaks.
#include <cmath>
#include <map>
#include <string>

#include "check.h"
#include "obf2/game/soldier_model.h"

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

game::Registry registryOf(MemoryFiles& files) {
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("all.con");
  return registry;
}

}  // namespace

static void testSpecops() {
  MemoryFiles files;
  files.files["all.con"] =
      "ObjectTemplate.create Soldier mec_light_soldier\n"
      "ObjectTemplate.geometry mec_light_soldier\n"
      "ObjectTemplate.skeleton1P Objects/Soldiers/Common/Animations/1p_setup.ske\n"
      "ObjectTemplate.skeleton3P Objects/Soldiers/Common/Animations/3p_setup.ske\n"
      "ObjectTemplate.animationSystem1P Objects/Soldiers/Common/Animations/AnimationSystem1p.inc\n"
      "ObjectTemplate.animationSystem3P Objects/Soldiers/Common/Animations/AnimationSystem3p.inc\n"
      "ObjectTemplate.poseCameraPosition 0 0/0.47/0.1\n"
      "ObjectTemplate.poseCameraPosition 1 0/-0.05/0.1\n"
      "ObjectTemplate.poseCameraPosition 2 0/-0.63/0.1\n"
      "ObjectTemplate.addTemplate SoldierCamera\n"
      "ObjectTemplate.create Camera SoldierCamera\n"
      "ObjectTemplate.worldFOV 1.1\n"
      "ObjectTemplate.insideFOV 1.1\n"
      "ObjectTemplate.create GenericFireArm rupis_baghira_silencer\n"
      "ObjectTemplate.geometry rupis_baghira_silencer\n"
      "ObjectTemplate.itemIndex 2\n"
      "ObjectTemplate.create GenericFireArm rurrif_ak74u\n"
      "ObjectTemplate.geometry rurrif_ak74u\n"
      "ObjectTemplate.animationSystem1P Objects/Weapons/Handheld/rurrif_ak74u/AnimationSystem1p.inc\n"
      "ObjectTemplate.animationSystem3P Objects/Weapons/Handheld/rurrif_ak74u/AnimationSystem3p.inc\n"
      "ObjectTemplate.itemIndex 3\n"
      "ObjectTemplate.create Kit MEC_Specops\n"
      "ObjectTemplate.geometry MEC_Kits\n"
      "ObjectTemplate.geometry.kit 2\n"
      "ObjectTemplate.addTemplate RUPIS_Baghira_silencer\n"
      "ObjectTemplate.addTemplate rurrif_ak74u\n";
  const game::Registry registry = registryOf(files);

  const auto model = game::soldierModel(registry, "mec_light_soldier", "MEC_Specops");
  CHECK(model.has_value());
  if (!model) return;
  CHECK_EQ(model->body.geometryName, std::string("mec_light_soldier"));
  CHECK_EQ(model->body.geometry, game::kSoldierThirdPersonGeometry);
  CHECK_EQ(model->skeleton3p,
           std::string("Objects/Soldiers/Common/Animations/3p_setup.ske"));
  CHECK(model->kit.has_value());
  if (model->kit) {
    CHECK_EQ(model->kit->geometryName, std::string("MEC_Kits"));
    CHECK_EQ(model->kit->geometry, 2);
  }
  CHECK(model->weapon.has_value());
  if (model->weapon) CHECK_EQ(model->weapon->geometryName, std::string("rurrif_ak74u"));
  CHECK_EQ(model->weaponAnimationSystem3p,
           std::string("Objects/Weapons/Handheld/rurrif_ak74u/AnimationSystem3p.inc"));

  // First person: the same meshes, sub-geometry 0, on the other skeleton.
  CHECK_EQ(model->skeleton1p, std::string("Objects/Soldiers/Common/Animations/1p_setup.ske"));
  CHECK_EQ(model->animationSystem1p,
           std::string("Objects/Soldiers/Common/Animations/AnimationSystem1p.inc"));
  CHECK_EQ(model->body1p.geometryName, std::string("mec_light_soldier"));
  CHECK_EQ(model->body1p.geometry, game::kSoldierFirstPersonGeometry);
  CHECK(model->weapon1p.has_value());
  if (model->weapon1p) {
    CHECK_EQ(model->weapon1p->geometryName, std::string("rurrif_ak74u"));
    CHECK_EQ(model->weapon1p->geometry, game::kSoldierFirstPersonGeometry);
  }
  CHECK_EQ(model->weaponAnimationSystem1p,
           std::string("Objects/Weapons/Handheld/rurrif_ak74u/AnimationSystem1p.inc"));

  // The camera's point per pose, and none for a pose the template does not name.
  const auto stand = game::poseCameraPosition(registry, "mec_light_soldier", 0);
  CHECK(stand.has_value());
  if (stand) CHECK(std::abs(stand->y - 0.47f) < 1e-5f && std::abs(stand->z - 0.1f) < 1e-5f);
  const auto prone = game::poseCameraPosition(registry, "mec_light_soldier", 2);
  CHECK(prone.has_value() && std::abs(prone->y + 0.63f) < 1e-5f);
  CHECK(!game::poseCameraPosition(registry, "mec_light_soldier", 3).has_value());
  CHECK(!game::poseCameraPosition(registry, "nobody", 0).has_value());

  // His camera's field of view; a template that is not a soldier has none.
  const auto fov = game::soldierCameraFov(registry, "mec_light_soldier");
  CHECK(fov.has_value() && std::abs(*fov - 1.1f) < 1e-5f);
  CHECK(!game::soldierCameraFov(registry, "rurrif_ak74u").has_value());

  // A soldier whose kit is not known yet is still a body.
  const auto bare = game::soldierModel(registry, "mec_light_soldier", "");
  CHECK(bare.has_value() && !bare->kit && !bare->weapon);
  CHECK(!game::soldierModel(registry, "nobody", "").has_value());
}

TEST_MAIN({ testSpecops(); })
