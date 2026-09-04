#include "obf2/net/bf2_events.h"

#include <cmath>
#include <cstdio>
#include <utility>
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
  if (*type == 4) {
    DataBlockPiece piece;
    const auto isHeader = reader.readBits(1);
    if (!isHeader) return std::nullopt;
    piece.header = *isHeader == 1;
    if (piece.header) {
      const auto blockType = reader.readBits(32);
      const auto size = reader.readBits(32);
      if (!blockType || !size) return std::nullopt;
      piece.blockType = *blockType;
      piece.size = *size;
    } else {
      const auto length = reader.readBits(8);
      if (!length) return std::nullopt;
      piece.chunk.resize(*length);
      if (!reader.readBytes(piece.chunk)) return std::nullopt;
    }
    event.block = std::move(piece);
    return event;
  }
  if (*type == 57) {
    CreateSpawnGroup group;
    const auto first = reader.readBits(8);
    const auto small = reader.readBits(4);
    const auto flag1 = reader.readBits(1);
    const auto flag2 = reader.readBits(1);
    const auto flag3 = reader.readBits(1);
    const auto worldX = reader.readBits(8);
    const auto worldZ = reader.readBits(8);
    const auto id = reader.readBits(16);
    if (!first || !small || !flag1 || !flag2 || !flag3 || !worldX || !worldZ || !id) {
      return std::nullopt;
    }
    group.id = static_cast<std::uint8_t>(*first);
    group.team = *small;
    group.flag1 = *flag1 != 0;
    group.flag2 = *flag2 != 0;
    group.flag3 = *flag3 != 0;
    group.worldX = static_cast<std::uint8_t>(*worldX);
    group.worldZ = static_cast<std::uint8_t>(*worldZ);
    group.networkId = static_cast<std::uint16_t>(*id);
    event.spawnGroup = group;
    return event;
  }
  if (*type == 9) {
    EnterVehicle enter;
    const auto player = reader.readBits(8);
    const auto object = reader.readBits(16);
    const auto flag = reader.readBits(1);
    if (!player || !object || !flag) return std::nullopt;
    enter.player = *player;
    enter.object = static_cast<std::uint16_t>(*object);
    enter.flag = *flag != 0;
    event.enter = enter;
    return event;
  }
  if (*type == 10) {
    const auto player = reader.readBits(8);
    const auto flag = reader.readBits(1);
    if (!player || !flag) return std::nullopt;
    event.exitPlayer = *player;
    return event;
  }
  if (*type == 11) {
    RemoteEvent remote;
    const auto category = reader.readBits(4);
    const auto number = reader.readBits(32);
    const auto delay = reader.readBits(32);
    const auto length = reader.readBits(8);
    if (!category || !number || !delay || !length) return std::nullopt;
    remote.category = *category;
    remote.number = *number;
    if (*length == 4) {
      std::uint32_t raw = 0;
      for (int i = 0; i < 4; ++i) {
        const auto byte = reader.readBits(8);
        if (!byte) return std::nullopt;
        raw |= *byte << (i * 8);
      }
      remote.value = static_cast<std::int32_t>(raw);
    } else if (!reader.skipBits(*length * 8)) {
      return std::nullopt;
    }
    event.remote = remote;
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

std::optional<std::pair<std::uint32_t, std::vector<std::byte>>> DataBlockAssembler::feed(
    const DataBlockPiece& piece) {
  if (piece.header) {
    type_ = piece.blockType;
    expected_ = piece.size;
    data_.clear();
    data_.reserve(expected_);
    return std::nullopt;
  }
  if (expected_ == 0) return std::nullopt;  // шматок без заголовка
  data_.insert(data_.end(), piece.chunk.begin(), piece.chunk.end());
  if (data_.size() < expected_) return std::nullopt;

  auto done = std::make_pair(type_, std::move(data_));
  data_.clear();
  expected_ = 0;
  return done;
}

std::optional<MapInfo> parseMapInfo(std::span<const std::byte> block) {
  // Розкладка знята з живого блока: u32, далі три рядки з довжиною u16
  // попереду — назва рівня, режим гри і розмір.
  BitReader reader(block);
  const auto first = reader.readBits(32);
  if (!first) return std::nullopt;

  const auto text = [&reader]() -> std::optional<std::string> {
    const auto length = reader.readBits(16);
    if (!length || *length > 256) return std::nullopt;
    std::string out;
    for (std::uint32_t i = 0; i < *length; ++i) {
      const auto byte = reader.readByte();
      if (!byte) return std::nullopt;
      out.push_back(static_cast<char>(*byte));
    }
    return out;
  };

  MapInfo info;
  const auto level = text();
  const auto mode = text();
  const auto size = reader.readBits(16);
  if (!level || !mode || !size) return std::nullopt;
  info.levelName = *level;
  info.gameMode = *mode;
  info.size = static_cast<int>(*size);
  info.first = *first;
  return info;
}

std::uint8_t nearestSpawnGroup(const std::vector<CreateSpawnGroup>& groups, float worldX,
                               float worldZ, float worldSize, float* distance) {
  std::uint8_t best = 0;
  float bestSquared = 0.0f;
  bool found = false;
  for (const CreateSpawnGroup& group : groups) {
    const float gx = spawnGroupWorldPos(group.worldX, worldSize);
    const float gz = spawnGroupWorldPos(group.worldZ, worldSize);
    const float dx = gx - worldX;
    const float dz = gz - worldZ;
    const float squared = dx * dx + dz * dz;
    if (found && squared >= bestSquared) continue;
    found = true;
    bestSquared = squared;
    best = group.id;
  }
  if (distance != nullptr) *distance = found ? std::sqrt(bestSquared) : 0.0f;
  return best;
}

std::optional<ServerMapInfo> parseMapInfoNetBuffer(std::span<const std::byte> block) {
  BitReader reader(block);

  const auto text = [&reader]() -> std::optional<std::string> {
    const auto length = reader.readBits(16);
    if (!length || *length > 256) return std::nullopt;
    std::string out;
    for (std::uint32_t i = 0; i < *length; ++i) {
      const auto byte = reader.readByte();
      if (!byte) return std::nullopt;
      out.push_back(static_cast<char>(*byte));
    }
    return out;
  };
  // Число зі знаком: один біт знака, далі 31 біт значення.
  const auto number = [&reader]() -> std::optional<int> {
    const auto sign = reader.readBits(1);
    const auto value = reader.readBits(31);
    if (!sign || !value) return std::nullopt;
    const int out = static_cast<int>(*value);
    return *sign ? -out : out;
  };

  ServerMapInfo info;
  const auto mode = text();
  const auto path = text();
  const auto name = text();
  if (!mode || !path || !name) return std::nullopt;
  info.gameMode = *mode;
  info.levelPath = *path;
  info.levelName = *name;

  const auto players = number();
  if (!players) return std::nullopt;
  info.maxPlayers = *players;

  const auto commander = reader.readBits(1);
  if (!commander) return std::nullopt;
  info.commanderEnabled = *commander != 0;

  const auto ordinal = number();
  if (!ordinal) return std::nullopt;
  info.challengeOrdinal = *ordinal;
  return info;
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

namespace {

// Спільний початок: пройти заголовок пакета й потік дій гравця.
// Повертає false, якщо це не пакет даних.
bool enterPayload(BitReader& reader) {
  const auto kind = reader.readBits(4);
  if (!kind || *kind != static_cast<std::uint32_t>(PacketKind::Data)) return false;
  if (!reader.skipBits(8 + 6 + 6 + 32 + kStreamFramingBits)) return false;
  return reader.skipBits(1);
}

}  // namespace

namespace {

// Спільне для заголовка й записів: дійти до потоку привидів, пройшовши
// потік дій гравця й усі події.
// Пройти стан керованого об'єкта, не розбираючи його.
//
// Прохід виписано з `GhostManager::readControlObjectState` (0x445c30)
// знаряддям `tools/linuxded/bitfields.py --blocks`, гілка за гілкою:
//
//   12                     число на початку
//   1 + 31                 лічильник (знак і величина; при знаку 0x445cbf,
//                          без знака 0x445ed0 — обидві гілки по 31 біт)
//   32, 32, 32             опорна точка стиснення -> setCompressionVector
//   16                     мережевий номер керованого об'єкта
//   1                      прапорець A; якщо 1 -> ще 16 біт (0x445f93)
//   1                      прапорець B (0x445db3, після getObject)
//                          якщо 1 -> 1 біт (0x445f33), і якщо той 1 -> 16 (0x445f66)
//   1                      прапорець C (0x445e52)
//   3                      обидві гілки читають по 3 біти (0x445e80 / 0x445fde)
//
// Далі у функції є ще читання по 10 бітів у циклі (0x44633a), але воно
// під умовою, якої ми ще не з'ясували. Тому прохід **перевіряється
// даними**: після нього мають прочитатися рівно `records` записів, і
// пакет має закінчитися. Не зійшлося — кажемо, що не вміємо, і не
// вдаємо, ніби прочитали.
bool skipControlObjectState(BitReader& reader) {
  if (!reader.skipBits(12)) return false;

  const auto sign = reader.readBits(1);
  if (!sign || !reader.skipBits(31)) return false;
  if (!reader.skipBits(32 * 3)) return false;  // опорна точка
  if (!reader.skipBits(16)) return false;      // мережевий номер

  const auto flagA = reader.readBits(1);
  if (!flagA) return false;
  if (*flagA == 1 && !reader.skipBits(16)) return false;

  const auto flagB = reader.readBits(1);
  if (!flagB) return false;
  if (*flagB == 1) {
    const auto more = reader.readBits(1);
    if (!more) return false;
    if (*more == 1 && !reader.skipBits(16)) return false;
  }

  const auto flagC = reader.readBits(1);
  if (!flagC || !reader.skipBits(3)) return false;
  return true;
}

std::optional<GhostHeader> enterGhosts(BitReader& reader) {
  if (!enterPayload(reader)) return std::nullopt;

  const auto hasEvents = reader.readBits(1);
  if (!hasEvents) return std::nullopt;
  if (*hasEvents == 1) {
    const auto count = reader.readBits(8);
    if (!count) return std::nullopt;
    reader.skipBits(5 + 1);
    for (std::uint32_t i = 0; i < *count; ++i) {
      const auto type = reader.readBits(kEventTypeBits);
      if (!type || !skipEvent(reader, *type)) return std::nullopt;
    }
  }

  const auto hasGhosts = reader.readBits(1);
  if (!hasGhosts || *hasGhosts != 1) return std::nullopt;

  const auto time = reader.readBits(32);
  const auto records = reader.readBits(8);
  const auto control = reader.readBits(1);
  if (!time || !records || !control) return std::nullopt;

  GhostHeader out;
  out.time = *time;
  out.records = static_cast<std::uint8_t>(*records);
  out.controlObjectState = *control != 0;
  return out;
}

}  // namespace

std::vector<GhostRecord> readGhostRecords(
    std::span<const std::byte> packet, const std::function<Vec3f(std::uint16_t)>& referenceFor,
    const std::function<bool(std::uint16_t)>& isSoldier) {
  std::vector<GhostRecord> out;
  BitReader reader(packet);
  const auto header = enterGhosts(reader);
  if (!header) return out;
  // Стан керованого об'єкта лежить перед записами. Раніше ми такі пакети
  // просто кидали — а після появи гравця він їде майже в кожному, і разом
  // із ними ми викидали майже весь потік.
  if (header->controlObjectState && !skipControlObjectState(reader)) return out;

  for (std::uint8_t i = 0; i < header->records; ++i) {
    const auto kind = reader.readBits(2);
    const auto networkId = reader.readBits(16);
    if (!kind || !networkId) break;

    GhostRecord record;
    record.kind = *kind;
    record.networkId = static_cast<std::uint16_t>(*networkId);
    if (*kind == 2) break;  // рушій вважає це помилкою потоку
    if (*kind == 1) {
      const auto flag = reader.readBits(1);
      const auto length = reader.readBits(kGhostLengthBits);
      if (!flag || !length) break;
      record.baseline = *flag != 0;
      record.payloadBits = *length;

      // Головний прохід іде **тільки** за довжиною — так рушій проходить
      // повз об'єкт, якого не знає. Вміст читаємо окремим читачем: якщо
      // ми в ньому помилимося, потік від цього не зіб'ється.
      const std::size_t payloadStart = reader.bitPosition();
      if (!reader.skipBits(*length)) break;

      // Розкладка вмісту залежить від мережевого класу об'єкта, і для
      // солдата вона інша: маска ширша, місце вмикає інший біт, опора
      // нульова, точність груба.
      const bool soldier = isSoldier && isSoldier(record.networkId);
      const unsigned maskBits = soldier ? kSoldierStateMaskBits : kObjectStateMaskBits;
      const std::uint32_t positionBit = soldier ? kSoldierStatePosition : kObjectStatePosition;
      const float precision = soldier ? kSoldierPositionPrecision : kObjectPositionPrecision;
      const Vec3f origin = referenceFor ? referenceFor(record.networkId) : Vec3f{};

      BitReader payload(packet);
      if (payload.skipBits(payloadStart)) {
        const auto mask = payload.readBits(maskBits);
        if (mask) {
          record.stateMask = *mask;
          if ((*mask & positionBit) != 0) {
            const auto at = payload.readCompressedVector(origin, precision);
            // Місце береться лише тоді, коли воно вмістилося у вміст:
            // інакше ми прочитали не те й видали б за позицію сміття.
            if (at && payload.bitPosition() <= payloadStart + *length) record.position = *at;
          }
        }
      }
    }
    out.push_back(record);
  }
  return out;
}

std::optional<bool> ghostFlag(std::span<const std::byte> packet) {
  BitReader reader(packet);
  if (!enterPayload(reader)) return std::nullopt;
  const auto hasEvents = reader.readBits(1);
  if (!hasEvents) return std::nullopt;
  if (*hasEvents == 1) {
    const auto count = reader.readBits(8);
    if (!count) return std::nullopt;
    reader.skipBits(5 + 1);
    for (std::uint32_t i = 0; i < *count; ++i) {
      const auto type = reader.readBits(kEventTypeBits);
      if (!type || !skipEvent(reader, *type)) return std::nullopt;
    }
  }
  const auto hasGhosts = reader.readBits(1);
  if (!hasGhosts) return std::nullopt;
  return *hasGhosts != 0;
}

std::optional<ControlObjectState> readControlObjectState(std::span<const std::byte> packet) {
  BitReader reader(packet);
  const auto header = enterGhosts(reader);
  if (!header || !header->controlObjectState) return std::nullopt;

  ControlObjectState out;
  const auto first = reader.readBits(12);
  if (!first) return std::nullopt;
  out.first = *first;

  // Лічильник: знак і 31 біт, як і числа в блоці MapInfo.
  const auto sign = reader.readBits(1);
  const auto value = reader.readBits(31);
  if (!sign || !value) return std::nullopt;
  out.counter = static_cast<std::int32_t>(*value);
  if (*sign == 1) out.counter = -out.counter;

  const auto position = readVector(reader);
  if (!position) return std::nullopt;
  out.compressionReference = *position;

  const auto networkId = reader.readBits(16);
  if (!networkId) return std::nullopt;
  out.networkId = static_cast<std::uint16_t>(*networkId);
  return out;
}

std::optional<GhostHeader> readGhostHeader(std::span<const std::byte> packet) {
  BitReader reader(packet);
  return enterGhosts(reader);
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
