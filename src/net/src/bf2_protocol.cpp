#include "obf2/net/bf2_protocol.h"

#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {
namespace {

// Рядки в запиті — рівно 32 байти з доповненням нулями (рушій читає
// 0x100 бітів і далі поводиться з ними як із C-рядком).
constexpr std::size_t kStringBytes = 32;

void writeFixedString(BitWriter& writer, const std::string& text) {
  for (std::size_t i = 0; i < kStringBytes; ++i) {
    const std::uint32_t byte = i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0u;
    writer.writeBits(byte, 8);
  }
}

std::string readFixedString(BitReader& reader) {
  std::string out;
  bool ended = false;
  for (std::size_t i = 0; i < kStringBytes; ++i) {
    const auto value = reader.readBits(8);
    if (!value) break;  // пакет обірвано
    const auto byte = static_cast<char>(*value);
    if (byte == '\0') ended = true;
    if (!ended) out.push_back(byte);
  }
  return out;
}

}  // namespace

std::string_view denyReasonName(DenyReason reason) {
  switch (reason) {
    case DenyReason::ServerFull: return "сервер повний";
    case DenyReason::VersionMismatch: return "версія не підходить";
    case DenyReason::WrongPassword: return "невірний пароль";
    case DenyReason::Banned: return "адресу заблоковано";
    case DenyReason::ClientTooOld: return "клієнт застарий";
    case DenyReason::ClientTooNew: return "клієнт занадто новий";
    case DenyReason::NoFreeSlots: return "немає вільних місць";
    case DenyReason::PunkBusterRequired: return "потрібен PunkBuster";
    case DenyReason::WrongMod: return "інша тека мода";
  }
  return "невідома причина";
}

std::vector<std::byte> writeConnectRequest(const ConnectRequest& request) {
  std::vector<std::byte> buffer(128);
  BitWriter writer(buffer);

  writer.writeBits(static_cast<std::uint32_t>(PacketKind::ConnectRequest), 4);
  writer.writeBits(0, 8);  // номера з'єднання ще немає
  writer.writeBits(request.magic, 32);
  writer.writeBits(request.version, 32);
  writer.writeBits(request.punkBuster ? 1u : 0u, 1);
  writer.writeBits(request.reconnectToken, 32);
  // Спершу пароль, потім тека мода — саме в такому порядку їх читає рушій.
  writeFixedString(writer, request.password);
  writeFixedString(writer, request.modDirectory);

  buffer.resize(writer.byteSize());
  return buffer;
}

std::vector<std::byte> writeShortPacket(PacketKind kind, std::uint8_t connectionId) {
  std::vector<std::byte> buffer(4);
  BitWriter writer(buffer);
  writer.writeBits(static_cast<std::uint32_t>(kind), 4);
  writer.writeBits(connectionId, 8);
  buffer.resize(writer.byteSize());
  return buffer;
}

std::vector<std::byte> writePingResponse(std::uint8_t connectionId, const ExtendedHeader& header,
                                         std::uint32_t time) {
  std::vector<std::byte> buffer(32);
  BitWriter writer(buffer);
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::PingResponse), 4);
  writer.writeBits(connectionId, 8);
  writer.writeBits(header.sequence & 0x3F, 6);
  writer.writeBits(header.ack & 0x3F, 6);
  writer.writeBits(header.ackBits, 32);
  // Прапорець підтвердження, далі час сервера й наш власний.
  writer.writeBits(1, 1);
  writer.writeBits(time, 32);
  writer.writeBits(time, 32);
  buffer.resize(writer.byteSize());
  return buffer;
}

std::optional<Incoming> readPacket(std::span<const std::byte> data) {
  if (data.size() * 8 < 12) return std::nullopt;
  BitReader reader(data);

  const auto kind = reader.readBits(4);
  const auto id = reader.readBits(8);
  if (!kind || !id) return std::nullopt;

  Incoming incoming;
  incoming.kind = static_cast<PacketKind>(*kind);
  incoming.connectionId = static_cast<std::uint8_t>(*id);

  if (incoming.kind == PacketKind::ConnectAccept) {
    const auto assigned = reader.readBits(8);
    const auto time = reader.readBits(32);
    const auto punkBuster = reader.readBits(1);
    if (!assigned || !time || !punkBuster) return incoming;  // обірваний пакет

    ConnectAccept accept;
    accept.connectionId = static_cast<std::uint8_t>(*assigned);
    accept.serverTime = *time;
    accept.punkBuster = *punkBuster != 0;
    incoming.accept = accept;
  } else if (incoming.kind == PacketKind::ConnectDenied) {
    const auto reason = reader.readBits(32);
    const auto hasMod = reader.readBits(1);
    if (!reason) return incoming;

    ConnectDenied denied;
    denied.reason = static_cast<DenyReason>(*reason);
    // Теку сервер додає лише коли причина — інший мод.
    if (hasMod && *hasMod != 0) denied.modDirectory = readFixedString(reader);
    incoming.denied = denied;
  } else if (incoming.kind == PacketKind::PingRequest || incoming.kind == PacketKind::Data ||
             incoming.kind == PacketKind::PingResponse) {
    const auto sequence = reader.readBits(6);
    const auto ack = reader.readBits(6);
    const auto ackBits = reader.readBits(32);
    if (!sequence || !ack || !ackBits) return incoming;

    ExtendedHeader header;
    header.sequence = static_cast<std::uint8_t>(*sequence);
    header.ack = static_cast<std::uint8_t>(*ack);
    header.ackBits = *ackBits;
    incoming.extended = header;

    if (incoming.kind == PacketKind::PingRequest) {
      // Після заголовка йде прапорець і час сервера.
      reader.readBits(1);
      if (const auto time = reader.readBits(32)) incoming.pingTime = *time;
    } else if (incoming.kind == PacketKind::Data) {
      // Каркас потоків, який ще не розібрано, — просто пропускаємо.
      for (unsigned i = 0; i < kStreamFramingBits; ++i) reader.readBits(1);

      const auto hasEvents = reader.readBits(1);
      if (!hasEvents || *hasEvents != 1) return incoming;
      const auto count = reader.readBits(8);
      reader.readBits(5);
      reader.readBits(1);
      if (!count) return incoming;
      incoming.eventCount = static_cast<int>(*count);

      // Розбираємо лише подію-виклик: решта типів ще попереду.
      const auto type = reader.readBits(kEventTypeBits);
      if (type && *type == 1) {
        ChallengeEvent event;
        // Рядок виклику — рівно 80 бітів, десять байтів із нулем у кінці.
        for (int i = 0; i < 10; ++i) {
          const auto byte = reader.readBits(8);
          if (!byte) break;
          if (*byte != 0) event.challenge.push_back(static_cast<char>(*byte));
        }
        if (const auto length = reader.readBits(8)) {
          for (std::uint32_t i = 0; i < *length && i < 64; ++i) {
            const auto byte = reader.readBits(8);
            if (!byte) break;
            event.modDirectory.push_back(static_cast<char>(*byte));
          }
        }
        incoming.challenge = std::move(event);
      }
    }
  }
  return incoming;
}

}  // namespace obf2::net::bf2
