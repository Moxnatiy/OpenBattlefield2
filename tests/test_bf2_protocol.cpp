#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/bitstream.h"

using namespace obf2;
using namespace obf2::net::bf2;

static void testHeaderIsTwelveBits() {
  // The engine's header is 4 bits of type and 8 bits of id, low bits first.
  const auto packet = writeShortPacket(PacketKind::ConnectAcceptAck, 0x2A);
  CHECK_EQ(packet.size(), std::size_t(2));
  if (packet.size() < 2) return;

  // 4 = the type, then the id 0x2A: the id's low four bits land in the first byte's
  // high four bits.
  CHECK_EQ(static_cast<unsigned>(packet[0]), 0xA4u);
  CHECK_EQ(static_cast<unsigned>(packet[1]), 0x02u);

  const auto parsed = readPacket(packet);
  CHECK(parsed.has_value());
  if (!parsed) return;
  CHECK(parsed->kind == PacketKind::ConnectAcceptAck);
  CHECK_EQ(parsed->connectionId, 0x2A);
}

static void testConnectRequestLayout() {
  ConnectRequest request;
  request.password = "";
  request.modDirectory = "mods/bf2";

  const auto packet = writeConnectRequest(request);
  // 12 bits of header + 32 + 32 + 1 + 32 + 256 + 256 = 621 bits -> 78 bytes.
  CHECK_EQ(packet.size(), std::size_t(78));
}

static void testAcceptIsParsed() {
  // We assemble the reply the way sendConnectAccept writes it and read it back.
  std::vector<std::byte> buffer(16);
  net::BitWriter writer(buffer);
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::ConnectAccept), 4);
  writer.writeBits(7, 8);        // the connection id in the header
  writer.writeBits(3, 8);        // the assigned id
  writer.writeBits(123456, 32);  // the server's time
  writer.writeBits(0, 1);        // PunkBuster off
  buffer.resize(writer.byteSize());

  const auto parsed = readPacket(buffer);
  CHECK(parsed.has_value());
  if (!parsed || !parsed->accept) { CHECK(false); return; }
  CHECK_EQ(parsed->accept->connectionId, 3);
  CHECK_EQ(parsed->accept->serverTime, 123456u);
  CHECK(!parsed->accept->punkBuster);
}

static void testDeniedCarriesModDirectory() {
  std::vector<std::byte> buffer(64);
  net::BitWriter writer(buffer);
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::ConnectDenied), 4);
  writer.writeBits(0, 8);
  writer.writeBits(static_cast<std::uint32_t>(DenyReason::WrongMod), 32);
  writer.writeBits(1, 1);  // to this reason the server adds its own directory
  const std::string mod = "mods/xpack";
  for (std::size_t i = 0; i < 32; ++i) {
    writer.writeBits(i < mod.size() ? static_cast<std::uint8_t>(mod[i]) : 0u, 8);
  }
  buffer.resize(writer.byteSize());

  const auto parsed = readPacket(buffer);
  CHECK(parsed.has_value());
  if (!parsed || !parsed->denied) { CHECK(false); return; }
  CHECK(parsed->denied->reason == DenyReason::WrongMod);
  CHECK_EQ(parsed->denied->modDirectory, mod);
}

static void testVersionIsBuildNumber() {
  // The version bytes read as 1.5 and build 0x0C51 = 3153 — exactly the one in the
  // official server's name, bf2-linuxded-1.5.3153.0.
  CHECK_EQ((kGameVersion >> 24) & 0xFF, 0x15u);
  CHECK_EQ((kGameVersion >> 8) & 0xFFFF, 0x0C51u);
  CHECK_EQ(0x0C51u, 3153u);
}

