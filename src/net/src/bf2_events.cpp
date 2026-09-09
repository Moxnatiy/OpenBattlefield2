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

// Parsing of events whose length depends on their contents.
bool readCreateObject(BitReader& reader, CreateObject& out) {
  const auto templateId = reader.readBits(32);
  const auto networkId = reader.readBits(16);
  const auto field2 = reader.readBits(2);
  const auto branch = reader.readBits(1);
  if (!templateId || !networkId || !field2 || !branch) return false;

  out.templateId = *templateId;
  out.networkId = static_cast<std::uint16_t>(*networkId);
  out.field2 = *field2;

  // One is the short branch: a single eight-bit field, and that is all.
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

// `DataBlockEvent`: a block's header or a chunk of data.
bool skipDataBlock(BitReader& reader) {
  const auto isHeader = reader.readBits(1);
  if (!isHeader) return false;
  if (*isHeader == 1) return reader.skipBits(64);
  const auto length = reader.readBits(8);
  if (!length) return false;
  return reader.skipBits(*length * 8);
}

// `StringManagerEvent`: in the packets after registration it arrives empty.
bool skipStringManager(BitReader& reader) {
  const auto flag = reader.readBits(1);
  if (!flag) return false;
  if (*flag == 0) return true;
  return reader.skipBits(6);
}

// `PostRemoteEvent`: the category, the number, the delay and how many data bytes.
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
    default: return false;  // this one is not parsed yet
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

    // The name is exactly 32 bytes, and rubbish is left in the field after the
    // zero, so we read bytes and stop at the first zero. `readString` will not do
    // here: it throws the zeroes away rather than ending the string at them.
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
  if (expected_ == 0) return std::nullopt;  // a chunk with no header
  data_.insert(data_.end(), piece.chunk.begin(), piece.chunk.end());
  if (data_.size() < expected_) return std::nullopt;

  auto done = std::make_pair(type_, std::move(data_));
  data_.clear();
  expected_ = 0;
  return done;
}

std::optional<MapInfo> parseMapInfo(std::span<const std::byte> block) {
  // The layout is taken from a live block: a u32, then three strings each
  // preceded by a u16 length — the level's name, the game mode and the size.
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
                               float worldZ, float worldSize, float* distance, int team) {
  std::uint8_t best = 0;
  float bestSquared = 0.0f;
  bool found = false;
  for (const CreateSpawnGroup& group : groups) {
    // Another team's groups will not do: the server spawns a player only in their
    // own (`ServerGameLogic::uPlayingSpawning` takes the player's group, which is
    // set by `NESelectSpawnGroup`). Zero in the group means neutral.
    if (team > 0 && group.team != 0 && static_cast<int>(group.team) != team) continue;
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
  // A signed number: one sign bit, then 31 bits of value.
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
    // A BF2 packet is never larger than a few kilobytes: a bigger number means
    // the file is the wrong one or truncated.
    if (length == 0 || length > 4096) break;
    std::vector<std::byte> packet(length);
    if (std::fread(packet.data(), 1, length, file) != length) break;
    packets.push_back(std::move(packet));
  }
  std::fclose(file);
  return packets;
}

namespace {

// The shared beginning: walk the packet's header and the player action stream.
// Returns false when this is not a data packet.
bool enterPayload(BitReader& reader) {
  const auto kind = reader.readBits(4);
  if (!kind || *kind != static_cast<std::uint32_t>(PacketKind::Data)) return false;
  if (!reader.skipBits(8 + 6 + 6 + 32 + kStreamFramingBits)) return false;
  return reader.skipBits(1);
}

}  // namespace

