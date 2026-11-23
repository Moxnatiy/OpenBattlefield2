// Parsing real packets of the original server.
//
// The sample in tests/data/bf2-world.bin was captured with
//   tools/linuxded/capture.py --stage world --out tests/data/bf2-world.bin
// on a server with the map dalian_plant. Keeping it in the repository is cheap, and
// the check it gives is one no synthetic packet can: if the length goes wrong in
// even one event, the next one reads as rubbish.
#include <cmath>
#include <cstdio>
#include <map>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>

#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/end_of_round.h"
#include "obf2/net/bitstream.h"
#include "check.h"

namespace {

using namespace obf2;

void testRetailEndOfRoundInfo() {
  std::ifstream file(std::string(OBF2_TEST_DATA) + "/bf2-end-of-round.bin",
                     std::ios::binary | std::ios::ate);
  CHECK(file && file.tellg() == 997);
  if (!file || file.tellg() != 997) return;
  std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
  file.seekg(0);
  CHECK(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));

  const auto info = net::bf2::parseEndOfRoundInfo(bytes);
  CHECK(info);
  if (!info) return;
  CHECK_EQ(info->delaySeconds, 15);
  CHECK_EQ(info->stats.at("np0"), "P. Brosnan");
  CHECK_EQ(info->stats.at("ssp0"), "8");
  CHECK_EQ(info->players.size(), 26u);
  CHECK_EQ(info->players[2].player, 253);
  CHECK_EQ(info->players[2].scores[3], 6);
  CHECK_EQ(info->history.size(), 6u);
  CHECK_EQ(info->history.back().map, "dalian_plant");
  CHECK_EQ(info->history.back().teams[0].faction, "CH");
  CHECK_EQ(info->history.back().teams[0].tickets, 139);
  CHECK_EQ(info->history.back().teams[1].tickets, 0);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    CHECK(!net::bf2::parseEndOfRoundInfo(std::span(bytes).first(length)));
  }
  bytes[10] ^= std::byte{0x80};
  CHECK(!net::bf2::parseEndOfRoundInfo(bytes));
}

void testServerExternalViews() {
  for (const bool allowed : {false, true}) {
    std::array<std::byte, 64> block{};
    net::BitWriter writer(block);
    writer.writeBits(6, 16);
    writer.writeString("Server", 6);
    writer.writeBits(2, 16);
    writer.writeBits(4, 16);
    writer.writeString("127.", 4);
    writer.writeBits(0, 16);
    const auto flags = writer.bitPosition() / 8;
    writer.writeBits(allowed ? 0 : 63, 6);
    writer.writeBool(allowed);
    writer.writeBits(123, 32);
    CHECK(net::bf2::readServerExternalViews(block) == std::optional<bool>(allowed));
    CHECK(!net::bf2::readServerExternalViews(std::span(block).first(flags)));
  }
}

void testRetailServerMatchOptions() {
  std::array<std::byte, 64> block{};
  net::BitWriter writer(block);
  writer.writeBits(0, 16); // server name
  writer.writeBits(0, 16); // welcome messages
  writer.writeBits(0, 1);  // request response
  writer.writeBool(true); // voice
  writer.writeBits(0, 3);
  writer.writeBool(false); // free camera
  writer.writeBool(true);  // external views
  writer.writeBits(0, 32);  // voice address
  writer.writeBits(55125, 16);
  writer.writeBits(55123, 16);
  writer.writeBits(6, 32);  // voice quality 3, signed magnitude
  for (unsigned width : {16u, 32u, 32u, 32u, 1u}) writer.writeBits(0, width);
  writer.writeBool(true);  // voting
  writer.writeBits(0, 17);
  writer.writeBool(true);  // team vote only
  const auto bytes = std::span(block).first(writer.byteSize());
  const auto setup = net::bf2::readServerMatchOptions(bytes);
  CHECK(setup && !setup->freeCamera && setup->voice && setup->voting && setup->teamVoteOnly);
  CHECK(setup && setup->voiceServerPort == 55125 && setup->voiceClientPort == 55123 && setup->voiceQuality == 3);
  CHECK(!net::bf2::readServerMatchOptions(bytes.first(bytes.size() - 1)));
}

