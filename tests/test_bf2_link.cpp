// The connection's delivery (obf2/net/bf2_link.h) as OpenBF2 reconstructs the
// engine's `SWConnection` and `GameEventManager`: numbering, acknowledgement,
// one block per packet, and the resend of a lost block under its own number.
#include <cstddef>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_link.h"

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

TEST_MAIN({
  testOneBlockPerPacket();
  testLostBlockIsResent();
  testOurAcknowledgement();
})
