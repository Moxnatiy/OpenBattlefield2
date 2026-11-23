// The connection's delivery (obf2/net/bf2_link.h) as OpenBF2 reconstructs the
// engine's `SWConnection` and `GameEventManager`: numbering, acknowledgement,
// one block per packet, and the resend of a lost block under its own number.
#include <cstddef>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_link.h"
#include "obf2/net/chat_string.h"

using obf2::net::BitReader;
using namespace obf2::net::bf2;
namespace bf2 = obf2::net::bf2;

namespace {

struct Section {
  bool present = false;
  unsigned count = 0, block = 0, resend = 0;
};

// The event section of a packet `writeDataPacket` made, with no actions.
Section sectionOf(const std::vector<std::byte>& packet) {
  BitReader reader(packet);
  reader.skipBits(4 + 8 + 6 + 6 + 32 + 16);
  reader.skipBits(1);  // no actions
  Section out;
  out.present = reader.readBits(1).value_or(0) == 1;
  if (!out.present) return out;
  out.count = reader.readBits(8).value_or(0);
  out.block = reader.readBits(5).value_or(0);
  out.resend = reader.readBits(1).value_or(0);
  return out;
}

bf2::ExtendedHeader fromServer(unsigned sequence, unsigned ack, std::uint32_t mask) {
  bf2::ExtendedHeader header;
  header.sequence = static_cast<std::uint8_t>(sequence);
  header.ack = static_cast<std::uint8_t>(ack);
  header.ackBits = mask;
  return header;
}

}  // namespace

// Data packets are numbered from 1; queued events all go in the next packet as
// one block, block 0 first.
static void testOneBlockPerPacket() {
  Link link;
  link.queue(postRemoteEvent(6, 7, 2));
  link.queue(postRemoteEvent(6, 8, 0));
  link.queue(postRemoteEvent(6, 6, 3));
  const auto packet = writeDataPacket(1, link, nullptr);
  BitReader reader(packet);
  reader.skipBits(4 + 8);
  CHECK(reader.readBits(6) == 1u);
  const Section section = sectionOf(packet);
  CHECK(section.present && section.count == 3 && section.block == 0 && section.resend == 0);
  CHECK(!link.hasEventsToSend());
  // The next one carries nothing.
  CHECK(!sectionOf(writeDataPacket(1, link, nullptr)).present);
}

// The server acknowledges packet 1 but not 2: block 0 is retired, block 1 comes
// back whole, under its own number, with the resend bit.
static void testLostBlockIsResent() {
  Link link;
  link.queue(postRemoteEvent(6, 2));
  writeDataPacket(1, link, nullptr);  // packet 1, block 0
  link.queue(postRemoteEvent(6, 4));
  writeDataPacket(1, link, nullptr);  // packet 2, block 1
  writeDataPacket(1, link, nullptr);  // packet 3, nothing
  // Acknowledged up to 3: 3 and 1 arrived, 2 did not (the mask's low bit is 3).
  CHECK(link.acceptData(fromServer(1, 3, 0b101)));
  CHECK_EQ(link.packetsLost(), 1);
  CHECK(link.hasEventsToSend());
  const Section again = sectionOf(writeDataPacket(1, link, nullptr));
  CHECK(again.present && again.count == 1 && again.block == 1 && again.resend == 1);
  CHECK_EQ(link.blocksResent(), 1);
}

// Our acknowledgement of the server: the last number and the mask with a one per
// packet received and a zero per gap; a repeat is dropped.
static void testOurAcknowledgement() {
  Link link;
  CHECK(link.acceptData(fromServer(1, 0, 0)));
  CHECK(link.acceptData(fromServer(2, 0, 0)));
  CHECK(link.acceptData(fromServer(4, 0, 0)));  // 3 lost
  CHECK(!link.acceptData(fromServer(4, 0, 0)));  // the same again: dropped
  const bf2::ExtendedHeader header = link.nextDataHeader();
  CHECK_EQ(static_cast<int>(header.ack), 4);
  CHECK_EQ(header.ackBits & 0xFu, 0b1101u);
  // A ping response takes the last number sent, not a new one.
  CHECK_EQ(static_cast<int>(link.pingHeader().sequence), static_cast<int>(header.sequence));
}

static void testChallengeMatchesExistingEncoding() {
  Link link;
  CHECK_EQ(link.pingHeader().sequence, 63);
  const auto response = makeChallengeResponse("00001111222233334444", "challenge", 123);
  link.queue(connectionTypeEvent(0));
  link.queue(challengeResponseEvent(response));
  CHECK(writeDataPacket(1, link, nullptr) ==
        writeChallengeResponse(1, ExtendedHeader{1, 0, 0}, 0, response, 0));
}