std::vector<std::vector<std::byte>> loadCapture(const std::string& path) {
  std::vector<std::vector<std::byte>> packets;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return packets;

  // The format is simple: a u32 length per packet, then the bytes.
  while (true) {
    std::uint32_t length = 0;
    if (std::fread(&length, sizeof(length), 1, file) != 1) break;
    if (length == 0 || length > 4096) break;
    std::vector<std::byte> packet(length);
    if (std::fread(packet.data(), 1, length, file) != length) break;
    packets.push_back(std::move(packet));
  }
  std::fclose(file);
  return packets;
}

void testRetailServerAllowsExternalViews() {
  net::bf2::DataBlockAssembler blocks;
  for (const auto& packet : loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-karkand-lives.bin")) {
    for (const auto& event : net::bf2::readEvents(packet)) {
      if (!event.block) continue;
      const auto ready = blocks.feed(*event.block);
      if (!ready || ready->first != net::bf2::kServerInfoBlock) continue;
      CHECK(net::bf2::readServerExternalViews(ready->second) == std::optional<bool>(true));
      return;
    }
  }
  CHECK(false);
}

void testWorldCaptureIsFullyDecoded() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-world.bin");
  CHECK(!packets.empty());

  int objects = 0, positioned = 0, players = 0;
  std::string playerName;

  for (const auto& packet : packets) {
    const auto events = obf2::net::bf2::readEvents(packet);
    for (const auto& event : events) {
      // A type number is never greater than 69: anything above that means the
      // previous event was read at the wrong length.
      CHECK(event.type <= 69);
      CHECK(!obf2::net::bf2::eventName(event.type).empty());
      if (event.object) {
        ++objects;
        if (event.object->position) ++positioned;
      }
      if (event.player) {
        ++players;
        playerName = event.player->name;
      }
    }
  }

  // The server sends the world of the map dalian_plant: there are many objects there.
  CHECK(objects >= 40);
  CHECK(positioned >= 40);

  // The player is us. The leading space is not an error: on a server without ranking
  // the engine assembles the name as "clan tag + space + name", and the tag is empty.
  CHECK_EQ(players, 1);
  CHECK_EQ(playerName, std::string(" OpenBF2"));
}

void testHeightsAreOnTheTerrain() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-world.bin");
  int checked = 0;
  for (const auto& packet : packets) {
    for (const auto& event : obf2::net::bf2::readEvents(packet)) {
      if (!event.object || !event.object->position) continue;
      const auto& p = *event.object->position;
      // If the layout is shifted by even a bit, floats produce numbers like 1e38.
      // dalian_plant's terrain keeps within these bounds.
      CHECK(p.y > 100.0f && p.y < 300.0f);
      CHECK(p.x > -2048.0f && p.x < 2048.0f);
      CHECK(p.z > -2048.0f && p.z < 2048.0f);
      ++checked;
    }
  }
  CHECK(checked >= 40);
}

}  // namespace

// The sample tests/data/bf2-ghosts.bin was captured after a full handshake
// (`capture.py --stage spawn`): it already holds a ghost stream.
void testGhostStreamIsWalkable() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-ghosts.bin");
  CHECK(!packets.empty());

  int withGhosts = 0, records = 0;
  std::map<std::uint16_t, int> perObject;
  for (const auto& packet : packets) {
    const auto header = obf2::net::bf2::readGhostHeader(packet);
    if (!header) continue;
    ++withGhosts;
    if (header->controlObjectState) continue;

    const auto found = obf2::net::bf2::readGhostRecords(packet);
    // Every record has to be read: the engine relies on the length in a record
    // letting even an unknown object be skipped.
    CHECK_EQ(found.size(), std::size_t(header->records));
    for (const auto& record : found) {
      CHECK(record.kind != 2);       // kind 2 is a stream error
      ++records;
      ++perObject[record.networkId];
    }
  }

  CHECK(withGhosts >= 100);
  CHECK(records >= 100);
  // Moving objects are updated almost every packet, statics once.
  CHECK(perObject.size() >= 10);
}

