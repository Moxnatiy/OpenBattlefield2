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

static void testChallengeEventIsDecoded() {
  // Збираємо пакет даних так, як його шле сервер, і читаємо назад.
  std::vector<std::byte> buffer(64);
  net::BitWriter writer(buffer);
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::Data), 4);
  writer.writeBits(0, 8);
  writer.writeBits(1, 6);   // номер пакета
  writer.writeBits(0, 6);   // підтвердження
  writer.writeBits(0, 32);  // маска
  for (unsigned i = 0; i < kStreamFramingBits; ++i) writer.writeBits(0, 1);

  writer.writeBits(0, 1);   // потік дій гравця: дій немає
  writer.writeBits(1, 1);   // події є
  writer.writeBits(1, 8);   // рівно одна
  writer.writeBits(0, 5);
  writer.writeBits(0, 1);
  writer.writeBits(1, kEventTypeBits);  // тип 1 — виклик

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

  const auto packet = writeChallengeResponse(0, header, 0);
  // 12 базового + 44 розширеного + 16 довжини + 1 дій + 15 подій + 7 типу
  // + 584 блоку + 32 + 32 + 32 + 1 привидів = 776 бітів -> 97 байтів.
  CHECK_EQ(packet.size(), std::size_t(97));

  // Читаємо назад ключові поля: тип пакета, номер, і що всередині подія.
  net::BitReader reader(packet);
  CHECK_EQ(reader.readBits(4).value_or(0), static_cast<std::uint32_t>(PacketKind::Data));
  CHECK_EQ(reader.readBits(8).value_or(0), 0u);
  CHECK_EQ(reader.readBits(6).value_or(0), 1u);
  CHECK_EQ(reader.readBits(6).value_or(0), 2u);
  CHECK_EQ(reader.readBits(32).value_or(0), 0xFFFFFFFFu);
  // Довжина корисної частини: увесь пакет мінус 9 байтів заголовка.
  CHECK_EQ(reader.readBits(16).value_or(0), static_cast<std::uint32_t>(packet.size() - 9));
  CHECK_EQ(reader.readBits(1).value_or(9), 0u);  // дій гравця немає
  CHECK_EQ(reader.readBits(1).value_or(0), 1u);  // події є
  CHECK_EQ(reader.readBits(8).value_or(0), 1u);  // рівно одна
  reader.readBits(5);
  reader.readBits(1);
  CHECK_EQ(reader.readBits(kEventTypeBits).value_or(0), 2u);  // тип 2

  // Пропускаємо блок і перевіряємо, що далі йде мережева версія.
  for (int i = 0; i < 73; ++i) reader.readBits(8);
  reader.readBits(32);
  CHECK_EQ(reader.readBits(32).value_or(0), kGameVersion);
}

TEST_MAIN({
  testChallengeResponseLayout();
  testChallengeEventIsDecoded();
  testHeaderIsTwelveBits();
  testConnectRequestLayout();
  testAcceptIsParsed();
  testDeniedCarriesModDirectory();
  testVersionIsBuildNumber();
})
