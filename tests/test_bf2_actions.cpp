// The player action stream — what the client moves its soldier with.
//
// The layout comes from `PlayerActionManager::processReceivedPacket` (0x44d670).
// We check it two ways, and the second matters more than the first:
//
//   1. what we write reads back;
//   2. our parser understands **real** packets of the original client,
//      captured off the network (`tests/data/actions-*.bin`).
//
// The second test is the proof that the layout is right: those bytes are not ours.
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_protocol.h"

// The BF2 namespace only: `obf2::net` has an ExtendedHeader of its own, and two
// identical names would make the reference ambiguous.
using namespace obf2::net::bf2;

namespace {

// One packet from a captured file: a u32 length, then the bytes.
std::vector<std::byte> capturedPacket(const char* name) {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/" + name);
  return packets.empty() ? std::vector<std::byte>{} : packets.front();
}

}  // namespace

// A round trip: what we assembled reads back with the same parser.
static void testRoundTrip() {
  ExtendedHeader header;
  header.sequence = 5;
  header.ack = 7;
  header.ackBits = 0xFFFFFFFFu;

  PlayerAction action;
  action.axes[kAxisForward] = kAxisFull;
  action.axes[kAxisMouseX] = -13;
  action.axes[kAxisMouseY] = 40;
  action.buttons = kButtonSprint;

  PlayerActions stream;
  stream.number = 200;
  stream.tick = 172;
  stream.actions.assign(3, action);
  const auto packet = writePlayerActions(9, header, stream);

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read) return;

  CHECK_EQ(read->number, std::uint32_t(200));
  CHECK_EQ(read->tick, 172);
  CHECK_EQ(read->actions.size(), std::size_t(3));
  for (const PlayerAction& got : read->actions) {
    CHECK_EQ(got.axes[kAxisForward], kAxisFull);
    CHECK_EQ(got.axes[kAxisMouseX], std::int16_t(-13));
    CHECK_EQ(got.axes[kAxisMouseY], std::int16_t(40));
    CHECK_EQ(got.buttons, kButtonSprint);
    CHECK(got.flag);
    // The three axes we do not name stay zero.
    CHECK_EQ(got.axes[0], std::int16_t(0));
    CHECK_EQ(got.axes[1], std::int16_t(0));
    CHECK_EQ(got.axes[2], std::int16_t(0));
  }
}

// Negative axes: moving backwards and the mouse to the left.
static void testNegativeAxes() {
  ExtendedHeader header;
  PlayerAction action;
  action.axes[kAxisForward] = -kAxisFull;
  action.axes[kAxisMouseX] = -127;

  PlayerActions stream;
  stream.tick = 1;
  stream.actions.push_back(action);
  const auto packet = writePlayerActions(1, header, stream);
  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read || read->actions.empty()) return;
  CHECK_EQ(read->actions.front().axes[kAxisForward], std::int16_t(-kAxisFull));
  CHECK_EQ(read->actions.front().axes[kAxisMouseX], std::int16_t(-127));
}

// A packet with no actions does not belong to the parser.
static void testPacketWithoutActions() {
  ExtendedHeader header;
  const auto packet =
      writePostRemoteEvent(1, header, 0, kNetworkCategory, kNetSelectSpawnGroup, 2);
  CHECK(!readPlayerActions(packet).has_value());
}

// A real packet of the original client: the player runs forward.
static void testCapturedRun() {
  const auto packet = capturedPacket("actions-run.bin");
  CHECK(!packet.empty());
  if (packet.empty()) return;

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read) return;

  // The original puts the last three sets into a packet — a reserve against loss.
  CHECK_EQ(read->actions.size(), std::size_t(3));
  if (read->actions.empty()) return;
  const PlayerAction& first = read->actions.front();
  CHECK_EQ(first.axes[kAxisForward], kAxisFull);  // full movement forward
  CHECK_EQ(first.buttons, std::uint32_t(0));      // sprint is not held
  CHECK(first.flag);
}

// The same player with sprint.
static void testCapturedSprint() {
  const auto packet = capturedPacket("actions-sprint.bin");
  CHECK(!packet.empty());
  if (packet.empty()) return;

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read || read->actions.empty()) return;
  const PlayerAction& first = read->actions.front();
  CHECK_EQ(first.axes[kAxisForward], kAxisFull);
  CHECK_EQ(first.buttons, kButtonSprint);
}

// And him again, standing still.
static void testCapturedStill() {
  const auto packet = capturedPacket("actions-still.bin");
  CHECK(!packet.empty());
  if (packet.empty()) return;

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read || read->actions.empty()) return;
  for (const std::int16_t axis : read->actions.front().axes) CHECK_EQ(axis, std::int16_t(0));
  CHECK_EQ(read->actions.front().buttons, std::uint32_t(0));
}

// The input counter grows by one per packet — by it the server lines the sets up
// in the right order.
static void testCapturedTickIsSane() {
  const auto run = readPlayerActions(capturedPacket("actions-run.bin"));
  const auto sprint = readPlayerActions(capturedPacket("actions-sprint.bin"));
  CHECK(run.has_value() && sprint.has_value());
  if (!run || !sprint) return;
  // The sprint in the dump came later than the start of the run.
  CHECK(sprint->tick > run->tick);
}

TEST_MAIN({
  testRoundTrip();
  testNegativeAxes();
  testPacketWithoutActions();
  testCapturedRun();
  testCapturedSprint();
  testCapturedStill();
  testCapturedTickIsSane();
})