// The controlled-object state: the server says itself which object we control.
// The number travels in 16 bits right after the triple of numbers, and the engine
// hands it to `NetworkManager::getObject` (0x445dc3) — so it is not a guess.
void testControlObjectStateNamesItsObject() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-ghosts.bin");
  CHECK(!packets.empty());

  std::map<std::uint16_t, int> perObject;
  std::int32_t previousCounter = -1;
  for (const auto& packet : packets) {
    const auto state = obf2::net::bf2::readControlObjectState(packet);
    if (!state) continue;
    ++perObject[state->networkId];
    // The controlled object exists, so a zero number cannot occur here.
    CHECK(state->networkId != 0);
    // In the captured stream the counter only grows.
    CHECK(state->counter >= previousCounter);
    previousCounter = state->counter;
  }

  CHECK(!perObject.empty());
  // Within one capture the object does not change: we control the same one throughout.
  CHECK_EQ(perObject.size(), std::size_t(1));
}

// An object's position in a record's content: 19 bits of mask, and if bit 1 is set
// in it — a compressed vector. The reference point arrives in the same packet's
// controlled-object state.
void testGhostRecordsCarryPositions() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-ghosts.bin");
  CHECK(!packets.empty());

  obf2::Vec3f reference;
  int withPosition = 0;
  int onTheMap = 0;
  // An object's class is told by its first full record and kept (bf2_events.h).
  std::map<std::uint16_t, obf2::net::bf2::GhostClass> classes;
  const auto classOf = [&](std::uint16_t id) {
    const auto found = classes.find(id);
    return found == classes.end() ? obf2::net::bf2::GhostClass::Unknown : found->second;
  };
  for (const auto& packet : packets) {
    if (const auto state = obf2::net::bf2::readControlObjectState(packet)) {
      reference = state->compressionReference;
      continue;
    }
    for (const auto& record : obf2::net::bf2::readGhostRecords(packet, reference, classOf)) {
      if (record.netClass != obf2::net::bf2::GhostClass::Unknown) {
        classes[record.networkId] = record.netClass;
      }
      if (!record.position) continue;
      ++withPosition;
      // Dalian is 2048 metres across, so any position on it lies within these
      // bounds. Rubbish would fly out of them at once.
      const obf2::Vec3f& at = *record.position;
      if (std::abs(at.x) < 1024.0f && std::abs(at.z) < 1024.0f && at.y > -100.0f &&
          at.y < 1000.0f) {
        ++onTheMap;
      }
    }
  }

  CHECK(withPosition > 0);
  // Were the layout wrong, the numbers would scatter: a match would be a rarity
  // rather than the rule.
  CHECK_EQ(onTheMap, withPosition);
}

// Packets with a controlled-object state are almost the whole stream after the
// player spawns. The walk past that state was written out from the binary, but it
// is the data that checks it: after the walk exactly as many records have to read
// as the header named.
void testControlObjectStateIsSkippable() {
  // A separate capture: this one was taken after the player spawned and with input
  // sent, so the controlled-object state is in almost every packet of it.
  //   openbf2 --connect <host> --level dalian_plant --frames 6000 \
  //           --click --mouse 727 546 --record tests/data/bf2-spawned.bin
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-spawned.bin");
  CHECK(!packets.empty());

  int withControl = 0, walked = 0;
  for (const auto& packet : packets) {
    const auto header = obf2::net::bf2::readGhostHeader(packet);
    if (!header || !header->controlObjectState || header->records == 0) continue;
    ++withControl;
    const auto found = obf2::net::bf2::readGhostRecords(packet);
    if (found.size() == std::size_t(header->records)) ++walked;
  }

  std::printf("  packets with a controlled-object state: %d, walked to the end: %d\n", withControl,
              walked);
  CHECK(withControl > 100);
  // The state's first twelve bits are its own size (the writer 0x5b9230 and the
  // reader 0x5b9860 both say so), so skipping it needs no branch inside it: every
  // packet reaches its records. The field-by-field walk this replaced managed 199.
  CHECK_EQ(walked, withControl);
}