static void testChallengeEventIsDecoded() {
  // We assemble a data packet the way the server sends it and read it back.
  std::vector<std::byte> buffer(64);
  net::BitWriter writer(buffer);
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::Data), 4);
  writer.writeBits(0, 8);
  writer.writeBits(1, 6);   // the packet number
  writer.writeBits(0, 6);   // the acknowledgement
  writer.writeBits(0, 32);  // the mask
  for (unsigned i = 0; i < kStreamFramingBits; ++i) writer.writeBits(0, 1);

  writer.writeBits(0, 1);   // the player action stream: no actions
  writer.writeBits(1, 1);   // there are events
  writer.writeBits(1, 8);   // exactly one
  writer.writeBits(0, 5);
  writer.writeBits(0, 1);
  writer.writeBits(1, kEventTypeBits);  // type 1 — the challenge

  const std::string challenge = "nytkkbgzj";
  for (int i = 0; i < 10; ++i) {
    writer.writeBits(i < static_cast<int>(challenge.size())
                         ? static_cast<std::uint8_t>(challenge[i])
                         : 0u,
                     8);
  }
  const std::string mod = "bf2";
  writer.writeBits(static_cast<std::uint32_t>(mod.size()), 8);
  for (const char c : mod) writer.writeBits(static_cast<std::uint8_t>(c), 8);
  buffer.resize(writer.byteSize());

  const auto parsed = readPacket(buffer);
  CHECK(parsed.has_value());
  if (!parsed || !parsed->challenge) { CHECK(false); return; }
  CHECK_EQ(parsed->eventCount, 1);
  CHECK_EQ(parsed->challenge->challenge, challenge);
  CHECK_EQ(parsed->challenge->modDirectory, mod);
}

static void testChallengeResponseLayout() {
  net::bf2::ExtendedHeader header;
  header.sequence = 1;
  header.ack = 2;
  header.ackBits = 0xFFFFFFFFu;

  const auto response = makeChallengeResponse("SYNTHETIC-KEY-FOR-TEST", "abCD1234", 0x1234abcdu);
  CHECK_EQ(std::string_view(response.data(), response.size() - 1),
           "b0126ade3ef1232624c029d0e5a2fb671234abcdadc15bb3d801bac3b6ddf08fb7f82cea");
  CHECK_EQ(response.back(), '\0');
  CHECK(response != makeChallengeResponse("SYNTHETIC-KEY-FOR-TEST", "abCD1235", 0x1234abcdu));
  const auto packet = writeChallengeResponse(0, header, 0, response);
  // 12 basic + 44 extended + 16 length + 1 actions + 15 events + 7 type
  // + 584 block + 32 + 32 + 32 + 1 ghosts = 776 bits -> 97 bytes.
  CHECK_EQ(packet.size(), std::size_t(97));

  // We read the key fields back: the packet's type, the id, and that an event is inside.
  net::BitReader reader(packet);
  CHECK_EQ(reader.readBits(4).value_or(0), static_cast<std::uint32_t>(PacketKind::Data));
  CHECK_EQ(reader.readBits(8).value_or(0), 0u);
  CHECK_EQ(reader.readBits(6).value_or(0), 1u);
  CHECK_EQ(reader.readBits(6).value_or(0), 2u);
  CHECK_EQ(reader.readBits(32).value_or(0), 0xFFFFFFFFu);
  // The payload's length: the whole packet minus 9 bytes of header.
  CHECK_EQ(reader.readBits(16).value_or(0), static_cast<std::uint32_t>(packet.size() - 9));
  CHECK_EQ(reader.readBits(1).value_or(9), 0u);  // no player actions
  CHECK_EQ(reader.readBits(1).value_or(0), 1u);  // there are events
  CHECK_EQ(reader.readBits(8).value_or(0), 1u);  // exactly one
  reader.readBits(5);
  reader.readBits(1);
  CHECK_EQ(reader.readBits(kEventTypeBits).value_or(0), 2u);  // type 2

  for (const char byte : response) {
    CHECK_EQ(reader.readBits(8).value_or(256), static_cast<std::uint32_t>(byte));
  }
  CHECK_EQ(reader.readBits(32).value_or(99), 0u);
  CHECK_EQ(reader.readBits(32).value_or(0), kGameVersion);

  const auto combined = writeChallengeResponse(0, header, 0, response, 0u);
  CHECK_EQ(combined.size(), 99u);
  net::BitReader initial(combined);
  CHECK(initial.skipBits(74));
  CHECK_EQ(initial.readBits(8).value_or(0), 2u);
  CHECK(initial.skipBits(6));
  CHECK_EQ(initial.readBits(kEventTypeBits).value_or(0), 3u);
  CHECK_EQ(initial.readBits(3).value_or(99), 0u);
  CHECK_EQ(initial.readBits(kEventTypeBits).value_or(0), 2u);
}

