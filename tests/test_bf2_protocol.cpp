#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/bitstream.h"

using namespace obf2;
using namespace obf2::net::bf2;

static void testHeaderIsTwelveBits() {
  // Заголовок рушія — 4 біти типу і 8 бітів номера, молодшими вперед.
  const auto packet = writeShortPacket(PacketKind::ConnectAcceptAck, 0x2A);
  CHECK_EQ(packet.size(), std::size_t(2));
  if (packet.size() < 2) return;

  // 4 = тип, далі номер 0x2A: молодші чотири біти номера потрапляють у
  // старші чотири біти першого байта.
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
  // 12 бітів заголовка + 32 + 32 + 1 + 32 + 256 + 256 = 621 біт -> 78 байтів.
  CHECK_EQ(packet.size(), std::size_t(78));
}

static void testAcceptIsParsed() {
  // Збираємо відповідь так, як її пише sendConnectAccept, і читаємо назад.
  std::vector<std::byte> buffer(16);
  net::BitWriter writer(buffer);
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::ConnectAccept), 4);
  writer.writeBits(7, 8);        // номер з'єднання в заголовку
  writer.writeBits(3, 8);        // виданий номер
  writer.writeBits(123456, 32);  // час сервера
  writer.writeBits(0, 1);        // PunkBuster вимкнено
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
  writer.writeBits(1, 1);  // до цієї причини сервер додає свою теку
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
  // Байти версії читаються як 1.5 і збірка 0x0C51 = 3153 — рівно та, що
  // стоїть у назві офіційного сервера bf2-linuxded-1.5.3153.0.
  CHECK_EQ((kGameVersion >> 24) & 0xFF, 0x15u);
  CHECK_EQ((kGameVersion >> 8) & 0xFFFF, 0x0C51u);
  CHECK_EQ(0x0C51u, 3153u);
}

TEST_MAIN({
  testHeaderIsTwelveBits();
  testConnectRequestLayout();
  testAcceptIsParsed();
  testDeniedCarriesModDirectory();
  testVersionIsBuildNumber();
})
