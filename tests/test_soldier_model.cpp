// What a soldier is drawn from (`soldier_model.h`). The text is the game's own,
// cut down: `objects/soldiers/mec/mec_light_soldier.con` with its `.tweak`,
// `objects/kits/mec/MEC_Specops.con`, and the weapons' `itemIndex` from their tweaks.
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
      "ObjectTemplate.skeleton3P Objects/Soldiers/Common/Animations/3p_setup.ske\n"
      "ObjectTemplate.animationSystem3P Objects/Soldiers/Common/Animations/AnimationSystem3p.inc\n"
      "ObjectTemplate.create GenericFireArm rupis_baghira_silencer\n"
      "ObjectTemplate.geometry rupis_baghira_silencer\n"
      "ObjectTemplate.itemIndex 2\n"
      "ObjectTemplate.create GenericFireArm rurrif_ak74u\n"
      "ObjectTemplate.geometry rurrif_ak74u\n"
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

  // A soldier whose kit is not known yet is still a body.
  const auto bare = game::soldierModel(registry, "mec_light_soldier", "");
  CHECK(bare.has_value() && !bare->kit && !bare->weapon);
  CHECK(!game::soldierModel(registry, "nobody", "").has_value());
}

TEST_MAIN({ testSpecops(); })