static void testCommanderRoleAndPointOrder() {
  Link link;
  link.queue(commanderEvent(CommanderAction::Apply, 2, 7));
  link.queue(commanderToolEvent(CommanderTool::Defend, 2, 4, {1.0f, 2.0f, 3.0f}));
  const auto packet = writeDataPacket(1, link, nullptr);
  const auto section = sectionOf(packet);
  CHECK_EQ(section.count, 2u);
  BitReader reader(packet);
  CHECK(reader.skipBits(4 + 8 + 6 + 6 + 32 + 16 + 1 + 1 + 8 + 5 + 1));
  CHECK(reader.readBits(kEventTypeBits) == 19u);
  CHECK(reader.readBits(4) == 4u);
  CHECK(reader.readBits(8) == 2u);
  CHECK(reader.readBits(1) == 0u);
  CHECK(reader.readBits(15) == 7u);
  CHECK(reader.readBits(kEventTypeBits) == 24u);
  CHECK(reader.readBits(8) == 4u);
  CHECK(reader.readBits(8) == 2u);
  CHECK(reader.readBits(8) == 6u);
  CHECK(reader.readBits(1) == 0u);
  for (const auto value : {0x3f800000u, 0x40000000u, 0x40400000u}) {
    CHECK(reader.readBits(32) == value);
    CHECK(reader.readBits(32) == 0u);
    CHECK(reader.readBits(32) == 0u);
  }
  CHECK(reader.readBits(8) == 1u);
  CHECK(reader.readBits(1) == 0u);
}

static void testCommanderAssetWireAndAssignment() {
  const auto checkBits = [](const EventBits& event,
      std::initializer_list<std::pair<unsigned, std::uint32_t>> fields) {
    std::array<std::byte, 64> buffer{};
    obf2::net::BitWriter writer(buffer);
    event.write(writer);
    BitReader reader(buffer);
    for (const auto& [width, value] : fields) CHECK(reader.readBits(width) == value);
    CHECK_EQ(reader.bitPosition(), event.bitCount());
  };
  constexpr obf2::Vec3f position{1, 2, 3};
  constexpr unsigned x = 0x3f800000, y = 0x40000000, z = 0x40400000;
  checkBits(commanderCameraEvent(position), {{kEventTypeBits, 36}, {32, x}, {32, y}, {32, z}});
  checkBits(commanderToolEvent(CommanderTool::Scan, 2, 0, position),
            {{kEventTypeBits, 45}, {32, x}, {32, z}, {8, 2}, {1, 1}});
  checkBits(commanderToolEvent(CommanderTool::UAV, 2, 0, position),
            {{kEventTypeBits, 45}, {32, x}, {32, z}, {8, 2}, {1, 0}});
  checkBits(commanderToolEvent(CommanderTool::Artillery, 2, 0, position),
            {{kEventTypeBits, 30}, {2, 1}, {32, 2}, {32, x}, {32, z}});
  checkBits(commanderToolEvent(CommanderTool::Supplies, 2, 0, position),
            {{kEventTypeBits, 37}, {32, 2}, {32, x}, {32, z}});
  checkBits(commanderToolEvent(CommanderTool::Vehicle, 2, 0, position),
            {{kEventTypeBits, 51}, {32, x}, {32, z}, {8, 2}, {1, 0}});
  for (const int player : {7, -1}) {
    std::array<std::byte, 8> buffer{};
    obf2::net::BitWriter writer(buffer);
    EventBits assignment;
    assignment.put(19, kEventTypeBits).put(3, 4).put(2, 8)
        .put(player < 0, 1).put(static_cast<unsigned>(std::abs(player)), 15);
    assignment.write(writer);
    BitReader reader(buffer);
    const auto event = readEvent(reader);
    CHECK(event && event->commanderAssignment && event->commanderAssignment->player == player &&
          event->commanderAssignment->team == 2);
  }
}

// The server's blocks are executed in their order: one that overtook a lost
// block waits for it, a repeat is dropped (`processReceivedPacket`).
static void testReceivedBlocksInOrder() {
  Link link;
  auto one = [](std::uint32_t type) {
    bf2::Event event;
    event.type = type;
    return std::vector<bf2::Event>{event};
  };
  auto first = link.receiveBlock(0, one(10));
  CHECK_EQ(first.size(), std::size_t{1});
  CHECK(link.receiveBlock(2, one(12)).empty());  // block 1 lost: 2 waits
  CHECK_EQ(link.blocksHeldBack(), 1);
  CHECK_EQ(link.blocksOutOfOrder(), 1);
  const auto both = link.receiveBlock(1, one(11));  // the resend frees both
  CHECK_EQ(both.size(), std::size_t{2});
  CHECK_EQ(both[0][0].type, 11u);
  CHECK_EQ(both[1][0].type, 12u);
  CHECK_EQ(link.blocksHeldBack(), 0);
  CHECK(link.receiveBlock(1, one(11)).empty());  // a repeat
  CHECK_EQ(link.blocksRepeated(), 1);
  // The index wraps at 32.
  for (unsigned i = 3; i < 34; ++i) CHECK_EQ(link.receiveBlock(i, one(i)).size(), std::size_t{1});
}

