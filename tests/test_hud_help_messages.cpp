// The in-game help box (obf2/hud/help_messages.h).
#include <string>

#include "check.h"
#include "obf2/hud/help_messages.h"

namespace {

void testExpandsPlayerInputMarker() {
  const auto name = [](std::string_view action) {
    return action == "c_PIUse" ? std::string("E") : std::string();
  };
  CHECK(obf2::hud::expandHelpKeys("Press the '#PIUse#' Key", name) == "Press the 'E' Key");
  CHECK(obf2::hud::expandHelpKeys("no marker", name) == "no marker");
  // An unbound action keeps its marker.
  CHECK(obf2::hud::expandHelpKeys("#PIOther# x", name) == "#PIOther# x");
}

void testBoxCountsDownAndHides() {
  obf2::hud::HelpBox box;
  box.show("text", 9.0f);
  CHECK(box.shown());
  box.tick(8.9f);
  CHECK(box.shown());
  box.tick(0.2f);
  CHECK(!box.shown());
  box.show("", 9.0f);
  CHECK(!box.shown());
  box.show("text", 0.0f);
  CHECK(!box.shown());
}

// 0x5f1460: the repeat timer fires once remaining reaches 0 (1/30 s at start),
// a shown message never comes again, and the next waits more than 10 s.
void testSchedulerOncePerMessageAndTenSecondGap() {
  using obf2::hud::HelpMessage;
  obf2::hud::HelpScheduler help;
  obf2::hud::HelpConditions exit;
  exit.exitVehicle = true;
  CHECK(!help.tick(0.02f, exit));  // remaining 1/30 -> 0.0133
  CHECK(!help.tick(0.02f, exit));  // -> below 0
  CHECK(help.tick(0.02f, exit) == HelpMessage::ExitVehicle);
  CHECK(help.played(HelpMessage::ExitVehicle));
  for (int i = 0; i < 1000; ++i) CHECK(!help.tick(0.1f, exit));

  obf2::hud::HelpScheduler gap;
  gap.markPlayed(HelpMessage::ExitVehicle);  // a profile that lists it
  CHECK(!gap.tick(1.0f, exit));
  obf2::hud::HelpConditions enter;
  enter.enterVehicle = true;
  gap.markPlayed(HelpMessage::ExitVehicle);
  for (int i = 0; i < 3; ++i) gap.tick(0.02f, enter);
  CHECK(gap.played(HelpMessage::EnterVehicle));
  obf2::hud::HelpScheduler off;
  off.setEnabled(false);
  for (int i = 0; i < 10; ++i) CHECK(!off.tick(0.02f, exit));
}

void testSchedulerWaitsTenSeconds() {
  using obf2::hud::HelpMessage;
  obf2::hud::HelpScheduler help;
  obf2::hud::HelpConditions both;
  both.enterVehicle = true;
  for (int i = 0; i < 3; ++i) help.tick(0.02f, both);
  CHECK(help.played(HelpMessage::EnterVehicle));
  both.exitVehicle = true;
  for (int i = 0; i < 99; ++i) CHECK(!help.tick(0.1f, both));  // 9.9 s
  bool shown = false;
  for (int i = 0; i < 10 && !shown; ++i) shown = help.tick(0.1f, both) == HelpMessage::ExitVehicle;
  CHECK(shown);
}

}  // namespace

TEST_MAIN({
  testExpandsPlayerInputMarker();
  testBoxCountsDownAndHides();
  testSchedulerOncePerMessageAndTenSecondGap();
  testSchedulerWaitsTenSeconds();
})