// The kit and its pickup, laid out as `CreateKitEvent::deSerialize` (Linux server
// 0x422b30) and `HandlePickupEvent::deSerialize` (0x428a60) read them. Kit template
// 3186 (MEC_Specops) and soldier 1666 were seen together on the live server; the
// kit's id, the player, the position and the two small values are made up.
void testKitAndPickupAreRead() {
  std::vector<std::byte> buffer(64);
  obf2::net::BitWriter writer(buffer);
  writer.writeBits(31, obf2::net::bf2::kEventTypeBits);
  writer.writeBits(3186, 32);
  writer.writeBits(2210, 16);
  for (const float value : {-161.0f, 158.2f, -263.5f}) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    writer.writeBits(raw, 32);
  }
  writer.writeBits(1, 4);
  writer.writeBits(3, 4);
  writer.writeBits(14, obf2::net::bf2::kEventTypeBits);
  writer.writeBits(250, 8);
  writer.writeBits(2210, 16);
  writer.writeBits(1666, 16);
  CHECK(writer.ok());

  obf2::net::BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto kit = obf2::net::bf2::readEvent(reader);
  CHECK(kit.has_value() && kit->kit.has_value());
  if (kit && kit->kit) {
    CHECK_EQ(kit->kit->templateId, std::uint32_t(3186));
    CHECK_EQ(kit->kit->networkId, std::uint16_t(2210));
    CHECK(std::abs(kit->kit->position.y - 158.2f) < 0.001f);
    CHECK_EQ(kit->kit->value24, std::uint32_t(1));
    CHECK_EQ(kit->kit->value28, 2);  // stored minus one
  }
  const auto pickup = obf2::net::bf2::readEvent(reader);
  CHECK(pickup.has_value() && pickup->pickup.has_value());
  if (pickup && pickup->pickup) {
    CHECK_EQ(pickup->pickup->player, std::uint32_t(250));
    CHECK_EQ(pickup->pickup->first, std::uint16_t(2210));
    CHECK_EQ(pickup->pickup->second, std::uint16_t(1666));
  }
}

// `KilledByEvent` ends in a compressed position (Linux 0x42a920), whose length is
// its own. Taken by a fixed table, the packet a death arrives in was read off its
// bits: the event after it came out as type 0, and the ghost header's time as
// 877259529 between 322192 and 322195 — the game tick, and the spawn countdown with
// it, never recovered. `tests/data/bf2-killed-by.bin` is five packets around a
// suicide on the live server; the third carries the kill.
static void testKilledByEventIsWalked() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-killed-by.bin");
  CHECK_EQ(packets.size(), std::size_t(5));
  if (packets.size() != 5) return;
  const auto events = obf2::net::bf2::readEvents(packets[2]);
  CHECK_EQ(events.size(), std::size_t(2));
  if (events.size() == 2) {
    CHECK_EQ(events[0].type, 21u);
    CHECK_EQ(events[1].type, 11u);
  }
  std::uint32_t previous = 0;
  for (const auto& packet : packets) {
    const auto header = obf2::net::bf2::readGhostHeader(packet);
    CHECK(header.has_value());
    if (!header) continue;
    if (previous != 0) CHECK(header->time > previous && header->time - previous < 10);
    previous = header->time;
  }
}

// A round reset on a live coop server (tests/data/bf2-round-reset.bin, two packets
// of one session). The first carries the reset — category 0 event 6, the status,
// then `NEReset` with the highest network id — behind a `CommanderEvent` (19); the
// second the squad orders (24). Both used to break the walk, and `NEReset` was
// lost with the rest: the client never answered it, and the server ghosted it
// nothing more.
static void testRoundResetIsWalked() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-round-reset.bin");
  CHECK_EQ(packets.size(), std::size_t(2));
  if (packets.size() != 2) return;
  for (const auto& packet : packets) {
    obf2::net::bf2::EventWalk walk;
    obf2::net::bf2::readEvents(packet, &walk);
    CHECK(!walk.brokeOn.has_value());
    CHECK_EQ(walk.read, walk.declared);
  }
  bool reset = false;
  for (const auto& event : obf2::net::bf2::readEvents(packets[0])) {
    if (event.remote && event.remote->category == obf2::net::bf2::kNetworkCategory &&
        event.remote->number == obf2::net::bf2::kNetReset) {
      reset = true;
      CHECK(event.remote->value.has_value() && *event.remote->value == 2146);
    }
  }
  CHECK(reset);
}