static void testMatchCommunicationWire() {
  for (const auto channel : {ChatChannel::All, ChatChannel::Team, ChatChannel::Squad}) {
    const std::string message(kMaxChatBytes, 'x');
    ChatAssembler assembler;
    std::optional<ChatString> result;
    const auto events = chatEvents(channel, 19, message);
    CHECK_EQ(events.size(), std::size_t{6});
    for (const auto& bits : events) {
      std::array<std::byte, 80> buffer{};
      obf2::net::BitWriter writer(buffer);
      bits.write(writer);
      BitReader reader(buffer);
      const auto decoded = readEvent(reader);
      CHECK(decoded && decoded->stringBlock);
      if (!decoded || !decoded->stringBlock) return;
      const auto& block = *decoded->stringBlock;
      CHECK_EQ(reader.bitPosition(), bits.bitCount());
      if (block.isNew) CHECK(assembler.begin(block.size));
      else result = assembler.add(block.chunk);
    }
    CHECK(result);
    if (result) {
      CHECK_EQ(result->text, message);
      CHECK_EQ(result->channel, static_cast<int>(channel));
      CHECK_EQ(result->type, 2);
      CHECK_EQ(result->sender, 19);
      CHECK_EQ(result->target, 255);
    }
  }
  CHECK(chatEvents(ChatChannel::All, 1, "").empty());
  CHECK(chatEvents(ChatChannel::All, 1, "x\ny").empty());
  CHECK(chatEvents(ChatChannel::All, 1, std::string(kMaxChatBytes + 1, 'x')).empty());

  const auto decode = [](const EventBits& bits) {
    std::array<std::byte, 64> buffer{};
    obf2::net::BitWriter writer(buffer);
    bits.write(writer);
    BitReader reader(buffer);
    auto event = readEvent(reader);
    CHECK(event);
    CHECK_EQ(reader.bitPosition(), bits.bitCount());
    return event.value_or(Event{});
  };
  const auto radio = decode(radioCallEvent({61, 7, 2, 0, 17, 3})).radioCall;
  CHECK(radio && radio->message == 61 && radio->sender == 7 && radio->channel == 17);
  EventBits leaderChanged;
  leaderChanged.put(28, kEventTypeBits).put(2, 8).put(7, 8).put(4, 8);
  const auto leader = decode(leaderChanged).squadLeader;
  CHECK(leader && leader->player == 7 && leader->squad == 4 && leader->team == 2);
  CHECK_EQ(decode(inviteSquadEvent(2, 7, 4, true)).type, 25u);
  CHECK_EQ(decode(spotEvent(177, 222, 2, {15, 8, -22})).type, 29u);
  SquadOrder order{4, 2, 5, 0, {{{15, 8, -22}, {2, 9, 3}, {4, 5, 6}}},
                   OrderRoute::Commander, false};
  const auto decoded = decode(squadOrderEvent(order)).squadOrder;
  CHECK(decoded && decoded->id == 5 && decoded->points[0].z == -22 && decoded->points[1].y == 9);
  order.target = 222;
  const auto target = decode(squadOrderEvent(order)).squadOrder;
  CHECK(target && target->target == 222 && target->route == OrderRoute::Commander);
  const auto ack = decode(orderResponseEvent({4, 2, true, OrderRoute::Commander, 5})).orderResponse;
  CHECK(ack && ack->accepted && ack->id == 5 && ack->team == 2 && ack->squad == 4);
  for (bool accepted : {false, true}) {
    const auto reply = commanderOrderReplyEvent(4, 2, accepted);
    CHECK_EQ(decode(reply).type, 50u);
    std::array<std::byte, 8> buffer{};
    obf2::net::BitWriter writer(buffer);
    reply.write(writer);
    BitReader reader(buffer);
    CHECK(reader.skipBits(kEventTypeBits + 8 + 8));
    CHECK_EQ(reader.readBits(3).value_or(0), accepted ? 2u : 3u);
  }
}

TEST_MAIN({
  testMatchCommunicationWire();
  testCommanderRoleAndPointOrder();
  testCommanderAssetWireAndAssignment();
  testChallengeMatchesExistingEncoding();
  testReceivedBlocksInOrder();
  testOneBlockPerPacket();
  testLostBlockIsResent();
  testOurAcknowledgement();
})
