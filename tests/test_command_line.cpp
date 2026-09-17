#include <string>
#include <vector>

#include "check.h"
#include "obf2/app/command_line.h"

using namespace obf2;

namespace {

app::Args parse(const std::vector<const char*>& flags) {
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>("openbf2"));
  for (const char* flag : flags) argv.push_back(const_cast<char*>(flag));
  return app::parseArgs(static_cast<int>(argv.size()), argv.data());
}

// With nothing on the line the run is the menu: no level, no mesh, a window of
// the base size doubled and the name the engine joins under.
static void testDefaults() {
  const app::Args args = parse({});
  CHECK(args.levelName.empty());
  CHECK(args.meshPath.empty());
  CHECK_EQ(args.width, 1600);
  CHECK_EQ(args.height, 1200);
  CHECK_EQ(args.frames, 0);
  CHECK(args.playerName == "OpenBF2");
  CHECK_EQ(args.lodIndex, 0);
  CHECK_EQ(args.geometryIndex, -1);
  CHECK(!args.spawnGroupGiven);
}

static void testLevelAndScreenshot() {
  const app::Args args =
      parse({"--level", "strike_at_karkand", "--frames", "4", "--screenshot", "out.png",
             "--width", "800", "--height", "600", "--no-hud"});
  CHECK(args.levelName == "strike_at_karkand");
  CHECK_EQ(args.frames, 4);
  CHECK(args.screenshot == "out.png");
  CHECK_EQ(args.width, 800);
  CHECK_EQ(args.height, 600);
  CHECK(args.noHud);
}

// The triples are in the game's own form, x/y/z — the same as in a `.con`.
static void testVectorFlags() {
  const app::Args args = parse({"--camera", "-135.4/165.0/-133.0", "--angles", "90", "-10",
                                "--focus", "1/2/3", "--collision-near", "-141.45/163.6/-118.2"});
  CHECK(args.camera.has_value());
  if (args.camera) {
    CHECK(std::abs(args.camera->x + 135.4f) < 1e-3f);
    CHECK(std::abs(args.camera->y - 165.0f) < 1e-3f);
    CHECK(std::abs(args.camera->z + 133.0f) < 1e-3f);
  }
  CHECK(std::abs(args.cameraYaw - 90.0f) < 1e-3f);
  CHECK(std::abs(args.cameraPitch + 10.0f) < 1e-3f);
  CHECK(args.focus.has_value());
  CHECK(args.collisionNear.has_value());
  if (args.collisionNear) CHECK(std::abs(args.collisionNear->z + 118.2f) < 1e-3f);
}

// The scheduled input: a turn of a known size, a run, a jump, a click and a
// console line, each on its own frame. These are the measuring instruments, so
// the shape of every one of them is fixed here.
static void testSchedules() {
  const app::Args args = parse({"--look-at", "800:300:3:0", "--move-at", "700:800:1:0:1",
                                "--jump-at", "900", "--jump-at", "1000", "--click-at",
                                "20:400:240", "--exec-at", "60:openbf2.spawnAt 4",
                                "--screenshot-at", "12:shot.png", "--trace-frames", "10:5"});
  CHECK_EQ(args.looks.size(), std::size_t(1));
  if (!args.looks.empty()) {
    CHECK_EQ(args.looks[0].frame, 800);
    CHECK_EQ(args.looks[0].frames, 300);
    CHECK(std::abs(args.looks[0].dx - 3.0f) < 1e-3f);
  }
  CHECK_EQ(args.moves.size(), std::size_t(1));
  if (!args.moves.empty()) {
    CHECK_EQ(args.moves[0].frames, 800);
    CHECK(std::abs(args.moves[0].dx - 1.0f) < 1e-3f);
    CHECK_EQ(args.moves[0].sprint, 1);
  }
  CHECK_EQ(args.jumps.size(), std::size_t(2));
  if (args.jumps.size() == 2) CHECK_EQ(args.jumps[1], 1000);
  CHECK_EQ(args.clicks.size(), std::size_t(1));
  if (!args.clicks.empty()) {
    CHECK_EQ(args.clicks[0].frame, 20);
    CHECK(std::abs(args.clicks[0].y - 240.0f) < 1e-3f);
  }
  CHECK_EQ(args.scheduledLines.size(), std::size_t(1));
  if (!args.scheduledLines.empty()) {
    CHECK_EQ(args.scheduledLines[0].frame, 60);
    // Everything after the first colon is the line, spaces and all.
    CHECK(args.scheduledLines[0].line == "openbf2.spawnAt 4");
  }
  CHECK_EQ(args.screenshotAt.size(), std::size_t(1));
  if (!args.screenshotAt.empty()) {
    CHECK_EQ(args.screenshotAt[0].first, 12);
    CHECK(args.screenshotAt[0].second == "shot.png");
  }
  CHECK_EQ(args.traceFrom, 10);
  CHECK_EQ(args.traceFrames, 5);
}

// `--group` has a default, so whether it was given is a flag of its own: a
// windowed run must not ask to spawn before the player has touched anything.
static void testSpawnChoice() {
  const app::Args plain = parse({"--team", "2", "--kit", "3"});
  CHECK_EQ(plain.team, 2);
  CHECK_EQ(plain.kit, 3);
  CHECK(!plain.spawnGroupGiven);
  CHECK_EQ(plain.spawnGroup, 1);

  const app::Args given = parse({"--group", "4"});
  CHECK(given.spawnGroupGiven);
  CHECK_EQ(given.spawnGroup, 4);
}

// A value that does not parse is reported and skipped; the flags around it still
// arrive.
static void testMalformedValueIsSkipped() {
  const app::Args args = parse({"--look-at", "nonsense", "--frames", "7"});
  CHECK(args.looks.empty());
  CHECK_EQ(args.frames, 7);
}

}  // namespace

TEST_MAIN({
  testDefaults();
  testLevelAndScreenshot();
  testVectorFlags();
  testSchedules();
  testSpawnChoice();
  testMalformedValueIsSkipped();
})
