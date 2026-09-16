// The session module's own helpers (`obf2/session/remote_world.h`).
//
// They lived in `main.cpp` and had no test at all, which is half of why rule 11
// exists. The session's conversation with a server needs a socket and is covered
// by the protocol tests beside this one; what is checked here is everything that
// can be checked without one.
#include <string>
#include <vector>

#include "check.h"
#include "obf2/session/remote_world.h"

using namespace obf2;

// The tolerance is deliberately narrow: both sides take a control point's
// position from the same data, so a match has to be all but exact, and a wider
// one would start inventing correspondences.
static void testNearestKnownMatchesOnlyWhatIsClose() {
  std::vector<session::KnownObject> known;
  known.push_back({"gasstation", Vec3f{-161.0f, 157.0f, -263.0f}});
  known.push_back({"market", Vec3f{-196.0f, 150.0f, 183.0f}});

  const auto* hit = session::nearestKnown(known, Vec3f{-161.4f, 157.0f, -263.2f});
  CHECK(hit != nullptr);
  if (hit != nullptr) CHECK_EQ(hit->name, std::string("gasstation"));

  // Three metres away is past the two the tolerance allows.
  CHECK(session::nearestKnown(known, Vec3f{-164.0f, 157.0f, -263.0f}) == nullptr);
  // And with nothing to match against there is no answer rather than a crash.
  CHECK(session::nearestKnown({}, Vec3f{}) == nullptr);
}

// Every stage has a name, because the report prints it and an unnamed one would
// read as a question mark next to an object that does not appear.
static void testEveryDrawStageIsNamed() {
  const session::DrawStage all[] = {
      session::DrawStage::Drawn,          session::DrawStage::NoTemplate,
      session::DrawStage::NoTree,         session::DrawStage::NoGeometryName,
      session::DrawStage::GeometryInChild, session::DrawStage::NoGeometryFile,
      session::DrawStage::NoMesh};
  for (const session::DrawStage stage : all) {
    const std::string_view name = session::drawStageName(stage);
    CHECK(!name.empty());
    CHECK(name != "?");
  }
}

// The settings the session takes are its own, not the application's `Args`: the
// defaults have to be the ones a plain connection wants.
static void testSettingsDefaults() {
  const session::Settings settings;
  CHECK(settings.connectTo.empty());
  CHECK(settings.playerName.empty());
  // -1 means "take the ordinal the server's challenge names" (bf2_protocol.h).
  CHECK_EQ(settings.ordinal, -1);
  CHECK(!settings.blockReady);
  CHECK(!settings.skipContent);
  CHECK(!settings.skipDatabase);
  CHECK(!settings.startSimulation);
}

TEST_MAIN({
  testNearestKnownMatchesOnlyWhatIsClose();
  testEveryDrawStageIsNamed();
  testSettingsDefaults();
})
