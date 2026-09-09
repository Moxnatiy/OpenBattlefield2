#include "obf2/net/bf2_protocol.h"

#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {
namespace {

// The strings in the request are exactly 32 bytes padded with zeroes (the engine
// reads 0x100 bits and then treats them as a C string).
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
    if (!value) break;  // the packet is truncated
    const auto byte = static_cast<char>(*value);
    if (byte == '\0') ended = true;
    if (!ended) out.push_back(byte);
  }
  return out;
}


// --- the data packet ---
//
// A data packet's header is exactly 72 bits:
//   4 bits  type, 8 bits connection id
//   6 bits  packet number, 6 bits acknowledgement, 32 bits mask
//   16 bits the payload's length in bytes
//
// The last field was measured against a live server: in its 26-byte packet it
// held 17, which is exactly 26 - 9 bytes of header. Without it all three streams
// read shifted and the server fails its ghostmanager check.
constexpr std::size_t kDataHeaderBytes = 9;

void writeDataHeader(BitWriter& writer, std::uint8_t connectionId, const ExtendedHeader& header) {
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::Data), 4);
  writer.writeBits(connectionId, 8);
  writer.writeBits(header.sequence & 0x3F, 6);
  writer.writeBits(header.ack & 0x3F, 6);
  writer.writeBits(header.ackBits, 32);
  writer.writeBits(0, 16);  // room for the length: filled in once we know it
}

// The event stream's framing. `batch` is the batch number: `GameEventManager`
// puts batches into a tree by this number and hands them to the game only in
// sequence, so the count has to start at zero and grow by one with every batch.
void writeEventFraming(BitWriter& writer, std::uint8_t batch, std::uint8_t count) {
  writer.writeBits(0, 1);  // the player action stream: no actions
  writer.writeBits(1, 1);  // there are events
  writer.writeBits(count, 8);
  writer.writeBits(batch & 0x1F, 5);
  writer.writeBits(0, 1);
}

// The packet's tail: the ghost stream (we send nothing) and the length in the header.
std::vector<std::byte> finishDataPacket(std::vector<std::byte>& buffer, BitWriter& writer) {
  writer.writeBits(0, 1);  // no ghosts
  const auto bytes = writer.byteSize();
  const auto payload = static_cast<std::uint16_t>(bytes - kDataHeaderBytes);
  buffer[7] = static_cast<std::byte>(payload & 0xFF);
  buffer[8] = static_cast<std::byte>(payload >> 8);
  buffer.resize(bytes);
  return buffer;
}

}  // namespace

std::string_view denyReasonName(DenyReason reason) {
  switch (reason) {
    case DenyReason::ServerFull: return "the server is full";
    case DenyReason::VersionMismatch: return "the version does not match";
    case DenyReason::WrongPassword: return "wrong password";
    case DenyReason::Banned: return "the address is banned";
    case DenyReason::ClientTooOld: return "the client is too old";
    case DenyReason::ClientTooNew: return "the client is too new";
    case DenyReason::NoFreeSlots: return "no free slots";
    case DenyReason::PunkBusterRequired: return "PunkBuster is required";
    case DenyReason::WrongMod: return "a different mod directory";
  }
  return "unknown reason";
}

std::vector<std::byte> writeConnectRequest(const ConnectRequest& request) {
  std::vector<std::byte> buffer(128);
  BitWriter writer(buffer);

  writer.writeBits(static_cast<std::uint32_t>(PacketKind::ConnectRequest), 4);
  writer.writeBits(0, 8);  // there is no connection id yet
  writer.writeBits(request.magic, 32);
  writer.writeBits(request.version, 32);
  writer.writeBits(request.punkBuster ? 1u : 0u, 1);
  writer.writeBits(request.reconnectToken, 32);
  // The password first, then the mod's directory — exactly the order the engine reads them in.
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
  // The acknowledgement flag, then the server's time and our own.
  writer.writeBits(1, 1);
  writer.writeBits(time, 32);
  writer.writeBits(time, 32);
  buffer.resize(writer.byteSize());
  return buffer;
}