// The squad events as OpenBF2's `deSerialize` reads them: the name (22), the
// lock (23) and the leader (28, `JoinSquadEvent`'s codec), then the server info
// block's head (block 0) up to `votingEnabled`.
static void testSquadEventsAndServerInfo() {
  std::vector<std::byte> buffer(64);
  obf2::net::BitWriter writer(buffer);
  writer.writeBits(22, obf2::net::bf2::kEventTypeBits);
  writer.writeBits(3, 8);   // squad
  writer.writeBits(2, 8);   // team
  writer.writeBits(1, 8);   // player
  writer.writeBits(7, 8);   // length
  for (char c : std::string("Charlie")) writer.writeBits(static_cast<std::uint8_t>(c), 8);
  writer.writeBits(23, obf2::net::bf2::kEventTypeBits);
  writer.writeBits(3, 8);
  writer.writeBits(2, 8);
  writer.writeBits(1, 1);
  writer.writeBits(28, obf2::net::bf2::kEventTypeBits);
  writer.writeBits(2, 8);
  writer.writeBits(1, 8);
  writer.writeBits(3, 8);
  writer.writeBits(11, obf2::net::bf2::kEventTypeBits);  // something after, to prove the walk
  CHECK(writer.ok());
  obf2::net::BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto name = obf2::net::bf2::readEvent(reader);
  CHECK(name && name->squadName && name->squadName->name == "Charlie" &&
        name->squadName->squad == 3 && name->squadName->team == 2 && name->squadName->player == 1);
  const auto lock = obf2::net::bf2::readEvent(reader);
  CHECK(lock && lock->squadPrivate && lock->squadPrivate->isPrivate);
  const auto leader = obf2::net::bf2::readEvent(reader);
  CHECK(leader && leader->type == 28u);

  std::vector<std::byte> block(64);
  obf2::net::BitWriter info(block);
  const std::string game = "OpenBF2 reference";
  info.writeBits(static_cast<std::uint32_t>(game.size()), 16);
  for (char c : game) info.writeBits(static_cast<std::uint8_t>(c), 8);
  info.writeBits(0, 16);  // no welcome lines
  info.writeBits(0, 1);   // requestResponse
  info.writeBits(1, 1);   // voip
  for (const unsigned width : {5u, 32u, 16u, 16u, 32u, 16u}) info.writeBits(0, width);
  for (int i = 0; i < 3; ++i) info.writeBits(0, 32);
  info.writeBits(1, 1);  // content checking
  info.writeBits(1, 1);  // voting
  CHECK(info.ok());
  const auto head =
      obf2::net::bf2::parseServerInfo(std::span<const std::byte>(block.data(), info.byteSize()));
  CHECK(head && head->gameName == game && head->voipEnabled == true &&
        head->votingEnabled == true);
}

// A simple object's head (`readSimpleObjectState`): the rotation behind an
// angular velocity that a physical object sends and another does not. The same
// quaternion — 90° about Y — has to come out of both, 1 long.
static void testSimpleObjectRotation() {
  const auto quaternion = [](obf2::net::BitWriter& w) {
    const float wxyz[4] = {0.70710678f, 0.0f, 0.70710678f, 0.0f};
    for (const float v : wxyz) {
      w.writeBits(static_cast<std::uint32_t>(std::lround((v + 1.0f) * 0.5f * 16383.0f)), 14);
    }
  };
  for (const bool physical : {true, false}) {
    std::array<std::byte, 64> buffer{};
    obf2::net::BitWriter writer(buffer);
    writer.writeBits(0x9, 19);  // angular velocity and rotation
    if (physical) writer.writeBits(7, 3);  // the wide vector's level 7: the base itself
    quaternion(writer);
    obf2::net::BitReader reader(buffer);
    const auto state = obf2::net::bf2::readSimpleObjectState(reader, obf2::Vec3f{});
    CHECK(state.has_value());
    if (!state || !state->rotation) continue;
    CHECK(std::abs(state->rotationLength - 1.0f) < 0.001f);
    CHECK(std::abs((*state->rotation)[1] - 0.70710678f) < 0.001f);  // y
    CHECK(std::abs((*state->rotation)[3] - 0.70710678f) < 0.001f);  // w
    CHECK_EQ(reader.bitPosition(), writer.bitPosition());
    CHECK_EQ(state->angular.has_value(), physical);
  }
}

