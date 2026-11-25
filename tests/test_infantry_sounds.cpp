#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "check.h"
#include "obf2/app/infantry_sounds.h"

using namespace obf2;

static void testPhaseLatch() {
  app::MovementSoundLatch latch;
  CHECK(!latch.update({1, .1f}, 2, true, 0, false));
  CHECK_EQ(latch.update({1, .5f}, 2, true, 0, false).value_or(99), 0u);
  CHECK(!latch.update({1, .9f}, 2, true, 0, false));
  CHECK_EQ(latch.update({1, .1f}, 2, true, 1, false).value_or(99), 1u);
  CHECK(!latch.update({1, .8f}, 2, false, 0, false));
  CHECK_EQ(latch.update({1, .8f}, 2, true, 2, false).value_or(99), 2u);
  CHECK(!latch.update({1, .1f}, 0, true, 0, false));
  CHECK(!latch.update({1, .2f}, 2, true, 0, false));
  CHECK_EQ(latch.update({1, .8f}, 2, true, 0, true).value_or(99), 3u);
}

static void testAuthoredSlots() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / ("infantry_sounds_" + std::to_string(getpid()));
  fs::create_directories(root / "common/material");
  {
    std::ofstream out(root / "common/material/materialManagerSettings.con");
    out << "MaterialManager.createCell 5500 8\nMaterialManager.setSoundTemplate 0 Dirt\n"
           "MaterialManager.setSoundTemplate 3 Sprint\nMaterialManager.createCell 5500 4\n"
           "MaterialManager.setSoundTemplate 0 Hard\nMaterialManager.setSoundTemplate 2 Dirt\n";
  }
  {
    std::ofstream out(root / "common/material/ImpactSounds.con");
    out << "ObjectTemplate.create Sound Dirt\nObjectTemplate.soundFilename a.wav,b.wav\n"
           "ObjectTemplate.volume 0.48\nObjectTemplate.reverbLevel 1\n"
           "ObjectTemplate.create Sound Hard\nObjectTemplate.soundFilename hard.wav\n"
           "ObjectTemplate.volume 0.44\nObjectTemplate.reverbLevel 1\n"
           "ObjectTemplate.create Sound Sprint\nObjectTemplate.soundFilename run.wav\n"
           "ObjectTemplate.volume 0.48\nObjectTemplate.reverbLevel 1\n";
  }
  FileSystem files;
  CHECK(files.mountDirectory(root));
  app::InfantrySounds sounds;
  CHECK(sounds.load(files));
  CHECK(sounds.cue(8, 0));
  if (const auto* cue = sounds.cue(8, 0)) {
    CHECK_EQ(cue->files.size(), 2u);
    CHECK_EQ(cue->gain, .48f);
  }
  CHECK_EQ(sounds.cue(8, 1), sounds.cue(8, 0));
  CHECK(sounds.cue(4, 0) && sounds.cue(4, 0)->files[0] == "hard.wav");
  CHECK(sounds.cue(4, 2) && sounds.cue(4, 2)->files[0] == "a.wav");
  CHECK(!sounds.cue(99, 0));
  fs::remove_all(root);
}

static void testJumpLandingBeforeContact() {
  app::JumpLandingSoundState state;
  CHECK_EQ(app::JumpLandingSoundState::traceStart({0, 1.37f, 0}, 1).y, 1.37f);
  CHECK_EQ(app::JumpLandingSoundState::traceStart({0, 1.37f, 0}, 2).y, 2.37f);
  server::PhysicsConstants physics;
  int traces = 0;
  Vec3f segment;
  bool surfaceInRange = false;
  auto trace = [&](const Vec3f& motion) -> std::optional<int> {
    ++traces; segment = motion;
    return surfaceInRange ? std::optional<int>{78} : std::nullopt;
  };
  CHECK(!state.update(true, {0, -1, 0}, physics, trace).landing);
  CHECK_EQ(traces, 0);
  CHECK(!state.update(false, {0, 2.49f, 0}, physics, trace).jump);
  CHECK(state.update(false, {0, 2.5f, 0}, physics, trace).jump);
  CHECK(!state.update(false, {0, 6, 0}, physics, trace).jump);
  CHECK(!state.update(false, {0, -.5f, 0}, physics, trace).landing);
  CHECK_EQ(traces, 0);
  CHECK(!state.update(false, {0, -2, 0}, physics, trace).landing);
  CHECK_EQ(segment.y, -1.f);
  surfaceInRange = true;
  const auto land = state.update(false, {0, -4, 0}, physics, trace);
  CHECK(land.landing && land.material == 78); // Airborne, before contact.
  const int landedTraces = traces;
  CHECK(!state.update(false, {0, -5, 0}, physics, trace).landing);
  CHECK_EQ(traces, landedTraces);
  CHECK(!state.update(true, {}, physics, trace).landing); // No duplicate contact cue.
  CHECK(state.update(false, {0, 3, 0}, physics, trace).jump); // Re-armed.
  state.reset();
  physics.animationLandHeight = 0;
  CHECK(!state.update(false, {0, -4, 0}, physics, trace).landing);
  physics.animationLandHeight = 1;
  state.update(false, {.2f, -.6f, 0}, physics, trace);
  CHECK(std::abs(length(segment)-1.f) < .0001f);
  CHECK(segment.y < -.9f); // Low-speed branch replaces Y before normalizing.
}

TEST_MAIN({ testPhaseLatch(); testAuthoredSlots(); testJumpLandingBeforeContact(); })