std::vector<std::byte> writeChallengeResponse(std::uint8_t connectionId,
                                              const ExtendedHeader& header, std::uint8_t batch) {
  std::vector<std::byte> buffer(128);
  BitWriter writer(buffer);
  writeDataHeader(writer, connectionId, header);
  writeEventFraming(writer, batch, 1);

  writer.writeBits(2, kEventTypeBits);  // type 2 — the challenge reply
  // A 73-byte response block: without an authenticator the server does not read it.
  for (int i = 0; i < 73; ++i) writer.writeBits(0, 8);
  writer.writeBits(0, 32);
  writer.writeBits(kGameVersion, 32);
  writer.writeBits(0, 1);       // the sign
  writer.writeBits(0x423, 31);  // BF2's product number

  return finishDataPacket(buffer, writer);
}

std::vector<std::byte> writeDataBlockHeader(std::uint8_t connectionId,
                                            const ExtendedHeader& header, std::uint8_t batch,
                                            std::uint32_t blockType, std::uint32_t size) {
  std::vector<std::byte> buffer(32);
  BitWriter writer(buffer);
  writeDataHeader(writer, connectionId, header);
  writeEventFraming(writer, batch, 1);

  writer.writeBits(kDataBlockEvent, kEventTypeBits);
  writer.writeBits(1, 1);  // this is a block header
  writer.writeBits(blockType, 32);
  writer.writeBits(size, 32);

  return finishDataPacket(buffer, writer);
}

std::vector<std::byte> writeDataBlockChunk(std::uint8_t connectionId, const ExtendedHeader& header,
                                           std::uint8_t batch, std::span<const std::byte> chunk) {
  std::vector<std::byte> buffer(chunk.size() + 32);
  BitWriter writer(buffer);
  writeDataHeader(writer, connectionId, header);
  writeEventFraming(writer, batch, 1);

  writer.writeBits(kDataBlockEvent, kEventTypeBits);
  writer.writeBits(0, 1);  // this is a data chunk
  writer.writeBits(static_cast<std::uint32_t>(chunk.size()), 8);
  for (const auto byte : chunk) writer.writeBits(std::to_integer<std::uint32_t>(byte), 8);

  return finishDataPacket(buffer, writer);
}

std::vector<std::byte> writePlayerActions(std::uint8_t connectionId, const ExtendedHeader& header,
                                          const PlayerActions& stream) {
  std::vector<std::byte> buffer(128);
  BitWriter writer(buffer);
  writeDataHeader(writer, connectionId, header);

  writer.writeBits(1, 1);  // there are actions
  WriteCursor cursor(writer);
  PlayerActions copy = stream;  // the cursor works with a mutable reference
  serializePlayerActions(cursor, copy);

  writer.writeBits(0, 1);  // no events
  return finishDataPacket(buffer, writer);
}

std::optional<PlayerActions> readPlayerActions(std::span<const std::byte> packet) {
  BitReader reader(packet);

  const auto kind = reader.readBits(4);
  if (!kind || *kind != static_cast<std::uint32_t>(PacketKind::Data)) return std::nullopt;
  // the connection id, sequence, ack, ackBits and the payload's length
  if (!reader.skipBits(8 + 6 + 6 + 32 + 16)) return std::nullopt;

  const auto hasActions = reader.readBits(1);
  if (!hasActions || *hasActions != 1) return std::nullopt;

  PlayerActions out;
  ReadCursor cursor(reader);
  if (!serializePlayerActions(cursor, out)) return std::nullopt;
  return out;
}

std::vector<std::byte> writePostRemoteEvent(std::uint8_t connectionId,
                                            const ExtendedHeader& header, std::uint8_t batch,
                                            std::uint32_t category, std::uint32_t event,
                                            std::optional<std::int32_t> value) {
  std::vector<std::byte> buffer(32);
  BitWriter writer(buffer);
  writeDataHeader(writer, connectionId, header);
  writeEventFraming(writer, batch, 1);

  writer.writeBits(kPostRemoteEvent, kEventTypeBits);
  writer.writeBits(category, 4);
  writer.writeBits(event, 32);
  writer.writeBits(0, 32);  // the delay: float 0.0
  if (value) {
    writer.writeBits(4, 8);
    const auto raw = static_cast<std::uint32_t>(*value);
    for (int i = 0; i < 4; ++i) writer.writeBits((raw >> (i * 8)) & 0xFF, 8);
  } else {
    writer.writeBits(0, 8);
  }

  return finishDataPacket(buffer, writer);
}

