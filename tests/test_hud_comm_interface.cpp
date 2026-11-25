// A communication rose: the `setStringMap` table of HUD/HudSetup/Communication
// and what `selectOrder` does with it (0x7a0de0, docs/functions/command-rose.md).
#include "check.h"
#include "obf2/hud/comm_interface.h"

using namespace obf2;

namespace {

void run() {
  hud::CommInterface rose;
  CHECK(rose.empty());
  // SquadLeader.con, two of its lines.
  rose.setStringMap("Interface_1:1", "HUD_COMROSE_SL_attack", "SquadLeader.sendOrder 5 0");
  rose.setStringMap("Interface_1:3", "\"*NOT USED*\"", "");
  CHECK(rose.entry(1) && rose.entry(1)->label == "HUD_COMROSE_SL_attack");
  CHECK(!rose.entry(2));

  // A leaf runs its command and the rose goes back to the top.
  const auto attack = rose.selectOrder(1);
  CHECK(!attack.descended);
  CHECK(attack.command && *attack.command == "SquadLeader.sendOrder 5 0");
  CHECK_EQ(rose.path(), std::string("1"));

  // A gap closes it with nothing to run.
  const auto gap = rose.selectOrder(3);
  CHECK(gap.command && gap.command->empty());
  const auto missing = rose.selectOrder(9);
  CHECK(missing.command && missing.command->empty());

  // Entries under `1:4` make 4 a level of its own: choosing it goes down.
  rose.setStringMap("Interface_1:4", "PARENT", "");
  rose.setStringMap("Interface_1:4:1", "CHILD", "child command");
  const auto down = rose.selectOrder(4);
  CHECK(down.descended && !down.command);
  CHECK_EQ(rose.path(), std::string("1:4"));
  CHECK(rose.entry(1) && rose.entry(1)->label == "CHILD");
  const auto child = rose.selectOrder(1);
  CHECK(child.command && *child.command == "child command");
  CHECK_EQ(rose.path(), std::string("1"));
}

}  // namespace

TEST_MAIN({ run(); })