namespace {

// Shared by the header and the records: get to the ghost stream, having walked
// the player action stream and every event.
// Walk past the controlled-object state without parsing it.
//
// The walk was written out from `GhostManager::readControlObjectState` (0x445c30)
// with `tools/linuxded/bitfields.py --blocks`, branch by branch:
//
//   12                     a number at the start
//   1 + 31                 a counter (sign and magnitude; with the sign 0x445cbf,
//                          without it 0x445ed0 — both branches 31 bits)
//   32, 32, 32             the compression reference point -> setCompressionVector
//   16                     the controlled object's network id
//   1                      flag A; if 1 -> 16 more bits (0x445f93)
//   1                      flag B (0x445db3, after getObject)
//                          if 1 -> 1 bit (0x445f33), and if that is 1 -> 16 (0x445f66)
//   1                      flag C (0x445e52)
//   3                      both branches read 3 bits (0x445e80 / 0x445fde)
//
// Further on the function also has a read of 10 bits in a loop (0x44633a), but it
// is behind a condition we have not worked out. So the walk is **checked against
// data**: after it exactly `records` records have to read and the packet has to
// end. If it does not add up we say we cannot do it rather than pretending we
// read something.
bool skipControlObjectState(BitReader& reader) {
  if (!reader.skipBits(12)) return false;

  const auto sign = reader.readBits(1);
  if (!sign || !reader.skipBits(31)) return false;
  if (!reader.skipBits(32 * 3)) return false;  // the reference point
  if (!reader.skipBits(16)) return false;      // the network id

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
  // The controlled-object state lies before the records. We used to drop such
  // packets — and after a player spawns it travels in almost every one, so we
  // were throwing away nearly the whole stream with them.
  if (header->controlObjectState && !skipControlObjectState(reader)) return out;

  for (std::uint8_t i = 0; i < header->records; ++i) {
    const auto kind = reader.readBits(2);
    const auto networkId = reader.readBits(16);
    if (!kind || !networkId) break;

    GhostRecord record;
    record.kind = *kind;
    record.networkId = static_cast<std::uint16_t>(*networkId);
    if (*kind == 2) break;  // the engine treats this as a stream error
    if (*kind == 1) {
      const auto flag = reader.readBits(1);
      const auto length = reader.readBits(kGhostLengthBits);
      if (!flag || !length) break;
      record.baseline = *flag != 0;
      record.payloadBits = *length;

      // The main walk goes by the length **only** — that is how the engine passes
      // an object it does not know. The content is read by a separate reader: if
      // we get it wrong, the stream does not go astray because of it.
      const std::size_t payloadStart = reader.bitPosition();
      if (!reader.skipBits(*length)) break;

      // The content's layout depends on the object's networked class, and for a
      // soldier it differs: the mask is wider, the position is under a different
      // bit, the reference is zero, the precision coarse.
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

          // The fields that lie **before** the position. Skipping them is
          // mandatory: without it the read shifts and rubbish comes out instead of the position.
          bool ok = true;
          if (soldier) {
            if ((*mask & kSoldierStateRagdoll) != 0) ok = false;  // another branch
            if (ok && (*mask & kSoldierStateHasByte) != 0) ok = payload.skipBits(8 + 1);
            if (ok && (*mask & kSoldierStateHasPair) != 0) ok = payload.skipBits(3 + 3);
          }

          if (ok && (*mask & positionBit) != 0) {
            const auto at = payload.readCompressedVector(origin, precision);
            // The position is taken only when it fitted into the content:
            // otherwise we read the wrong thing and would pass rubbish off as a position.
            if (at && payload.bitPosition() <= payloadStart + *length) record.position = *at;
            else ok = false;
          }

          // Then come the velocity and the angles. We read them not for the
          // fields' own sake but because the yaw comes **after** the velocity:
          // without skipping the velocity the angle would be the wrong one.
          if (ok && soldier) {
            if ((*mask & kSoldierStateVelocity) != 0) {
              ok = payload.readCompressedVector(Vec3f{}, kSoldierVelocityPrecision).has_value();
            }
            if (ok && (*mask & kSoldierStateYaw) != 0) {
              const auto packed = payload.readBits(kSoldierAngleBits);
              if (packed && payload.bitPosition() <= payloadStart + *length) {
                record.yaw = soldierAngle(*packed, kSoldierYawRange);
              }
            }
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

  // The counter: a sign and 31 bits, like the numbers in the MapInfo block.
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
  // the connection id, the extended header and the payload's length
  if (!reader.skipBits(8 + 6 + 6 + 32 + kStreamFramingBits)) return events;
  // the player action stream: we do not send our own actions yet, so one zero there
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