std::vector<std::byte> writeContentCheckEvent(std::uint8_t connectionId,
                                              const ExtendedHeader& header, std::uint8_t batch,
                                              const std::array<std::byte, 16>& misc,
                                              const std::array<std::byte, 16>& archives,
                                              const std::array<std::byte, 16>& level) {
  std::vector<std::byte> buffer(96);
  BitWriter writer(buffer);
  writeDataHeader(writer, connectionId, header);
  writeEventFraming(writer, batch, 1);

  writer.writeBits(kContentCheckEvent, kEventTypeBits);
  for (const auto* hash : {&misc, &archives, &level}) {
    for (const auto byte : *hash) writer.writeBits(std::to_integer<std::uint32_t>(byte), 8);
  }
  return finishDataPacket(buffer, writer);
}

std::optional<std::array<std::byte, 16>> readFingerprint(std::string_view text, int ordinal) {
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t end = text.find('\n', at);
    const std::string_view line = text.substr(at, end == std::string_view::npos ? end : end - at);
    at = end == std::string_view::npos ? text.size() : end + 1;

    // Split into words: the last is the hash itself, the one before it the number.
    std::vector<std::string_view> words;
    std::size_t from = 0;
    while (from < line.size()) {
      const std::size_t space = line.find_first_of(" \t\r", from);
      const std::size_t stop = space == std::string_view::npos ? line.size() : space;
      if (stop > from) words.push_back(line.substr(from, stop - from));
      from = stop + 1;
    }
    if (words.size() < 2) continue;

    const std::string_view number = words[words.size() - 2];
    const std::string_view hash = words.back();
    if (hash.size() != 32) continue;
    int value = 0;
    bool digits = !number.empty();
    for (const char c : number) {
      if (c < '0' || c > '9') { digits = false; break; }
      value = value * 10 + (c - '0');
    }
    if (!digits || value != ordinal) continue;

    std::array<std::byte, 16> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
      const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = digit(hash[i * 2]), lo = digit(hash[i * 2 + 1]);
      if (hi < 0 || lo < 0) return std::nullopt;
      out[i] = static_cast<std::byte>(hi * 16 + lo);
    }
    return out;
  }
  return std::nullopt;
}

std::uint32_t clientInfoNameHash(const std::string& name) {
  std::uint32_t value = 0x1505;
  for (const char raw : name) {
    auto c = static_cast<std::uint32_t>(static_cast<unsigned char>(raw));
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    value = value * 0x21 ^ c;
  }
  return value;
}

std::vector<std::byte> buildClientInfo(const ClientInfo& info) {
  std::vector<std::byte> buffer(info.name.size() + info.clanTag.size() + info.auth.size() + 32);
  BitWriter writer(buffer);

  const auto text = [&writer](const std::string& value) {
    writer.writeBits(static_cast<std::uint32_t>(value.size()), 16);
    for (const char c : value) writer.writeBits(static_cast<std::uint8_t>(c), 8);
  };

  text(info.name);
  writer.writeBits(info.nameHash, 32);
  writer.writeBits(info.profileId < 0 ? 1u : 0u, 1);
  writer.writeBits(static_cast<std::uint32_t>(info.profileId < 0 ? -info.profileId
                                                                : info.profileId),
                   31);
  text(info.clanTag);
  text(info.auth);
  writer.writeBits(info.flag ? 1u : 0u, 1);

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
    if (!assigned || !time || !punkBuster) return incoming;  // a truncated packet

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
    // The server adds the directory only when the reason is a different mod.
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
      // After the header come a flag and the server's time.
      reader.readBits(1);
      if (const auto time = reader.readBits(32)) incoming.pingTime = *time;
    } else if (incoming.kind == PacketKind::Data) {
      // 16 bits of length, then the player action stream — we do not parse it.
      for (unsigned i = 0; i < kStreamFramingBits; ++i) reader.readBits(1);
      reader.readBits(1);

      const auto hasEvents = reader.readBits(1);
      if (!hasEvents || *hasEvents != 1) return incoming;
      const auto count = reader.readBits(8);
      reader.readBits(5);
      reader.readBits(1);
      if (!count) return incoming;
      incoming.eventCount = static_cast<int>(*count);

      // We parse only the challenge event: the other types are still ahead.
      const auto type = reader.readBits(kEventTypeBits);
      if (type && *type == 1) {
        ChallengeEvent event;
        // The challenge string is exactly 80 bits, ten bytes with a zero at the end.
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
