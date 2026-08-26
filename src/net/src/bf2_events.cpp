#include "obf2/net/bf2_events.h"

#include <cstdio>
#include <cstring>

#include "obf2/net/bf2_protocol.h"

namespace obf2::net::bf2 {
namespace {

struct EventInfo {
  std::uint32_t type;
  std::string_view name;
  bool branchy;
  const unsigned* widths;
  std::size_t count;
};

#define BF2_EVENT(number, label, ...) \
  constexpr unsigned kWidths##number[] = {__VA_ARGS__};
#define BF2_EVENT_BRANCHY(number, label)
#define BF2_EVENT_EMPTY(number, label)
#include "bf2_events.inc"
#undef BF2_EVENT
#undef BF2_EVENT_BRANCHY
#undef BF2_EVENT_EMPTY

constexpr EventInfo kEvents[] = {
#define BF2_EVENT(number, label, ...) \
  {number, label, false, kWidths##number, std::size(kWidths##number)},
#define BF2_EVENT_BRANCHY(number, label) {number, label, true, nullptr, 0},
#define BF2_EVENT_EMPTY(number, label) {number, label, false, nullptr, 0},
#include "bf2_events.inc"
#undef BF2_EVENT
#undef BF2_EVENT_BRANCHY
#undef BF2_EVENT_EMPTY
};

const EventInfo* find(std::uint32_t type) {
  for (const auto& info : kEvents) {
    if (info.type == type) return &info;
  }
  return nullptr;
}

std::optional<float> readFloat(BitReader& reader) {
  const auto bits = reader.readBits(32);
  if (!bits) return std::nullopt;
  float value = 0.0f;
  const std::uint32_t raw = *bits;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

std::optional<Vec3f> readVector(BitReader& reader) {
  const auto x = readFloat(reader);
  const auto y = readFloat(reader);
  const auto z = readFloat(reader);
  if (!x || !y || !z) return std::nullopt;
  return Vec3f{*x, *y, *z};
}

// Розбір подій, довжина яких залежить від вмісту.
bool readCreateObject(BitReader& reader, CreateObject& out) {
  const auto templateId = reader.readBits(32);
  const auto networkId = reader.readBits(16);
  const auto field2 = reader.readBits(2);
  const auto branch = reader.readBits(1);
  if (!templateId || !networkId || !field2 || !branch) return false;

  out.templateId = *templateId;
  out.networkId = static_cast<std::uint16_t>(*networkId);
  out.field2 = *field2;

  // Одиниця — коротка гілка: одне восьмибітне поле, і на цьому все.
  if (*branch == 1) {
    const auto value = reader.readBits(8);
    if (!value) return false;
    out.field8 = static_cast<std::uint8_t>(*value);
    return true;
  }

  const auto hasPosition = reader.readBits(1);
  if (!hasPosition) return false;
  if (*hasPosition == 1) {
    out.position = readVector(reader);
    if (!out.position) return false;
  }
  const auto hasRotation = reader.readBits(1);
  if (!hasRotation) return false;
  if (*hasRotation == 1) {
    out.rotation = readVector(reader);
    if (!out.rotation) return false;
  }
  return true;
}

// `DataBlockEvent`: заголовок блока або шматок даних.
bool skipDataBlock(BitReader& reader) {
  const auto isHeader = reader.readBits(1);
  if (!isHeader) return false;
  if (*isHeader == 1) return reader.skipBits(64);
  const auto length = reader.readBits(8);
  if (!length) return false;
  return reader.skipBits(*length * 8);
}

// `StringManagerEvent`: у пакетах після реєстрації приходить порожньою.
bool skipStringManager(BitReader& reader) {
  const auto flag = reader.readBits(1);
  if (!flag) return false;
  if (*flag == 0) return true;
  return reader.skipBits(6);
}

// `PostRemoteEvent`: категорія, номер, затримка і скільки байтів даних.
bool skipPostRemote(BitReader& reader) {
  if (!reader.skipBits(4 + 32 + 32)) return false;
  const auto length = reader.readBits(8);
  if (!length) return false;
  return reader.skipBits(*length * 8);
}

}  // namespace

std::string_view eventName(std::uint32_t type) {
  const auto* info = find(type);
  return info ? info->name : std::string_view{};
}

bool eventIsBranchy(std::uint32_t type) {
  const auto* info = find(type);
  return info && info->branchy;
}

bool skipEvent(BitReader& reader, std::uint32_t type) {
  const auto* info = find(type);
  if (!info) return false;

  if (!info->branchy) {
    for (std::size_t i = 0; i < info->count; ++i) {
      if (!reader.skipBits(info->widths[i])) return false;
    }
    return true;
  }

  switch (type) {
    case 0: return skipStringManager(reader);
    case 4: return skipDataBlock(reader);
    case 6: {
      CreateObject ignored;
      return readCreateObject(reader, ignored);
    }
    case 11: return skipPostRemote(reader);
    default: return false;  // цю ще не розібрали
  }
}

std::optional<Event> readEvent(BitReader& reader) {
  const auto type = reader.readBits(kEventTypeBits);
  if (!type) return std::nullopt;

  Event event;
  event.type = *type;

  if (*type == 6) {
    CreateObject object;
    if (!readCreateObject(reader, object)) return std::nullopt;
    event.object = object;
    return event;
  }
  if (*type == 5) {
    CreatePlayer player;
    const auto team = reader.readBits(3);
    const auto squad = reader.readBits(4);
    reader.readBits(1);
    const auto id = reader.readBits(8);
    reader.readBits(16);
    reader.readBits(16);
    reader.readBits(1);
    if (!team || !squad || !id) return std::nullopt;
    player.team = *team;
    player.squad = *squad;
    player.id = *id;

    // Ім'я — рівно 32 байти, і за нулем у полі лишається сміття, тож
    // читаємо байтами й спиняємось на першому нулі. `readString` тут не
    // годиться: він нулі викидає, а не обриває на них рядок.
    bool ended = false;
    for (int i = 0; i < 32; ++i) {
      const auto byte = reader.readByte();
      if (!byte) return std::nullopt;
      if (*byte == 0) ended = true;
      if (!ended) player.name.push_back(static_cast<char>(*byte));
    }
    event.player = player;
    return event;
  }

  if (!skipEvent(reader, *type)) return std::nullopt;
  return event;
}

std::vector<std::vector<std::byte>> loadCapture(const std::string& path) {
  std::vector<std::vector<std::byte>> packets;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return packets;

  while (true) {
    std::uint32_t length = 0;
    if (std::fread(&length, sizeof(length), 1, file) != 1) break;
    // Пакет BF2 не буває більшим за кілька кілобайтів: більше число
    // означає, що файл не той або обірваний.
    if (length == 0 || length > 4096) break;
    std::vector<std::byte> packet(length);
    if (std::fread(packet.data(), 1, length, file) != length) break;
    packets.push_back(std::move(packet));
  }
  std::fclose(file);
  return packets;
}

std::vector<Event> readEvents(std::span<const std::byte> packet) {
  std::vector<Event> events;
  BitReader reader(packet);

  const auto kind = reader.readBits(4);
  if (!kind || *kind != static_cast<std::uint32_t>(PacketKind::Data)) return events;
  // номер з'єднання, розширений заголовок і довжина корисної частини
  if (!reader.skipBits(8 + 6 + 6 + 32 + kStreamFramingBits)) return events;
  // потік дій гравця: свої дії ми поки не шлемо, тож там один нуль
  if (!reader.skipBits(1)) return events;

  const auto hasEvents = reader.readBits(1);
  if (!hasEvents || *hasEvents != 1) return events;
  const auto count = reader.readBits(8);
  if (!count) return events;
  reader.skipBits(5 + 1);

  for (std::uint32_t i = 0; i < *count; ++i) {
    auto event = readEvent(reader);
    if (!event) break;
    events.push_back(*event);
  }
  return events;
}

}  // namespace obf2::net::bf2