void testMapListSelectsCurrentEntry() {
  std::array<std::byte, 128> storage{};
  net::BitWriter writer(storage);
  const auto text = [&](std::string_view value) {
    writer.writeBits(static_cast<std::uint32_t>(value.size()), 16);
    for (const char c : value) writer.writeBits(static_cast<std::uint8_t>(c), 8);
  };
  writer.writeBits(2, 16);
  writer.writeBits(1, 16);
  text("previous_map");
  text("gpm_cq");
  writer.writeBits(32, 16);
  text("fushe_pass");
  text("gpm_coop");
  writer.writeBits(16, 16);

  const auto payload = std::span(storage).first(writer.byteSize());
  const auto list = net::bf2::parseMapList(payload);
  const auto* selected = list ? list->currentEntry() : nullptr;
  CHECK(selected != nullptr);
  if (selected) {
    CHECK_EQ(selected->levelName, "fushe_pass");
    CHECK_EQ(selected->gameMode, "gpm_coop");
    CHECK_EQ(selected->size, 16);
    CHECK_EQ(list->current, 1);
  }
  CHECK(!net::bf2::parseMapList(payload.first(payload.size() - 1)));
}

static void testTurretAnglesInGhostAndControlledState() {
  using namespace obf2::net;
  using namespace obf2::net::bf2;
  for (bool controlled : {false, true}) {
    std::array<std::byte, 32> data{};
    BitWriter writer(data);
    const unsigned width = controlled ? 16 : 12;
    const unsigned max = (1u << width) - 1;
    writer.writeBits(0xf, 4);
    writer.writeBits(1, 4);
    writer.writeBits(1, 4);
    writer.writeBits(0, 4);
    writer.writeBits(0, 1);
    writer.writeBits(0x9, 7);
    writer.writeBits(max * 7 / 12, width);
    writer.writeBits(0, width);  // yaw rate
    writer.writeBits(0, 7);     // static gunbase
    writer.writeBits(0x12, 7);
    writer.writeBits(max * 2 / 5, width);
    writer.writeBits(0, width);  // pitch rate
    if (controlled) {
      writer.writeBits(3, 10);
      writer.writeBits(0, 3);
    }
    const auto end = writer.bitPosition();
    const auto angles = readSeatRotations(data, 0, end, 3, controlled);
    CHECK(angles.has_value());
    if (!angles) continue;
    CHECK(std::abs((*angles)[0][0].value_or(0) - 30) < .1f);
    CHECK(!(*angles)[0][1]);
    CHECK((*angles)[0].normalizedRates[0] == -1.0f);
    CHECK(!(*angles)[1].normalizedRates[0]);
    CHECK((*angles)[2].normalizedRates[1] == -1.0f);
    CHECK(!(*angles)[1][0]);
    CHECK(std::abs((*angles)[2][1].value_or(0) + 36) < .1f);
    CHECK(!readSeatRotations(data, 0, end - 1, 3, controlled));
    CHECK(!readSeatRotations(data, 0, end + 1, 3, controlled));
  }
}

