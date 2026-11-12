// The bolt-action cycle (`obf2/game/weapon_shift.h`): the M24's own lines, and the
// cycle `DefaultAnimationComp` runs after a round (Linux 0x60a740, 0x60a840).
#include <map>
#include <string>

#include "check.h"
#include "obf2/game/weapon_shift.h"

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

}  // namespace

static void testTemplate() {
  const game::Registry registry = registryOf(
      "ObjectTemplate.create GenericFireArm m24\n"
      "ObjectTemplate.createComponent DefaultAnimationComp\n"
      "ObjectTemplate.animation.useShiftAnimation 1\n"
      "ObjectTemplate.animation.shiftDelay 1.8\n"
      "ObjectTemplate.create GenericFireArm m4\n"
      "ObjectTemplate.createComponent DefaultAnimationComp\n");
  const game::ShiftTemplate m24 = game::shiftOf(*registry.find("m24"));
  CHECK(m24.useShiftAnimation);
  CHECK_EQ(m24.shiftDelayTicks(), 54);  // 1.8 × 30
  const game::ShiftTemplate m4 = game::shiftOf(*registry.find("m4"));
  CHECK(!m4.useShiftAnimation);
}

// A round with rounds left asks for a shift; it starts when nothing is busy, raises
// the message once, refuses a round until `shiftDelay` ticks have passed.
static void testCycle() {
  game::ShiftTemplate tmpl;
  tmpl.useShiftAnimation = true;
  tmpl.shiftDelay = 1.8f;
  game::WeaponShift shift;
  shift.setWeapon(tmpl);
  CHECK(shift.canFire());
  shift.onRoundComplete(4);
  CHECK(!shift.canFire());
  CHECK(!shift.update(100, true));  // the weapon is still busy: it waits
  CHECK(shift.update(101, false));  // starts, message 32
  CHECK(!shift.canFire());
  for (std::uint32_t t = 102; t < 155; ++t) {
    CHECK(!shift.update(t, false));
    CHECK(!shift.canFire());
  }
  CHECK(!shift.update(155, false));  // 101 + 54
  CHECK(shift.canFire());
  CHECK_EQ(shift.shifts(), 1);
}

// The last round of a magazine does not cycle; a weapon without the flag never does.
static void testNoShift() {
  game::ShiftTemplate tmpl;
  tmpl.useShiftAnimation = true;
  tmpl.shiftDelay = 1.0f;
  game::WeaponShift shift;
  shift.setWeapon(tmpl);
  shift.onRoundComplete(0);
  CHECK(shift.canFire());
  game::WeaponShift plain;
  plain.setWeapon(game::ShiftTemplate{});
  plain.onRoundComplete(10);
  CHECK(plain.canFire());
}

TEST_MAIN({
  testTemplate();
  testCycle();
  testNoShift();
})
