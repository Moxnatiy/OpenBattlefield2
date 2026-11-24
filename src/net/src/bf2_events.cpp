#include <algorithm>
#include <bit>
#include <cmath>
#include "obf2/net/bf2_events.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// `StringManagerEvent` (type 0), `deSerialize` Linux 0x431cb0: a flag (1), a
// 6-bit length and that many bytes of text, a bit saying a value follows and the
// value (8; -1 without it), then two flags (1, 1). We had read the first flag
// alone — on an empty one that left 9 bits behind and threw the packet off.
bool skipStringManager(BitReader& reader) {
  if (!reader.skipBits(1)) return false;
  const auto length = reader.readBits(6);
  if (!length || !reader.skipBits(*length * 8)) return false;
  const auto hasValue = reader.readBits(1);
  if (!hasValue) return false;
  if (*hasValue == 1 && !reader.skipBits(8)) return false;
  return reader.skipBits(2);
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
    case 21:
      // `KilledByEvent::deSerialize` (Linux 0x42a920): 8, 8, 1, 16, 32, 32, 8, then a
      // compressed position at 1/1000. The vector's length is its own; the table
      // used to stop before it, and every kill threw the rest of its packet off.
      return reader.skipBits(8 + 8 + 1 + 16 + 32 + 32 + 8) &&
             reader.readCompressedVector(Vec3f{}, 0.001f).has_value();
    case 64:
      // `GrapplingHookUpdateEvent::deSerialize` (Linux 0x4280b0): 16, 8, two
      // compressed vectors at 1/1000, 1.
      return reader.skipBits(16 + 8) && reader.readCompressedVector(Vec3f{}, 0.001f).has_value() &&
             reader.readCompressedVector(Vec3f{}, 0.001f).has_value() && reader.skipBits(1);
    // The rest by their `deSerialize` as OpenBF2 reconstructs them
    // (Code/BF2/Game/GameEvents/<Name>.cpp). `bitsToHold(n)` there is the
    // fewest bits whose all-ones reaches n: 2 -> 2, 4 -> 3, 9 -> 4, 32 -> 6.
    case 1: {
      // `ChallengeEvent`: the challenge, 80 bits, then the mod directory with an
      // 8-bit length (the same layout `readPacket` takes it by, bf2_protocol.cpp).
      if (!reader.skipBits(80)) return false;
      const auto length = reader.readBits(8);
      return length && reader.skipBits(*length * 8);
    }
    case 15: {
      // `StringBlockEvent`: a flag; a chunk (8-bit length, the bytes) or a size.
      const auto isNew = reader.readBits(1);
      if (!isNew) return false;
      if (*isNew == 1) return reader.skipBits(8);
      const auto size = reader.readBits(8);
      return size && reader.skipBits(*size * 8);
    }
    case 26:
      // `RankEvent`: type bitsToHold(2), rank bitsToHold(32), 32, player 8.
      return reader.skipBits(2 + 6 + 32 + 8);
    case 19:
      // `CommanderEvent`: the id in bitsToHold(9), team 8, sign 1, magnitude 15.
      return reader.skipBits(4 + 8 + 1 + 15);
    case 20:
      // `RadioMessageEvent`: message 8, sender 8, team 2, 8, channel 5, type 3.
      return reader.skipBits(8 + 8 + 2 + 8 + 5 + 3);
    case 22: {
      // `ChangeSquadNameEvent`: squad 8, team 8, player 8, then an 8-bit length.
      if (!reader.skipBits(24)) return false;
      const auto length = reader.readBits(8);
      return length && reader.skipBits(*length * 8);
    }
    case 24: {
      // `IssueSquadOrderEvent`: squad 8, team 8, order 8; a target flag — a
      // 16-bit network id, or nine 32-bit floats; then the type 8 and a flag 1.
      if (!reader.skipBits(24)) return false;
      const auto hasTarget = reader.readBits(1);
      if (!hasTarget) return false;
      return reader.skipBits(*hasTarget == 1 ? 16 : 9 * 32) && reader.skipBits(8 + 1);
    }
    case 30: {
      // `ArtilleryEvent`: type 2, team 32; a fire request (1) adds a 2-D target,
      // 2 × 32; a shot (2) a 16-bit weapon and a 32-bit tick.
      const auto kind = reader.readBits(2);
      if (!kind || !reader.skipBits(32)) return false;
      if (*kind == 1) return reader.skipBits(64);
      if (*kind == 2) return reader.skipBits(16 + 32);
      return true;
    }
    case 32: {
      // `StickyProjectileEvent`: projectile 16, target 16 (when non-zero: a
      // count 8 and a child 8), player 8, then four rows of three 32-bit floats.
      if (!reader.skipBits(16)) return false;
      const auto target = reader.readBits(16);
      if (!target) return false;
      if (*target != 0 && !reader.skipBits(16)) return false;
      return reader.skipBits(8 + 12 * 32);
    }
    case 34:
      // `AmbientEffectAreaEvent`: area 16, team 8, object 16, a position 3 × 32.
      return reader.skipBits(16 + 8 + 16 + 3 * 32);
    case 49: {
      // `PythonCommandEvent`: command 32, player 8, a 4-bit count of 32-bit args.
      if (!reader.skipBits(32 + 8)) return false;
      const auto count = reader.readBits(4);
      return count && reader.skipBits(*count * 32);
    }
    case 50:
      // `RequestEvent`: squad 8, team 8, the request bitsToHold(4).
      return reader.skipBits(8 + 8 + 3);
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
  if (*type == 32) {
    const auto projectile = reader.readBits(16);
    const auto target = reader.readBits(16);
    if (!projectile || !target || (*target != 0 && !reader.skipBits(16))) return std::nullopt;
    const auto player = reader.readBits(8);
    if (!player) return std::nullopt;
    auto transform = Mat4::identity();
    for (const int row : {8, 0, 4, 12}) {
      const auto value = readVector(reader);
      if (!value) return std::nullopt;
      transform.m[row] = value->x;
      transform.m[row + 1] = value->y;
      transform.m[row + 2] = value->z;
    }
    event.stickyProjectile = StickyProjectile{static_cast<std::uint16_t>(*projectile),
        static_cast<std::uint16_t>(*target), static_cast<std::uint8_t>(*player), transform};
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
  if (*type == 19) {
    const auto action = reader.readBits(4), team = reader.readBits(8);
    const auto negative = reader.readBits(1), magnitude = reader.readBits(15);
    if (!action || !team || !negative || !magnitude) return std::nullopt;
    if (*action == 3 && *team >= 1 && *team <= 2) {
      const int player = static_cast<int>(*magnitude);
      event.commanderAssignment = Event::CommanderAssignment{*team, *negative ? -player : player};
    }
    return event;
  }
  if (*type == 20) {
    const auto message = reader.readBits(8), sender = reader.readBits(8), team = reader.readBits(2);
    const auto extra = reader.readBits(8), channel = reader.readBits(5), kind = reader.readBits(3);
    if (!message || !sender || !team || !extra || !channel || !kind) return std::nullopt;
    event.radioCall = RadioCall{*message, *sender, *team, *extra, *channel, *kind};
    return event;
  }
  if (*type == 28) {
    const auto team = reader.readBits(8), player = reader.readBits(8), squad = reader.readBits(8);
    if (!team || !player || !squad) return std::nullopt;
    event.squadLeader = SquadLeaderChange{*team, *player, *squad};
    return event;
  }
  if (*type == 24) {
    const auto squad = reader.readBits(8), team = reader.readBits(8), id = reader.readBits(8);
    const auto hasTarget = reader.readBits(1);
    if (!squad || !team || !id || !hasTarget) return std::nullopt;
    SquadOrder order{*squad, *team, *id};
    if (*hasTarget) {
      const auto target = reader.readBits(16);
      if (!target) return std::nullopt;
      order.target = static_cast<std::uint16_t>(*target);
    } else {
      for (auto component : {&Vec3f::x, &Vec3f::y, &Vec3f::z}) {
        for (auto& point : order.points) {
          const auto value = reader.readBits(32);
          if (!value) return std::nullopt;
          point.*component = std::bit_cast<float>(*value);
        }
      }
    }
    const auto route = reader.readBits(8), accepted = reader.readBits(1);
    if (!route || !accepted) return std::nullopt;
    if (*route <= static_cast<unsigned>(OrderRoute::Request)) {
      order.route = static_cast<OrderRoute>(*route);
      order.accepted = *accepted != 0;
      event.squadOrder = order;
    }
    return event;
  }
  if (*type == 27) {
    const auto squad = reader.readBits(8), team = reader.readBits(8), accepted = reader.readBits(1);
    const auto route = reader.readBits(8), id = reader.readBits(8);
    if (!squad || !team || !accepted || !route || !id) return std::nullopt;
    if (*route <= static_cast<unsigned>(OrderRoute::Request)) {
      event.orderResponse = OrderResponse{*squad, *team, *accepted != 0,
                                          static_cast<OrderRoute>(*route), *id};
    }
    return event;
  }
  if (*type == 22) {
    Event::SquadName name;
    const auto squad = reader.readBits(8);
    const auto team = reader.readBits(8);
    const auto player = reader.readBits(8);
    const auto length = reader.readBits(8);
    if (!squad || !team || !player || !length) return std::nullopt;
    for (std::uint32_t i = 0; i < *length; ++i) {
      const auto byte = reader.readBits(8);
      if (!byte) return std::nullopt;
      name.name.push_back(static_cast<char>(*byte));
    }
    name.squad = *squad;
    name.team = *team;
    name.player = *player;
    event.squadName = std::move(name);
    return event;
  }
  if (*type == 23) {
    const auto squad = reader.readBits(8);
    const auto team = reader.readBits(8);
    const auto flag = reader.readBits(1);
    if (!squad || !team || !flag) return std::nullopt;
    event.squadPrivate = Event::SquadPrivate{*squad, *team, *flag == 1};
    return event;
  }
  if (*type == 56) {
    // `BeginRoundEvent` (Linux 0x41f3d0): the state, then the next change's tick.
    const auto state = reader.readBits(32);
    const auto tick = reader.readBits(32);
    if (!state || !tick) return std::nullopt;
    event.beginRound = Event::BeginRound{*state, *tick};
    return event;
  }
  if (*type == 15) {
    Event::StringBlock block;
    const auto isNew = reader.readBits(1);
    if (!isNew) return std::nullopt;
    block.isNew = *isNew == 1;
    const auto size = reader.readBits(8);
    if (!size) return std::nullopt;
    if (block.isNew) {
      block.size = *size;
    } else {
      // At most 63 bytes are kept (`MAX_CHUNK_SIZE`); a longer chunk's rest is
      // read past, as the original does.
      for (std::uint32_t i = 0; i < *size; ++i) {
        const auto byte = reader.readBits(8);
        if (!byte) return std::nullopt;
        if (i < 63) block.chunk.push_back(static_cast<std::uint8_t>(*byte));
      }
    }
    event.stringBlock = std::move(block);
    return event;
  }
  if (*type == 21) {
    Event::KilledBy kill;
    const auto killer = reader.readBits(8);
    const auto victim = reader.readBits(8);
    const auto alive = reader.readBits(1);
    const auto field04 = reader.readBits(16);
    const auto weapon = reader.readBits(32);
    const auto vehicle = reader.readBits(32);
    const auto bone = reader.readBits(8);
    if (!killer || !victim || !alive || !field04 || !vehicle || !weapon || !bone) return std::nullopt;
    const auto direction = reader.readCompressedVector(Vec3f{}, 0.001f);
    if (!direction) return std::nullopt;
    kill.killer = *killer;
    kill.victim = *victim;
    kill.killerAlive = *alive == 1;
    kill.material = *field04;
    kill.vehicleTemplate = *vehicle;
    kill.weaponTemplate = *weapon;
    kill.lastHitBone = *bone;
    kill.lastHitDirection = *direction;
    event.killedBy = kill;
    return event;
  }
  if (*type == 26) {
    const auto kind = reader.readBits(2), rank = reader.readBits(6);
    if (!kind || !rank || !reader.skipBits(32)) return std::nullopt;
    const auto player = reader.readBits(8);
    if (!player) return std::nullopt;
    event.rank = Event::Rank{*kind, *player, static_cast<int>(*rank) - 1};
    return event;
  }
  if (*type == 39) {
    std::array<std::uint32_t, 9> fields{};
    for (auto& field : fields) {
      const auto value = reader.readBits(8);
      if (!value) return std::nullopt;
      field = *value;
    }
    const auto time = reader.readBits(32);
    if (!time) return std::nullopt;
    event.vote = Vote{fields[3], fields[1], fields[2], fields[4], fields[5],
                      fields[6], fields[7], fields[8], *time};
    return event;
  }
  if (*type == 41) {
    // MedalEvent::serialize (Linux 0x42b270): id 32, player 8, level 4.
    const auto id = reader.readBits(32), player = reader.readBits(8), level = reader.readBits(4);
    if (!id || !player || !level) return std::nullopt;
    event.medal = Event::Medal{*id, *player, *level};
    return event;
  }
  if (*type == 42) {
    const auto kind = reader.readBits(2), player = reader.readBits(8);
    if (!kind || !player) return std::nullopt;
    auto& unlocks = event.unlocks.emplace();
    unlocks.kind = *kind;
    unlocks.player = *player;
    for (auto& team : unlocks.levels) {
      for (auto& level : team) {
        const auto value = reader.readBits(4);
        if (!value) return std::nullopt;
        level = *value;
      }
    }
    return event;
  }
  if (*type == 54) {
    event.voiceSession = reader.readBits(16);
    if (!event.voiceSession) return std::nullopt;
    return event;
  }
  if (*type == 48) {
    const auto rounds = reader.readBits(32);
    const auto winner = reader.readBits(32);
    const auto victory = reader.readBits(32);
    if (!rounds || !winner || !victory) return std::nullopt;
    event.endOfRound = Event::EndOfRound{static_cast<std::int32_t>(*rounds),
                                         static_cast<std::int32_t>(*winner),
                                         static_cast<std::int32_t>(*victory)};
    return event;
  }
  if (*type == 7) {
    const auto id = reader.readBits(16);
    if (!id) return std::nullopt;
    event.destroyObject = static_cast<std::uint16_t>(*id);
    return event;
  }
  if (*type == 8) {
    const auto id = reader.readBits(8);
    if (!id) return std::nullopt;
    event.destroyPlayer = static_cast<std::uint8_t>(*id);
    return event;
  }
  if (*type == 58) {
    // `RemoveSpawnGroupEvent`: the group's number, then its network id.
    const auto id = reader.readBits(8);
    if (!id || !reader.skipBits(16)) return std::nullopt;
    event.removeSpawnGroup = static_cast<std::uint8_t>(*id);
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
    group.active = *flag1 != 0;
    group.aiOnly = *flag2 != 0;
    group.selectable = *flag3 != 0;
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
  if (*type == 31) {
    CreateKit kit;
    const auto templateId = reader.readBits(32);
    const auto id = reader.readBits(16);
    const auto position = readVector(reader);
    const auto value24 = reader.readBits(4);
    const auto value28 = reader.readBits(4);
    if (!templateId || !id || !position || !value24 || !value28) return std::nullopt;
    kit.templateId = *templateId;
    kit.networkId = static_cast<std::uint16_t>(*id);
    kit.position = *position;
    kit.value24 = *value24;
    kit.value28 = static_cast<std::int32_t>(*value28) - 1;  // 0x422c18: `decl`
    event.kit = kit;
    return event;
  }
  if (*type == 13) {
    const auto player = reader.readBits(8);
    const auto item = reader.readBits(16);
    const auto dropper = reader.readBits(16);
    const auto position = readVector(reader);
    if (!player || !item || !dropper || !position) return std::nullopt;
    event.drop = HandleDrop{*player, static_cast<std::uint16_t>(*item),
                            static_cast<std::uint16_t>(*dropper), *position};
    return event;
  }
  if (*type == 14) {
    HandlePickup pickup;
    const auto player = reader.readBits(8);
    const auto first = reader.readBits(16);
    const auto second = reader.readBits(16);
    if (!player || !first || !second) return std::nullopt;
    pickup.player = *player;
    pickup.first = static_cast<std::uint16_t>(*first);
    pickup.second = static_cast<std::uint16_t>(*second);
    event.pickup = pickup;
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
    remote.data.reserve(*length);
    for (std::uint32_t i = 0; i < *length; ++i) {
      const auto byte = reader.readBits(8);
      if (!byte) return std::nullopt;
      remote.data.push_back(static_cast<std::uint8_t>(*byte));
    }
    // The events that carry a number carry it in the first four bytes, whatever
    // follows (NEPlayerDead has eight, see RemoteEvent).
    if (remote.data.size() >= 4) {
      std::uint32_t raw = 0;
      for (int i = 0; i < 4; ++i) raw |= static_cast<std::uint32_t>(remote.data[i]) << (i * 8);
      remote.value = static_cast<std::int32_t>(raw);
    }
    event.remote = remote;
    return event;
  }
  if (*type == 5) {
    CreatePlayer player;
    const auto team = reader.readBits(3);
    const auto spawnGroup = reader.readBits(4);
    reader.readBits(1);
    const auto id = reader.readBits(8);
    const auto networkId = reader.readBits(16);
    reader.readBits(16);
    const auto aiPlayer = reader.readBits(1);
    if (!team || !spawnGroup || !id || !networkId || !aiPlayer) return std::nullopt;
    player.team = *team;
    player.spawnGroup = *spawnGroup;
    player.id = *id;
    player.networkId = static_cast<std::uint16_t>(*networkId);
    player.aiPlayer = *aiPlayer != 0;

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

std::optional<MapList> parseMapList(std::span<const std::byte> block) {
  // OpenBF2 MapList.cpp `updateFromDataBlock`: host-order (little-endian) u16s and
  // u16-length strings.
  std::size_t at = 0;
  const auto u16 = [&]() -> std::optional<int> {
    if (at + 2 > block.size()) return std::nullopt;
    const int value = std::to_integer<int>(block[at]) | (std::to_integer<int>(block[at + 1]) << 8);
    at += 2;
    return value;
  };
  const auto text = [&]() -> std::optional<std::string> {
    const auto length = u16();
    if (!length || at + static_cast<std::size_t>(*length) > block.size()) return std::nullopt;
    std::string out(reinterpret_cast<const char*>(block.data() + at), static_cast<std::size_t>(*length));
    at += static_cast<std::size_t>(*length);
    return out;
  };
  const auto count = u16();
  const auto current = u16();
  if (!count || !current) return std::nullopt;
  MapList list;
  list.current = *current;
  for (int i = 0; i < *count; ++i) {
    const auto name = text();
    const auto mode = text();
    const auto size = u16();
    if (!name || !mode || !size) return std::nullopt;
    list.entries.push_back(MapListEntry{*name, *mode, *size});
  }
  return list;
}

std::optional<SpawnGroupState> readSpawnGroupState(BitReader& reader) {
  // Linux `SpawnGroup::setNetUpdate` 0x4b9ab0, in its own order.
  SpawnGroupState out;
  const auto mask = reader.readBits(kSpawnGroupMaskBits);
  if (!mask) return std::nullopt;
  out.mask = *mask;
  if (out.mask & 0x10) {
    const auto v = reader.readBits(1);
    if (!v) return std::nullopt;
    out.selectable = *v != 0;
  }
  if (out.mask & 0x2) {
    const auto v = reader.readBits(4);
    if (!v) return std::nullopt;
    out.team = *v;
  }
  if (out.mask & 0x4) {
    const auto v = reader.readBits(1);
    if (!v) return std::nullopt;
    out.active = *v != 0;
  }
  if (out.mask & 0x8) {
    const auto v = reader.readBits(1);
    if (!v) return std::nullopt;
    out.aiOnly = *v != 0;
  }
  if (out.mask & 0x1) {
    const auto x = reader.readBits(8);
    const auto z = reader.readBits(8);
    if (!x || !z) return std::nullopt;
    out.worldX = static_cast<std::uint8_t>(*x);
    out.worldZ = static_cast<std::uint8_t>(*z);
  }
  if (out.mask & 0x20) {
    const auto v = reader.readBits(8);
    if (!v) return std::nullopt;
    out.squad = static_cast<int>(*v) - 1;
  }
  return out;
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
    // A flag's group is one the level made. Numbers from kDynamicSpawnGroupFirst up
    // are the server's own, created at run time (mobile spawns in vehicles and the
    // like): on the co-op server a LAV's group 197 stood nearer the gas station's
    // flag than the flag's group 3, was chosen, and the player did not appear.
    if (group.id >= kDynamicSpawnGroupFirst) continue;
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

std::optional<ServerInfoHead> parseServerInfo(std::span<const std::byte> block) {
  BitReader reader(block);
  const auto text = [&reader]() -> std::optional<std::string> {
    const auto length = reader.readBits(16);
    if (!length || *length > 1024) return std::nullopt;
    std::string out;
    for (std::uint32_t i = 0; i < *length; ++i) {
      const auto byte = reader.readByte();
      if (!byte) return std::nullopt;
      out.push_back(static_cast<char>(*byte));
    }
    return out;
  };
  ServerInfoHead head;
  const auto name = text();
  if (!name) return std::nullopt;
  head.gameName = *name;
  const auto count = reader.readBits(16);
  if (!count || *count > 64) return head;
  for (std::uint32_t i = 0; i < *count; ++i) {
    const auto line = text();
    if (!line) return head;
    head.welcome.push_back(*line);
  }
  const auto bit = [&reader]() { return reader.readBits(1); };
  const auto requestResponse = bit();
  const auto voip = bit();
  if (!requestResponse || !voip) return head;
  head.voipEnabled = *voip == 1;
  // allowNoseCam, hitIndicator, tkPunishDefault, allowFreeCam, allowExternalViews;
  // the voice server's address (32) and ports (16, 16); the quality (32); the
  // GameSpy port (16); three signed radio numbers (3 × 32).
  if (!reader.skipBits(5 + 32 + 16 + 16 + 32 + 16 + 3 * 32)) return head;
  const auto content = bit();
  const auto voting = bit();
  if (!content || !voting) return head;
  head.contentChecking = *content == 1;
  head.votingEnabled = *voting == 1;
  return head;
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

// The controlled-object state carries its own length. The writer,
// `GhostManager::writeControlObjectState` (`BF2.exe`, 0x5b9230 — the path
// `Code\BF2\Game\Common\GhostManager.cpp` in its error calls), reserves twelve
// bits first, writes the whole state, and goes back to store how many bits it
// wrote after them; past 0xfff it complains "control object size is to big!!!"
// (line 0x480); the reader, 0x5b9860, checks the same number at its end ("Size
// differs bits read … indicated size"). So the first twelve bits are the
// state's size, and the end is found without knowing a single branch inside.
// On `tests/data/bf2-spawned.bin` that reaches the records in 200 packets of
// 200, where the old field-by-field walk managed 199.
bool skipControlObjectState(BitReader& reader) {
  const auto size = reader.readBits(12);
  if (!size) return false;
  return reader.skipBits(*size);
}

// Shared by the header and the records: get to the ghost stream, having walked
// the player action stream and every event.
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
    std::span<const std::byte> packet, const Vec3f& reference,
    const std::function<GhostClass(std::uint16_t)>& classOf,
    const std::function<int(std::uint16_t)>& inventoryOf,
    const std::function<std::optional<std::optional<ProjectileArmor>>(std::uint16_t)>& armorOf) {
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
      record.payloadStart = payloadStart;
      if (!reader.skipBits(*length)) break;

      // The layout is the object's class (see GhostClass). When the caller does not
      // know it yet, a full record names it by its mask.
      const std::size_t payloadEnd = payloadStart + *length;
      record.netClass = classOf ? classOf(record.networkId) : GhostClass::Unknown;
      // The score manager's id is fixed on both sides (score_state.h).
      if (record.networkId == kScoreManagerNetworkId) record.netClass = GhostClass::ScoreManager;
      if (record.netClass == GhostClass::Unknown) {
        BitReader peek(packet);
        if (peek.skipBits(payloadStart)) {
          const auto wide = peek.readBits(kSoldierMaskBits);
          if (wide && *wide == kSoldierGhostMask) {
            record.netClass = GhostClass::Soldier;
          } else if (wide && (*wide & ((1u << kObjectStateMaskBits) - 1u)) == kObjectGhostMask) {
            record.netClass = GhostClass::SimpleObject;
          }
        }
      }

      BitReader payload(packet);
      if (record.netClass == GhostClass::Soldier && payload.skipBits(payloadStart)) {
        const int inventory = inventoryOf ? inventoryOf(record.networkId) : -1;
        if (auto state = readSoldierState(payload, reference, SoldierLayout::Ghost, inventory)) {
          record.stateMask = state->mask;
          record.readBits = static_cast<std::uint32_t>(payload.bitPosition() - payloadStart);
          // Taken only when it fitted into the content: otherwise we read the
          // wrong thing and would pass rubbish off as a position.
          if (payload.bitPosition() <= payloadEnd) {
            record.position = state->position;
            record.soldier = std::move(*state);
          }
        }
      } else if (record.netClass == GhostClass::SpawnGroup && payload.skipBits(payloadStart)) {
        if (auto state = readSpawnGroupState(payload)) {
          record.stateMask = state->mask;
          record.readBits = static_cast<std::uint32_t>(payload.bitPosition() - payloadStart);
          if (payload.bitPosition() <= payloadEnd) record.spawnGroup = *state;
        }
      } else if (record.netClass == GhostClass::ControlPoint && payload.skipBits(payloadStart)) {
        if (auto state = readControlPointState(payload)) {
          record.stateMask = state->mask;
          record.readBits = static_cast<std::uint32_t>(payload.bitPosition() - payloadStart);
          if (state->complete && payload.bitPosition() <= payloadEnd) record.controlPoint = *state;
        }
      } else if (record.netClass == GhostClass::Projectile && payload.skipBits(payloadStart)) {
        const auto armor = armorOf ? armorOf(record.networkId)
                                   : std::optional<std::optional<ProjectileArmor>>{};
        if (auto state = readProjectileState(payload, reference, armor.value_or(std::nullopt),
                                             armor.has_value())) {
          record.stateMask = state->mask;
          record.readBits = static_cast<std::uint32_t>(payload.bitPosition() - payloadStart);
          if (payload.bitPosition() <= payloadEnd) {
            record.position = state->position;
            if (state->rotation) {
              const Quatf& q = *state->rotation;
              record.rotation = std::array<float, 4>{q.x, q.y, q.z, q.w};
              record.rotationLength = 1.0f;
            }
            record.projectile = std::move(*state);
          }
        }
      } else if (record.netClass == GhostClass::OverheadCamera && payload.skipBits(payloadStart)) {
        // ELF 0x823ca84. Pan rates, wide-vector position, signed team, enabled.
        const auto mask = payload.readBits(4);
        if (mask && (!(*mask & 1) || payload.skipBits(16))) {
          const auto position = *mask & 2 ? payload.readWideVector(reference, 0.001f) : std::nullopt;
          std::optional<int> team;
          if (*mask & 4) {
            const auto sign = payload.readBits(1), value = payload.readBits(7);
            if (sign && value) team = *sign ? -static_cast<int>(*value) : static_cast<int>(*value);
          }
          const auto enabled = *mask & 8 ? payload.readBool() : std::nullopt;
          if ((!(*mask & 2) || position) && (!(*mask & 4) || team) && (!(*mask & 8) || enabled) &&
              payload.bitPosition() == payloadEnd) {
            record.stateMask = *mask;
            record.position = position;
            record.cameraTeam = team;
            record.cameraEnabled = enabled;
            record.readBits = record.payloadBits;
          }
        }
      } else if (record.netClass == GhostClass::ScoreManager && payload.skipBits(payloadStart)) {
        if (auto state = readScoreState(payload)) {
          record.stateMask = state->mask;
          record.readBits = static_cast<std::uint32_t>(payload.bitPosition() - payloadStart);
          if (state->complete && payload.bitPosition() <= payloadEnd) record.score = *state;
        }
      } else if (record.netClass == GhostClass::Player && payload.skipBits(payloadStart)) {
        if (auto state = readPlayerState(payload)) {
          record.stateMask = state->mask;
          record.readBits = static_cast<std::uint32_t>(payload.bitPosition() - payloadStart);
          if (state->complete && payload.bitPosition() <= payloadEnd) {
            record.player = std::move(*state);
          }
        }
      } else if (record.netClass == GhostClass::SimpleObject && payload.skipBits(payloadStart)) {
        if (const auto state = readSimpleObjectState(payload, reference)) {
          record.stateMask = state->mask;
          record.readBits = static_cast<std::uint32_t>(payload.bitPosition() - payloadStart);
          if (payload.bitPosition() <= payloadEnd) {
            record.position = state->position;
            record.velocity = state->velocity;
            if (state->rotation) {
              const auto& q = *state->rotation;
              const float n = state->rotationLength;
              record.rotationLength = n;
              // 0x62ebc0: a length under 1.19e-7 becomes the identity.
              record.rotation = n < 1.1920929e-7f
                                    ? std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}
                                    : std::array<float, 4>{q[0] / n, q[1] / n, q[2] / n, q[3] / n};
            }
          }
          // What follows the head is the object's other networkables, each with its
          // own mask (docs/functions/network-events.md, "What follows the head"):
          // kept bit by bit, a byte a bit, for the notes' analysis.
          BitReader tail = payload;
          while (tail.bitPosition() < payloadEnd && record.tail.size() < 512) {
            const auto bit = tail.readBits(1);
            if (!bit) break;
            record.tail.push_back(static_cast<std::uint8_t>(*bit));
          }
        }
      }
    }
    out.push_back(record);
  }
  return out;
}

namespace {

std::optional<SimpleObjectState> readSimpleObjectHead(BitReader& reader, const Vec3f& reference,
                                                      bool physical, bool fullPrecision = false,
                                                      const SimpleObjectState* baseline = nullptr) {
  SimpleObjectState out;
  const auto mask = reader.readBits(kObjectStateMaskBits);
  if (!mask) return std::nullopt;
  out.mask = *mask;
  const Vec3f zero{};
  if ((out.mask & kObjectStatePosition) != 0) {
    out.position = reader.readCompressedVector(baseline ? baseline->position.value_or(reference) :
                                               reference, kObjectPositionPrecision);
    if (!out.position) return std::nullopt;
  }
  if (physical && (out.mask & kObjectStateVelocity) != 0) {
    out.velocity = reader.readCompressedVector(baseline ? baseline->velocity.value_or(zero) :
                                               zero, kObjectVelocityPrecision);
    if (!out.velocity) return std::nullopt;
  }
  if (physical && (out.mask & kObjectStateAngular) != 0) {
    out.angular = fullPrecision
        ? reader.readCompressedVector(baseline ? baseline->angular.value_or(zero) : zero,
                                      kObjectVelocityPrecision)
        : reader.readWideVector(zero, kObjectAngularPrecision);
    if (!out.angular) return std::nullopt;
  }
  if ((out.mask & kObjectStateRotation) != 0) {
    // 0x6b7270: each of w, x, y, z is n bits over 2^n − 1 mapped to 2u − 1.
    constexpr float kTop = static_cast<float>((1u << kObjectQuaternionBits) - 1u);
    float wxyz[4];
    for (float& value : wxyz) {
      const auto raw = reader.readBits(kObjectQuaternionBits);
      if (!raw) return std::nullopt;
      value = 2.0f * (static_cast<float>(*raw) / kTop) - 1.0f;
    }
    out.rotation = std::array<float, 4>{wxyz[1], wxyz[2], wxyz[3], wxyz[0]};
    out.rotationLength = std::sqrt(wxyz[0] * wxyz[0] + wxyz[1] * wxyz[1] + wxyz[2] * wxyz[2] +
                                   wxyz[3] * wxyz[3]);
  }
  return out;
}

}  // namespace

std::optional<SimpleObjectState> readSimpleObjectState(BitReader& reader, const Vec3f& reference) {
  // Whether the object is physical decides whether 0x4 and 0x8 are in the record,
  // and the client asks the object (`(*(object+0x44))->+0x130`), not the stream.
  // The stream's own test tells it apart: read as physical, the rotation behind
  // those fields is 1 long; read past fields that are not there, it is not.
  // Measured on the live server: the vehicles (jeep, tank, LAV) come out 1 long
  // read as physical, the radars, the guns and the bipods read as not.
  BitReader physicalReader = reader;
  auto physical = readSimpleObjectHead(physicalReader, reference, true);
  const bool needsAsk = physical && physical->rotation &&
                        (physical->mask & (kObjectStateVelocity | kObjectStateAngular)) != 0;
  if (needsAsk && std::abs(physical->rotationLength - 1.0f) > 0.01f) {
    BitReader plainReader = reader;
    auto plain = readSimpleObjectHead(plainReader, reference, false);
    if (plain && plain->rotation && std::abs(plain->rotationLength - 1.0f) <= 0.01f) {
      reader = plainReader;
      return plain;
    }
  }
  reader = physicalReader;
  return physical;
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

namespace {

std::optional<SoldierWeaponState> readWeaponSlot(BitReader& reader) {
  if (!reader.skipBits(4)) return std::nullopt;  // camera and flags
  const auto fireRate = reader.readBits(2);
  const auto rounds = reader.readBits(rangedBits(1023));
  const auto magazines = reader.readBits(rangedBits(511));
  if (!fireRate || !rounds || *rounds > 1023 || !magazines || *magazines > 511) return std::nullopt;
  return SoldierWeaponState{static_cast<int>(*rounds) - 1, static_cast<int>(*magazines) - 1,
                            static_cast<int>(*fireRate)};
}

}  // namespace

// GhostManager wraps Player::getStateSync in a 10-bit length. Soldiers use
// active weapon slot 0. Slots 1 and 2 are reserved for other control objects.
std::optional<SoldierPlayerSync> readSoldierPlayerSync(BitReader& reader, std::size_t end) {
  const auto length = reader.readBits(10);
  if (!length || reader.bitPosition() + *length != end) return std::nullopt;
  SoldierPlayerSync out;
  const auto hitFlag = reader.readBits(1);
  if (!hitFlag) return std::nullopt;
  out.hitFlag = *hitFlag != 0;
  const auto replenish = reader.readBits(1);
  const auto ability = reader.readBits(1);
  const auto repair = reader.readBits(1);
  if (!replenish || !ability || !repair || !reader.skipBits(8)) return std::nullopt;
  out.givingReplenish = *replenish != 0;
  out.givingAbility = *ability != 0;
  out.repairing = *repair != 0;
  // The hit direction (0x5018a0: presence bit, then 8 bits), then the camera
  // shake (presence bit, 8 bits: × 1/255 × a constant into the camera's +0x4c —
  // not applied).
  const auto hitPresent = reader.readBits(1);
  if (!hitPresent) return std::nullopt;
  if (*hitPresent) {
    const auto byte = reader.readBits(8);
    if (!byte) return std::nullopt;
    out.hitFromDirection = decodeHitFromDirection(*byte);
  }
  const auto shakePresent = reader.readBits(1);
  if (!shakePresent || (*shakePresent && !reader.skipBits(8))) return std::nullopt;
  const auto active = reader.readBits(1);
  if (!active) return std::nullopt;
  if (*active) {
    out.weapon = readWeaponSlot(reader);
    if (!out.weapon) return std::nullopt;
  }
  constexpr std::size_t tailBits = 2 + 32 + 1 + 9 + 3;
  // A heat-using weapon contributes one extra byte at the end of its slot.
  if (*active && reader.bitPosition() + tailBits + 8 == end && !reader.skipBits(8)) {
    return std::nullopt;
  }
  if (reader.bitPosition() + tailBits != end) return std::nullopt;
  const auto otherSlots = reader.readBits(2);
  if (!otherSlots || *otherSlots != 0) return std::nullopt;
  // The spawn tick and the man-down flag: the server writes `+0x104` in 32 bits
  // and `+0x108` in one (Linux `getStateSync` 0x4a18b0, 0x4a18cf); the client
  // reads them into player +0xe4 and +0xe8 (0x5018a0's tail, 0x501c53), the
  // fields `getTimeToSpawn` (0x4f9f80) and `getManDownTimeLeft` (0x504a70) read.
  const auto spawnAtTick = reader.readBits(32);
  const auto manDown = reader.readBits(1);
  if (!spawnAtTick || !manDown) return std::nullopt;
  out.spawnAtTick = *spawnAtTick;
  out.manDown = *manDown != 0;
  const auto negative = reader.readBits(1);
  const auto health = reader.readBits(8);
  if (!negative || !health) return std::nullopt;
  out.health = *negative ? 0.0f : static_cast<float>(*health) / 255.0f;
  // Three flags into player +0x124, +0x187 and +0x188 (0x5018a0's last reads):
  // purpose not established, not used.
  for (bool* flag : {&out.flag124, &out.flag187, &out.flag188}) {
    const auto bit = reader.readBits(1);
    if (!bit) return std::nullopt;
    *flag = *bit != 0;
  }
  return out;
}

namespace {

bool skipWeaponSyncPrefix(BitReader& reader) {
  if (!reader.skipBits(12)) return false;
  for (int i = 0; i < 2; ++i) {
    const auto present = reader.readBits(1);
    if (!present || (*present && !reader.skipBits(8))) return false;
  }
  return true;
}

// The FAAV guns and the M1A2 cupola put heat after the slot zero counts.
std::optional<SeatWeaponState> readBeltFedSeatWeapon(BitReader reader, std::size_t end) {
  if (!skipWeaponSyncPrefix(reader) || reader.readBits(1) != 1u) return std::nullopt;
  const auto ammo = readWeaponSlot(reader);
  const auto heat = reader.readBits(8);
  if (!ammo || !heat) return std::nullopt;
  for (int slot = 1; slot < 3; ++slot) {
    const auto active = reader.readBits(1);
    if (!active || (*active && !readWeaponSlot(reader))) return std::nullopt;
  }
  constexpr std::size_t tailBits = 32 + 1 + 9 + 3;
  if (reader.bitPosition() + tailBits != end) return std::nullopt;
  return SeatWeaponState{ammo->rounds, ammo->magazines, static_cast<float>(*heat) / 255.0f};
}

std::optional<TankWeaponState> readTankWeapons(
    std::span<const std::byte> packet, std::size_t begin, std::size_t end) {
  // Three populated slots (cannon, coaxial, smoke), with heat on the coaxial
  // slot. Each of the two optional bytes preceding the slots adds eight bits.
  constexpr unsigned baseBits = 14 + 3 * 28 + 8 + 45;
  for (unsigned length = baseBits; length <= baseBits + 16; length += 8) {
    if (end < begin + 10 + length) continue;
    BitReader reader(packet);
    if (!reader.skipBits(end - length - 10) || reader.readBits(10) != length ||
        !skipWeaponSyncPrefix(reader) || reader.readBits(1) != 1u) continue;
    const auto cannon = readWeaponSlot(reader);
    if (!cannon || cannon->rounds < 0 || cannon->rounds > 1 ||
        cannon->magazines < 0 || reader.readBits(1) != 1u) continue;
    const auto coax = readWeaponSlot(reader);
    const auto heat = reader.readBits(8);
    if (!coax || !heat || reader.readBits(1) != 1u || !readWeaponSlot(reader) ||
        reader.bitPosition() + 45 != end) continue;
    return TankWeaponState{{cannon->rounds, cannon->magazines, std::nullopt, 1},
                           {coax->rounds, coax->magazines, static_cast<float>(*heat) / 255.0f}};
  }
  return std::nullopt;
}

}  // namespace

std::optional<ControlObjectState> readControlObjectState(
    std::span<const std::byte> packet, const std::function<int(std::uint16_t)>& inventorySize) {
  BitReader reader(packet);
  const auto header = enterGhosts(reader);
  if (!header || !header->controlObjectState) return std::nullopt;

  ControlObjectState out;
  const std::size_t headerStart = reader.bitPosition();
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

  // The rest, as `GhostManager::readControlObjectState` (`BF2.exe`, 0x5b9860)
  // reads it: an optional parachute soldier's id, then an optional soldier
  // kit id, then the baseline — one bit and three bits of
  // index ("Invalid server baseLineIndex" past 7). Then every networkable of the
  // controlled object reads itself with type 3 through its vtable slot +0x30,
  // and for a soldier that slot is `SoldierNetworkable::setNetUpdate` (the
  // vtable at 0x8fcba8, slot 12 = 0x62d4e0).
  const auto flagA = reader.readBits(1);
  if (!flagA || (*flagA == 1 && !reader.skipBits(16))) return out;
  const auto flagB = reader.readBits(1);
  if (!flagB) return out;
  if (*flagB == 1) {
    const auto hasKit = reader.readBits(1);
    if (!hasKit) return out;
    if (*hasKit) {
      const auto kit = reader.readBits(16);
      if (!kit) return out;
      out.kitId = static_cast<std::uint16_t>(*kit);
    }
  }
  out.isSoldier = *flagB == 1;
  const auto delta = reader.readBits(1);
  const auto baseline = reader.readBits(3);
  if (!delta || !baseline) return out;
  out.delta = *delta != 0;
  out.baselineSlot = *baseline;
  out.stateStart = reader.bitPosition();
  out.stateEnd = headerStart + 12 + out.first;
  if (!out.isSoldier) return out;
  const int slots = inventorySize ? inventorySize(out.networkId) : -1;
  if (auto soldier = readSoldierState(reader, out.compressionReference,
                                      SoldierLayout::Controlled, slots);
      soldier && reader.bitPosition() <= out.stateEnd) {
    if (soldier->complete && *flagA == 0 && out.kitId != 0) {
      out.playerSync = readSoldierPlayerSync(reader, out.stateEnd);
    }
    out.soldier = std::move(*soldier);
  }
  return out;
}

namespace {

std::optional<std::vector<SeatRotation>> readRotationalBundles(
    BitReader& reader, unsigned bundles, bool controlled) {
  // RotationalBundleNetworkable::setNetUpdate (0x6322e0), type 3 for control.
  const unsigned width = controlled ? 16 : 12;
  const float max = static_cast<float>((1u << width) - 1);
  std::vector<SeatRotation> rotations(bundles);
  for (auto& rotation : rotations) {
    const auto mask = reader.readBits(7);
    if (!mask) return std::nullopt;
    for (unsigned field = 0; field < 7; ++field) {
      if (!(*mask & (1u << field))) continue;
      const auto value = reader.readBits(field == 6 ? 16 : width);
      if (!value) return std::nullopt;
      if (field < rotation.size()) {
        const float normalized = 2 * static_cast<float>(*value) / max - 1;
        rotation[field] = std::abs(normalized) <= 2 / max ? 0.0f : normalized * 180;
      } else if (field < 6) {
        const float normalized = 2 * static_cast<float>(*value) / max - 1;
        rotation.normalizedRates[field-3] = std::abs(normalized) <= 2/max ? 0.0f : normalized;
      } else {
        // BF2.exe 0x6322e0: mask40 uses16 bits independently of angle precision.
        const float normalized = 2 * static_cast<float>(*value) / 65535.0f - 1;
        rotation.excessInput = std::abs(normalized) < 1.0f/32767.0f ? 0.0f : normalized*41.0f;
      }
    }
  }
  return rotations;
}

// EngineNetworkable::setNetUpdate (Linux 0x5c9bd0): an 11-bit mask, then
// 0x2 the gear (gearBits), 0x4/0x8/0x10 16 bits each, 0x20/0x40/0x80 12 bits
// each, 0x100 16 bits when the engine type has 0x20, 0x200 two bits, 0x400
// (engine type bit 7) 1 + 16 + 8 + 1 + 2 bits. Mask 0x1 reads nothing.
// EngineNetworkable::setNetUpdate (Linux 0x5c9bd0): 2u/65535 − 1, zero when
// |v| ≤ 3.0518044e-05; 2u/4095 − 1, zero when |v| ≤ 0.0004884005.
float engineUnit16(std::uint32_t u) {
  const float half = static_cast<float>(static_cast<double>(u) / 65535.0);
  const float v = half + half - 1.0f;
  return std::fabs(v) > 3.0518044e-05f ? v : 0.0f;
}
std::optional<float> engineUnit12(std::uint32_t u) {
  const float half = static_cast<float>(static_cast<double>(u) / 4095.0);
  const float v = half + half - 1.0f;
  if (std::fabs(v) > 0.0004884005f || std::isnan(v)) return v * 1.25f;
  return std::nullopt;
}

std::optional<EngineNetState> readEngineNetworkable(BitReader& reader, const DescriptorMember& member) {
  EngineNetState state;
  const auto mask = reader.readBits(11);
  if (!mask) return std::nullopt;
  state.mask = *mask;
  if (*mask & 0x2u) {
    const auto gear = reader.readBits(member.gearBits);
    if (!gear) return std::nullopt;
    state.gear = *gear;
  }
  const std::array<std::pair<unsigned, std::optional<float>*>, 3> axes{
      {{0x4u, &state.axis4}, {0x8u, &state.axis8}, {0x10u, &state.axis10}}};
  for (const auto& [field, out] : axes) {
    if (!(*mask & field)) continue;
    const auto u = reader.readBits(16);
    if (!u) return std::nullopt;
    *out = engineUnit16(*u);
  }
  const std::array<std::pair<unsigned, std::optional<float>*>, 3> analog{
      {{0x20u, &state.rpm}, {0x40u, &state.field2e8}, {0x80u, &state.field2dc}}};
  for (const auto& [field, out] : analog) {
    if (!(*mask & field)) continue;
    const auto u = reader.readBits(12);
    if (!u) return std::nullopt;
    const auto value = engineUnit12(*u);
    // 0x20 and 0x40 store 0 in the dead band; 0x80 leaves the field.
    if (value || field != 0x80u) *out = value.value_or(0.0f);
  }
  if (member.engineField100 && (*mask & 0x100u)) {
    const auto u = reader.readBits(16);
    if (!u) return std::nullopt;
    state.field100 = engineUnit16(*u);
  }
  if (*mask & 0x200u) {
    const auto on = reader.readBits(1);
    const auto negative = reader.readBits(1);
    if (!on || !negative) return std::nullopt;
    // 0x5ca63b: 1 (0xb2f3bc) or 0 (0xb2d910), sign flipped by the second bit.
    const float value = *on ? 1.0f : 0.0f;
    state.flag200 = *negative ? -value : value;
  }
  if (member.engineBlock400 && (*mask & 0x400u)) {
    const auto sign = reader.readBits(1);
    const auto u = reader.readBits(16);
    const auto load = reader.readBits(8);
    const auto sign368 = reader.readBits(1);
    const auto value368 = reader.readBits(2);
    if (!sign || !u || !load || !sign368 || !value368) return std::nullopt;
    state.field400 = static_cast<float>(static_cast<double>(*u) / 65535.0);
    state.field400Negative = *sign == 1;
    state.load = static_cast<float>(static_cast<double>(*load) / 255.0);
    state.value368 = *sign368 ? -static_cast<int>(*value368) : static_cast<int>(*value368);
  }
  return state;
}

// SpringNetworkable::setNetUpdate (Linux 0x5e0080): one bit; when set two
// high-compressed vectors at 0.002 (0xb6db18), then with +0x74 one bit and,
// when that is 1, 16 bits mapped to -1..1.
bool skipSpringNetworkable(BitReader& reader, const DescriptorMember& member) {
  const auto changed = reader.readBits(1);
  if (!changed) return false;
  if (!*changed) return true;
  if (!reader.readHighCompressedVector({}, 0.002f) || !reader.readHighCompressedVector({}, 0.002f)) return false;
  if (!member.springExtra) return true;
  const auto extra = reader.readBits(1);
  if (!extra) return false;
  return *extra != 1 || reader.skipBits(16);
}

std::optional<std::vector<SeatRotation>> readDescriptorMembers(
    BitReader& reader, std::span<const DescriptorMember> members, bool controlled,
    std::vector<EngineNetState>* engines) {
  std::vector<SeatRotation> rotations;
  for (const auto& member : members) {
    switch (member.kind) {
      case DescriptorMember::Kind::Rotational: {
        const auto one = readRotationalBundles(reader, 1, controlled);
        if (!one) return std::nullopt;
        rotations.push_back(one->front());
        break;
      }
      case DescriptorMember::Kind::Engine:
      {
        const auto engine = readEngineNetworkable(reader, member);
        if (!engine) return std::nullopt;
        if (engines) engines->push_back(*engine);
        break;
      }
      case DescriptorMember::Kind::Spring:
        if (!skipSpringNetworkable(reader, member)) return std::nullopt;
        break;
    }
  }
  return rotations;
}

bool skipVehiclePhysicsTail(BitReader& reader, unsigned mask) {
  if ((mask & 0x4000u) != 0 &&
      (!reader.readCompressedVector({}, 0.001f) || !reader.readCompressedVector({}, 0.001f))) {
    return false;
  }
  for (const unsigned field : {0x20u, 0x40u, 0x100u, 0x200u}) {
    if ((mask & field) != 0 && !reader.readCompressedVector({}, 0.0001f)) return false;
  }
  for (const unsigned field : {0x80u, 0x8000u}) {
    if ((mask & field) != 0 && !reader.skipBits(1)) return false;
  }
  return true;
}

std::optional<int> readArmorValue(BitReader& reader, const VehicleArmor& armor) {
  if (armor.maximum <= 0 || armor.maximum > 100000 || armor.wreck < 0 || armor.wreck > 100000) {
    return std::nullopt;
  }
  const auto span = static_cast<unsigned>(armor.maximum + armor.wreck);
  const auto value = reader.readBits(rangedBits(span));
  return value && *value <= span ? std::optional(static_cast<int>(*value) - armor.wreck)
                                : std::nullopt;
}

std::optional<std::vector<SeatRotation>> readTankTurret(
    BitReader& reader, unsigned mask, std::size_t end, bool controlled, const VehicleArmor& armor,
    unsigned bundles, std::span<const DescriptorMember> members,
    std::vector<EngineNetState>* engines = nullptr) {
  if (engines) engines->clear();
  if (!skipVehiclePhysicsTail(reader, mask)) return std::nullopt;
  if ((mask & 0x10u) && !readArmorValue(reader, armor)) return std::nullopt;
  if ((mask & 0x400u) && !reader.skipBits(2)) return std::nullopt;
  for (const unsigned field : {0x800u, 0x1000u, 0x20000u}) {
    if ((mask & field) && !reader.skipBits(4)) return std::nullopt;
  }
  if ((mask & 0x10000u) && !reader.skipBits(rangedBits(15))) return std::nullopt;
  if ((mask & 0x40000u) && !reader.skipBits(1)) return std::nullopt;

  // Authored networked rotational bundles follow SimpleObjectNetworkable.
  // The rest of the descriptor also contains wheel and engine state.
  if (!members.empty() && controlled) {
    // The controlled record continues past the descriptor (getStateSync),
    // so its end cannot check the cut: keep the leading rotational bundles.
    const auto prefix = std::ranges::find_if(members, [](const DescriptorMember& member) {
      return member.kind != DescriptorMember::Kind::Rotational; });
    members = members.first(static_cast<std::size_t>(prefix - members.begin()));
    if (members.empty()) return std::vector<SeatRotation>{};
  }
  const auto start = reader;
  auto rotations = members.empty() ? readRotationalBundles(reader, bundles, controlled)
                                   : readDescriptorMembers(reader, members, controlled, engines);
  // A ghost record holds exactly the root group's networkables: a cut that
  // does not end on the record's last bit is not this layout. Then only the
  // leading rotational bundles are trusted, as before the cut was known.
  const bool cut = std::ranges::any_of(members, [](const DescriptorMember& member) {
    return member.kind != DescriptorMember::Kind::Rotational; });
  if (cut && (!rotations || reader.bitPosition() != end)) {
    if (engines) engines->clear();
    reader = start;
    const auto prefix = std::ranges::find_if(members, [](const DescriptorMember& member) {
      return member.kind != DescriptorMember::Kind::Rotational; });
    const auto count = static_cast<unsigned>(prefix - members.begin());
    if (!count) return std::nullopt;
    rotations = readRotationalBundles(reader, count, controlled);
  }
  if (!rotations || reader.bitPosition() > end) return std::nullopt;
  return rotations;
}

}  // namespace

std::optional<std::vector<SeatRotation>> readSeatRotations(
    std::span<const std::byte> packet, std::size_t begin, std::size_t end,
    unsigned bundles, bool controlled, std::optional<SeatWeaponState>* weapon) {
  if (weapon) weapon->reset();
  if (begin > end || end > packet.size() * 8) return std::nullopt;
  BitReader reader(packet);
  if (!reader.skipBits(begin)) return std::nullopt;
  // PlayerControlObjectNetworkable with a seat list (BF2.exe 0x635b40).
  const auto items = reader.readBits(4);
  if (!items) return std::nullopt;
  for (unsigned slot = 0; slot < 3; ++slot) {
    if (*items & (1u << slot)) {
      const auto item = reader.readBits(4);
      if (!item || *item > 8) return std::nullopt;
    }
  }
  if ((*items & 8) && !reader.skipBits(1)) return std::nullopt;

  const auto rotations = readRotationalBundles(reader, bundles, controlled);
  if (!rotations) return std::nullopt;
  if (controlled) {
    const auto length = reader.readBits(10);  // Player::getStateSync, after the descriptor.
    if (!length || reader.bitPosition() + *length != end) return std::nullopt;
    if (weapon) *weapon = readBeltFedSeatWeapon(reader, end);
    if (!reader.skipBits(*length)) return std::nullopt;
  }
  if (reader.bitPosition() != end) return std::nullopt;
  return rotations;
}

std::optional<int> readVehicleHealth(std::span<const std::byte> packet,
                                    const GhostRecord& record, const VehicleArmor& armor) {
  if ((record.stateMask & 0x10u) == 0 || !record.readBits) return std::nullopt;
  BitReader reader(packet);
  if (!reader.skipBits(record.payloadStart + record.readBits) ||
      (armor.mobile && !skipVehiclePhysicsTail(reader, record.stateMask))) return std::nullopt;
  const auto health = readArmorValue(reader, armor);
  return reader.bitPosition() <= record.payloadStart + record.payloadBits ? health : std::nullopt;
}

std::optional<std::vector<SeatRotation>> readTankTurretRotations(
    std::span<const std::byte> packet, const GhostRecord& record, const VehicleArmor& armor,
    unsigned bundles, std::span<const DescriptorMember> members, std::vector<EngineNetState>* engines) {
  if (engines) engines->clear();
  BitReader reader(packet);
  if (!record.readBits || !reader.skipBits(record.payloadStart + record.readBits)) return std::nullopt;
  auto rotations = readTankTurret(reader, record.stateMask, record.payloadStart + record.payloadBits, false, armor,
                                  bundles, members, engines);
  if (!rotations && engines) engines->clear();
  return rotations;
}

std::optional<std::vector<SeatRotation>> readTankTurretRotations(
    std::span<const std::byte> packet, const ControlObjectState& control, const VehicleArmor& armor,
    std::optional<TankWeaponState>* weapons, unsigned bundles, std::span<const DescriptorMember> members) {
  if (weapons) weapons->reset();
  if (control.isSoldier || !control.stateStart || control.stateEnd > packet.size() * 8) return std::nullopt;
  BitReader reader(packet);
  if (!reader.skipBits(control.stateStart)) return std::nullopt;
  const auto head = readSimpleObjectHead(reader, control.compressionReference, true, true);
  if (!head) return std::nullopt;
  const auto rotations = readTankTurret(reader, head->mask, control.stateEnd, true, armor, bundles, members);
  if (rotations && weapons) *weapons = readTankWeapons(packet, reader.bitPosition(), control.stateEnd);
  return rotations;
}

std::optional<SimpleObjectState> readControlledVehicleState(
    std::span<const std::byte> packet, const ControlObjectState& control, bool mobile,
    const SimpleObjectState* baseline) {
  if (control.isSoldier || !control.stateStart || (control.delta && !baseline)) return std::nullopt;
  BitReader reader(packet);
  if (!reader.skipBits(control.stateStart)) return std::nullopt;
  auto state = readSimpleObjectHead(reader, control.compressionReference, mobile, true, baseline);
  if (!state || reader.bitPosition() > control.stateEnd) return std::nullopt;
  return state;
}

std::optional<GhostHeader> readGhostHeader(std::span<const std::byte> packet) {
  BitReader reader(packet);
  return enterGhosts(reader);
}

std::optional<std::uint16_t> readRevivedSoldier(const RemoteEvent& event) {
  if (event.category != kGameLogicCategory || event.number != kGameLogicRevived ||
      event.data.size() < 2) return std::nullopt;
  // BF2.exe 0x4e1fed reads a network object ID from the first two payload bytes.
  const auto id = static_cast<std::uint16_t>(event.data[0] | (event.data[1] << 8));
  if (id == 0) return std::nullopt;
  return id;
}

std::optional<UnitSpot> readUnitSpot(const RemoteEvent& event) {
  if (event.category != 2 || event.number != 43 || event.data.size() != 24) return std::nullopt;
  const auto word = [&](std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(event.data[offset + i]) << (8 * i);
    }
    return value;
  };
  const auto target = static_cast<std::uint16_t>(word(0) >> 16);
  const int team = event.data[4];
  const float x = std::bit_cast<float>(word(8));
  const float z = std::bit_cast<float>(word(12));
  if (team < 1 || team > 2 || !std::isfinite(x) || !std::isfinite(z)) return std::nullopt;
  return UnitSpot{target, team, x, z, static_cast<std::uint16_t>(word(0))};
}

std::vector<Event> readEvents(std::span<const std::byte> packet, EventWalk* walk) {
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
  const auto block = reader.readBits(5);
  const auto resend = reader.readBits(1);
  if (walk != nullptr) {
    walk->declared = *count;
    walk->hasBlock = true;
    walk->block = block.value_or(0);
    walk->resend = resend.value_or(0) == 1;
  }

  for (std::uint32_t i = 0; i < *count; ++i) {
    BitReader peek = reader;
    auto event = readEvent(reader);
    if (!event) {
      if (walk != nullptr) walk->brokeOn = peek.readBits(kEventTypeBits).value_or(0xFFFFFFFFu);
      break;
    }
    events.push_back(*event);
  }
  if (walk != nullptr) walk->read = static_cast<std::uint32_t>(events.size());
  return events;
}

float decodeHitFromDirection(std::uint32_t byte) {
  constexpr float kTwoPi = 6.28318530717958647692f;     // 0x8ac48c
  constexpr float kDeadZone = 1.0f / 127.5f;            // 0x8ac490, 0.00784314
  const float s = static_cast<float>(byte) * (2.0f / 255.0f) - 1.0f;  // 0x8ac480 = 1/255
  return std::abs(s) <= kDeadZone ? 0.0f : s * kTwoPi;
}

}  // namespace obf2::net::bf2