static void testFaavControlledWeaponSync() {
  using namespace obf2::net;
  using namespace obf2::net::bf2;
  const auto writeSlot = [](BitWriter& writer) {
    writer.writeBits(1, 1);   // present
    writer.writeBits(32, 6);  // fire mode 2
    writer.writeBits(0, 11);  // unlimited belt (-1)
    writer.writeBits(2, 10);  // one loaded belt
  };
  for (bool hitDirection : {false, true}) {
    std::array<std::byte, 32> data{};
    BitWriter writer(data);
    writer.writeBits(0, 4);  // no item changes
    writer.writeBits(0, 7);  // no yaw change
    writer.writeBits(0, 7);  // no pitch change
    writer.writeBits(hitDirection ? 132 : 124, 10);
    writer.writeBits(0, 12);
    writer.writeBits(hitDirection, 1);
    if (hitDirection) writer.writeBits(127, 8);
    writer.writeBits(0, 1);  // no camera shake
    writeSlot(writer);
    writer.writeBits(7, 8);  // heat after a burst
    writeSlot(writer);       // slot 1 has no heat byte
    writer.writeBits(0, 1);  // slot 2 absent
    writer.writeBits(0, 32);
    writer.writeBits(0, 13);
    std::optional<SeatWeaponState> ammo;
    const auto end = writer.bitPosition();
    CHECK(readSeatRotations(data, 0, end, 2, true, &ammo).has_value());
    CHECK(ammo && ammo->rounds == -1 && ammo->magazines == 1);
    CHECK(ammo && ammo->heat && std::abs(*ammo->heat - 7.0f / 255.0f) < .00001f);
    CHECK(!readSeatRotations(data, 0, end - 1, 2, true, &ammo));
    CHECK(!ammo);
  }
}

static void testT90TurretInRetailGhost() {
  using namespace obf2::net::bf2;
  const auto packets = obf2::net::bf2::loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-karkand-lives.bin");
  // The original client recorded a parked T-90 at object 1830 on Karkand.
  constexpr std::uint16_t tank = 1830;
  bool checked = false;
  for (const auto& packet : packets) {
    const auto control = readControlObjectState(packet);
    const auto reference = control ? control->compressionReference : obf2::Vec3f{};
    for (const auto& record : readGhostRecords(packet, reference, [=](std::uint16_t id) {
           return id == tank ? GhostClass::SimpleObject : GhostClass::Unknown;
         })) {
      if (record.networkId != tank || record.kind != 1 || record.stateMask != 0x5849bu) continue;
      const auto aim = readTankTurretRotations(packet, record, {1000, 1000});
      CHECK(aim && aim->size() == 2);
      if (aim && aim->size() == 2) {
        CHECK((*aim)[0][0] && *(*aim)[0][0] == 0.0f);
        CHECK((*aim)[1][1] && *(*aim)[1][1] == 0.0f);
      }
      checked = true;
    }
  }
  CHECK(checked);
}

// EngineNetworkable::setNetUpdate (Linux 0x5c9bd0) after a SimpleObject
// record with mask 0: gear, rpm (0x20), 0x80 in its dead band, the 0x200
// flags and the 0x400 block of a NewCar2 (type bit 7).
static void testEngineNetworkable() {
  using namespace obf2::net;
  using namespace obf2::net::bf2;
  std::array<std::byte, 32> data{};
  BitWriter writer(data);
  writer.writeBits(0, 1);  // stands for the record's SimpleObject part
  writer.writeBits(0x2 | 0x20 | 0x80 | 0x200 | 0x400, 11);
  writer.writeBits(2, 3);      // gear 2
  writer.writeBits(4095, 12);  // rpm 2·1 − 1 = 1, × 1.25
  writer.writeBits(2048, 12);  // 0x80: 2·2048/4095 − 1 ≈ 0.00024, in the dead band
  writer.writeBits(1, 1);      // 0x200: on
  writer.writeBits(1, 1);      //        negated
  writer.writeBits(0, 1);      // 0x400: sign
  writer.writeBits(65535, 16);
  writer.writeBits(51, 8);     // load 51/255
  writer.writeBits(1, 1);
  writer.writeBits(3, 2);
  GhostRecord record;
  record.kind = 1;
  record.readBits = 1;
  record.payloadBits = static_cast<std::uint32_t>(writer.bitPosition());
  DescriptorMember engine;
  engine.kind = DescriptorMember::Kind::Engine;
  engine.gearBits = 3;
  engine.engineBlock400 = true;
  const std::array members{engine};
  std::vector<EngineNetState> engines;
  CHECK(readTankTurretRotations(data, record, {1000, 1000}, 2, members, &engines).has_value());
  CHECK_EQ(engines.size(), std::size_t{1});
  if (engines.size() == 1) {
    const auto& e = engines[0];
    CHECK(e.gear && *e.gear == 2u);
    CHECK(e.rpm && std::abs(*e.rpm - 1.25f) < 1e-5f);
    CHECK(!e.field2dc);
    CHECK(e.flag200 && *e.flag200 == -1.0f);
    CHECK(e.load && std::abs(*e.load - 0.2f) < 1e-6f);
    CHECK(e.field400 && *e.field400 == 1.0f && !e.field400Negative);
    CHECK(e.value368 && *e.value368 == -3);
  }
  // One bit short: the cut fails and no engine state is kept.
  record.payloadBits -= 1;
  readTankTurretRotations(data, record, {1000, 1000}, 2, members, &engines);
  CHECK(engines.empty());
}

