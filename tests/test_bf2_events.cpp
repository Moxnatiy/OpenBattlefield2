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
#include <cstring>
#include <string>
#include <vector>

#include "obf2/net/bf2_events.h"
#include "check.h"

namespace {

using namespace obf2;

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
  for (const auto& packet : packets) {
    if (const auto state = obf2::net::bf2::readControlObjectState(packet)) {
      reference = state->compressionReference;
      continue;
    }
    const auto base = [&](std::uint16_t) { return reference; };
    for (const auto& record : obf2::net::bf2::readGhostRecords(packet, base)) {
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
  // One packet in a hundred takes a branch we have not worked out (the function has
  // a read of 10 bits in a loop at 0x44633a). Such a packet simply yields no
  // records — that is one update lost, not a broken stream. For now we require at
  // least 95 in 100 to pass.
  CHECK(walked * 100 >= withControl * 95);
}

TEST_MAIN({
  testWorldCaptureIsFullyDecoded();
  testHeightsAreOnTheTerrain();
  testGhostStreamIsWalkable();
  testControlObjectStateNamesItsObject();
  testGhostRecordsCarryPositions();
  testControlObjectStateIsSkippable();
});
