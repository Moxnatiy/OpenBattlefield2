// A hand weapon's zoom (`obf2/game/weapon_zoom.h`): the template from the M24's own
// lines, and the toggle `DefaultZoomComp` runs (Linux 0x629990..0x6297e0).
#include <map>
#include <string>

#include "check.h"
#include "obf2/game/weapon_zoom.h"

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

game::Registry registryOf(const std::string& text) {
  MemoryFiles files;
  files.files["all.con"] = text;
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("all.con");
  return registry;
}

const char* kM24 =
    "ObjectTemplate.create GenericFireArm usrif_m24\n"
    "ObjectTemplate.createComponent DefaultZoomComp\n"
    "ObjectTemplate.zoom.zoomDelay 0.125\n"
    "ObjectTemplate.zoom.zoomLod 1\n"
    "ObjectTemplate.zoom.addZoomFactor 0\n"
    "ObjectTemplate.zoom.addZoomFactor 0.2\n"
    "ObjectTemplate.zoom.changeFovDelay 0.08\n"
    "ObjectTemplate.zoom.zoomOutAfterFire 1\n"
    "ObjectTemplate.zoom.disableMuzzleWhenZoomed 1\n"
    "ObjectTemplate.create GenericFireArm usrif_m4\n"
    "ObjectTemplate.createComponent SingleFireComp\n";

}  // namespace

// The sniper's list is [0, 0.2]: its own `addZoomFactor 0` is already there.
static void testTemplate() {
  const game::Registry registry = registryOf(kM24);
  const game::ObjectTemplate* m24 = registry.find("usrif_m24");
  const game::ObjectTemplate* m4 = registry.find("usrif_m4");
  CHECK(m24 != nullptr && m4 != nullptr);
  if (m24 == nullptr || m4 == nullptr) return;
  CHECK(!game::zoomOf(*m4).has_value());
  const auto zoom = game::zoomOf(*m24);
  CHECK(zoom.has_value());
  if (!zoom) return;
  CHECK_EQ(zoom->factors.size(), std::size_t(2));
  CHECK_EQ(zoom->factors[1], 0.2f);
  CHECK_EQ(zoom->zoomDelayTicks(), 3);       // 0.125 × 30, truncated
  CHECK_EQ(zoom->changeFovDelayTicks(), 2);  // 0.08 × 30
  CHECK(zoom->zoomOutAfterFire);
  CHECK_EQ(zoom->mouseSpeedMod, 1.0f);
}

// The M24: a press at tick T zooms in; the field of view follows `changeFovDelay`
// (2) ticks later; a second press inside the zoom delay (3 ticks) is ignored; a
// press after it zooms out, at once.
static void testToggle() {
  const game::Registry registry = registryOf(kM24);
  game::WeaponZoom zoom;
  zoom.setWeapon(game::zoomOf(*registry.find("usrif_m24")));
  CHECK(zoom.present());
  CHECK(!zoom.hidesMuzzle());
  CHECK(!zoom.update(100));
  zoom.press(101);
  CHECK(zoom.zoomed());
  CHECK(zoom.hidesMuzzle());
  CHECK_EQ(zoom.fov(), 0.0f);          // not yet: 101 + 2
  CHECK(!zoom.update(102));
  CHECK(zoom.update(103));
  CHECK_EQ(zoom.fov(), 0.2f);
  zoom.press(103);                      // inside the zoom delay (101 + 3 = 104)
  CHECK(zoom.zoomed());
  zoom.update(104);                     // the zoom lod arrives with the zoom delay
  zoom.press(105);
  CHECK(!zoom.zoomed());
  CHECK(!zoom.hidesMuzzle());
  auto unsuppressed = game::zoomOf(*registry.find("usrif_m24"));
  unsuppressed->disableMuzzleWhenZoomed = false;
  game::WeaponZoom alternate;
  alternate.setWeapon(unsuppressed);
  alternate.press(101);
  CHECK(alternate.zoomed());
  CHECK(!alternate.hidesMuzzle());
  CHECK_EQ(zoom.fov(), 0.0f);           // a zoom out is at once
}

// `zoomOutAfterFire`: a round while zoomed asks for a toggle, made by the next
// update; a reload goes back to level 0 on its own.
static void testZoomOut() {
  const game::Registry registry = registryOf(kM24);
  game::WeaponZoom zoom;
  zoom.setWeapon(game::zoomOf(*registry.find("usrif_m24")));
  zoom.press(10);
  zoom.update(12);
  CHECK_EQ(zoom.fov(), 0.2f);
  zoom.onRound();
  zoom.update(13);
  CHECK(!zoom.zoomed());
  CHECK_EQ(zoom.fov(), 0.0f);
  zoom.press(20);
  zoom.update(22);
  CHECK(zoom.zoomed());
  zoom.onReload(30);
  CHECK(!zoom.zoomed());
}

// `zoomLod`: the weapon's mesh goes to that level `zoomDelay` ticks after a zoom in
// and back at once with the zoom out.
static void testLod() {
  const game::Registry registry = registryOf(kM24);
  game::WeaponZoom zoom;
  zoom.setWeapon(game::zoomOf(*registry.find("usrif_m24")));
  CHECK_EQ(zoom.lod(), 0);
  zoom.press(200);
  zoom.update(201);
  CHECK_EQ(zoom.lod(), 0);
  zoom.update(202);
  CHECK_EQ(zoom.lod(), 0);
  zoom.update(203);  // 200 + 3
  CHECK_EQ(zoom.lod(), 1);
  zoom.press(210);
  CHECK_EQ(zoom.lod(), 0);
}

TEST_MAIN({
  testTemplate();
  testToggle();
  testZoomOut();
  testLod();
})