static void testPingBookkeeping() {
  net::bf2::DataAcknowledgement received;
  CHECK_EQ(received.mask, 0u);
  received.receive(62);
  received.receive(0);
  CHECK_EQ(received.sequence, 0u);
  CHECK_EQ(received.mask, 5u);
  received.receive(63);
  received.receive(0);
  CHECK_EQ(received.mask, 7u);
  received.receive(1);
  CHECK_EQ(received.sequence, 1u);
  CHECK_EQ(received.mask, 15u);

  const auto reply = writePingResponse(0, {63, 0, 0}, 1234, 5678);
  CHECK_EQ(reply.size(), 16u);
  net::BitReader reader(reply);
  CHECK_EQ(reader.readBits(4).value_or(0), 8u);
  CHECK(reader.skipBits(8));
  CHECK_EQ(reader.readBits(6).value_or(0), 63u);
  CHECK_EQ(reader.readBits(6).value_or(99), 0u);
  CHECK_EQ(reader.readBits(32).value_or(99), 0u);
  CHECK_EQ(reader.readBits(1).value_or(99), 0u);
  CHECK_EQ(reader.readBits(32).value_or(0), 1234u);
  CHECK_EQ(reader.readBits(32).value_or(0), 5678u);
}

static void testClientInfoBatch() {
  net::bf2::ClientInfo info;
  info.name = "Player";
  info.profileId = 100000123;
  const auto blob = net::bf2::buildClientInfo(info);
  const auto packet = net::bf2::writeDataBlockStart(
      0, {2, 2, 3}, 1, net::bf2::kClientInfoBlock, static_cast<std::uint32_t>(blob.size()), blob);
  CHECK_EQ(packet.size(), 44u);
  const auto events = net::bf2::readEvents(packet);
  CHECK_EQ(events.size(), 2u);
  if (events.size() != 2 || !events[0].block || !events[1].block) {
    CHECK(false);
    return;
  }
  net::bf2::DataBlockAssembler assembler;
  CHECK(!assembler.feed(*events[0].block));
  const auto completed = assembler.feed(*events[1].block);
  CHECK(completed.has_value());
  if (completed) {
    CHECK_EQ(completed->first, net::bf2::kClientInfoBlock);
    CHECK(completed->second == blob);
    net::BitReader identity(completed->second);
    const auto length = identity.readBits(16).value_or(0);
    for (unsigned i = 0; i < length; ++i) identity.readBits(8);
    identity.readBits(32);
    CHECK_EQ(identity.readBits(1).value_or(1), 0u);
    CHECK_EQ(identity.readBits(31).value_or(0), 100000123u);
  }
}

// The goodbye: type 5, the connection, then the 32-bit reason (13, as
// `GameClient::disconnect` closes) — 44 bits, six bytes.
static void testDisconnectCarriesItsReason() {
  const auto packet = obf2::net::bf2::writeDisconnect(7);
  CHECK_EQ(packet.size(), std::size_t(6));
  obf2::net::BitReader reader(packet);
  CHECK(reader.readBits(4) == 5u);
  CHECK(reader.readBits(8) == 7u);
  CHECK(reader.readBits(32) == 13u);
}

TEST_MAIN({
  testDisconnectCarriesItsReason();
  testClientInfoBatch();
  testPingBookkeeping();
  testChallengeResponseLayout();
  testChallengeEventIsDecoded();
  testHeaderIsTwelveBits();
  testConnectRequestLayout();
  testAcceptIsParsed();
  testDeniedCarriesModDirectory();
  testVersionIsBuildNumber();
})