static void testControlledTankTurretAndAmmo() {
  using namespace obf2::net;
  using namespace obf2::net::bf2;
  for (unsigned bundles : {2u,3u}) {
  std::array<std::byte, 64> data{};
  BitWriter writer(data);
  writer.writeBits(0, 1);
  ControlObjectState control;
  control.stateStart = writer.bitPosition();
  writer.writeBits(0x10000, 19);
  writer.writeBits(9, 5);       // the final ranged field before the bundles
  writer.writeBits(0x01, 7);
  writer.writeBits(38228, 16);  // yaw about 30 degrees
  writer.writeBits(0x02, 7);
  writer.writeBits(33678, 16);  // pitch about 5 degrees
  if (bundles==3) {
    writer.writeBits(0x02,7); // independently networked HUD/barrel pitch
    writer.writeBits(34588,16); // about 10 degrees, not the preceding bundle
  }
  writer.writeBits(0, 28);      // remaining networkables before Player::getStateSync
  writer.writeBits(151, 10);
  writer.writeBits(0, 14);      // no optional hit direction or camera shake
  const auto slot = [&](int roundsPlusOne, int clipsPlusOne) {
    writer.writeBits(1, 1);
    writer.writeBits(0, 6);
    writer.writeBits(roundsPlusOne, 11);
    writer.writeBits(clipsPlusOne, 10);
  };
  slot(2, 41);
  slot(0, 0);
  writer.writeBits(25, 8);
  slot(3, 0);
  writer.writeBits(0, 32);
  writer.writeBits(0, 13);
  control.stateEnd = writer.bitPosition();
  std::optional<TankWeaponState> weapons;
  const auto aim = readTankTurretRotations(data, control, {1000, 1000}, &weapons,bundles);
  CHECK(aim && aim->size()==bundles);
  if (bundles==3) CHECK(aim && std::abs((*aim)[2][1].value_or(0)-10)<.02f);
  CHECK(aim && std::abs((*aim)[0][0].value_or(0) - 30.0f) < 0.01f);
  CHECK(aim && std::abs((*aim)[1][1].value_or(0) - 5.0f) < 0.01f);
  CHECK(weapons && weapons->cannon.rounds == 1 && weapons->cannon.magazines == 40);
  CHECK(weapons && !weapons->cannon.heat && weapons->coaxial.rounds == -1);
  CHECK(weapons && weapons->coaxial.heat && *weapons->coaxial.heat == 25.0f / 255.0f);
  --control.stateEnd;
  CHECK(readTankTurretRotations(data, control, {1000, 1000}, &weapons,bundles));
  CHECK(!weapons);
  }
}

TEST_MAIN({
  testRetailEndOfRoundInfo();
  testRetailServerMatchOptions();
  testServerExternalViews();
  testRetailServerAllowsExternalViews();
  testT90TurretInRetailGhost();
  testEngineNetworkable();
  testControlledTankTurretAndAmmo();
  testTurretAnglesInGhostAndControlledState();
  testFaavControlledWeaponSync();
  testRoundResetIsWalked();
  testMapListSelectsCurrentEntry();
  testSimpleObjectRotation();
  testKilledByEventIsWalked();
  testSquadEventsAndServerInfo();
  testKitAndPickupAreRead();
  testWorldCaptureIsFullyDecoded();
  testHeightsAreOnTheTerrain();
  testGhostStreamIsWalkable();
  testControlObjectStateNamesItsObject();
  testGhostRecordsCarryPositions();
  testControlObjectStateIsSkippable();
});
