// capture_scan — walks a `--record` capture packet by packet and shows where the
// walk goes astray.
//
//   capture_scan <capture.bin> [--all | --remote | --ghosts]
//
// A capture is what `openbf2 --connect … --record <file>` writes: for each data
// packet a 32-bit length and the bytes. For every packet the tool reads the
// events and the ghost stream's header, the way the session does, and flags a
// packet whose header time jumps by more than a minute of ticks from the last
// good one, or whose controlled-object state names object 0 — the two marks of a
// walk that lost its place among the events. A flagged packet is printed with
// every event type in it, in order, so the event whose length is wrong is in
// front of you. `--all` prints every packet; `--remote` lists every remote event
// (category, number, value) with the packet's ghost flag. `--ghosts` lists every
// network id the ghost stream named, low to high: how many records of each kind,
// the class the walk gave it (by its create event or its full mask — which the
// tool, having no events' world, only has from the mask and the fixed score
// manager id), the shortest and longest content, and the first content's mask —
// the way to tell a family of static networkables (the ids below 642,
// `NetworkManager::regressNetworkableIds`) apart.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "obf2/net/bf2_events.h"

namespace {

std::vector<std::vector<std::byte>> loadCapture(const char* path) {
  std::vector<std::vector<std::byte>> packets;
  std::FILE* file = std::fopen(path, "rb");
  if (!file) return packets;
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: capture_scan <capture.bin> [--all | --remote | --ghosts]\n");
    return 1;
  }
  const bool all = argc > 2 && std::strcmp(argv[2], "--all") == 0;
  const bool remote = argc > 2 && std::strcmp(argv[2], "--remote") == 0;
  const bool ghosts = argc > 2 && std::strcmp(argv[2], "--ghosts") == 0;
  const auto packets = loadCapture(argv[1]);
  std::printf("%zu packets\n", packets.size());

  if (ghosts) {
    struct Seen {
      int kinds[4] = {};
      std::uint32_t shortest = UINT32_MAX, longest = 0;
      std::uint32_t firstMask = 0;
      bool haveMask = false;
      obf2::net::bf2::GhostClass netClass = obf2::net::bf2::GhostClass::Unknown;
    };
    std::map<std::uint16_t, Seen> seen;
    for (const auto& packet : packets) {
      for (const auto& record : obf2::net::bf2::readGhostRecords(packet)) {
        Seen& s = seen[record.networkId];
        if (record.kind < 4) ++s.kinds[record.kind];
        if (record.kind != 1) continue;
        if (record.netClass != obf2::net::bf2::GhostClass::Unknown) s.netClass = record.netClass;
        if (record.payloadBits < s.shortest) s.shortest = record.payloadBits;
        if (record.payloadBits > s.longest) s.longest = record.payloadBits;
        if (!s.haveMask) {
          // The content's first 32 bits as they lie, whatever the class.
          obf2::net::BitReader peek(packet);
          if (peek.skipBits(record.payloadStart)) {
            const unsigned width = record.payloadBits < 32 ? record.payloadBits : 32;
            if (const auto bits = peek.readBits(width)) {
              s.firstMask = *bits;
              s.haveMask = true;
            }
          }
        }
      }
    }
    static const char* const kClass[] = {"?", "object", "soldier", "player", "spawn group",
                                         "score", "control point"};
    for (const auto& [id, s] : seen) {
      std::printf("id %5u%s: kind0 %d kind1 %d kind3 %d, class %s, content %u..%u bits, first "
                  "bits 0x%08x\n",
                  id, id < 642 ? " (static)" : "", s.kinds[0], s.kinds[1], s.kinds[3],
                  kClass[static_cast<int>(s.netClass)], s.shortest == UINT32_MAX ? 0 : s.shortest,
                  s.longest, s.firstMask);
    }
    return 0;
  }

  // `--remote`: every `PostRemoteEvent` (type 11) in order, with its category and
  // the packet's ghost flag. The join and the round's resets are told by these:
  // category 6 is the connection's (docs/functions/network-events.md), category 0
  // the game logic's (status 1, reset 6).
  if (remote) {
    int set = 0;
    int clear = 0;
    for (std::size_t i = 0; i < packets.size(); ++i) {
      const auto flag = obf2::net::bf2::ghostFlag(packets[i]);
      if (flag) (*flag ? set : clear)++;
      for (const auto& event : obf2::net::bf2::readEvents(packets[i])) {
        if (!event.remote) continue;
        std::printf("packet %zu (ghost flag %s, so far set %d, clear %d): category %u, event %u",
                    i, flag ? (*flag ? "set" : "clear") : "?", set, clear, event.remote->category,
                    event.remote->number);
        if (event.remote->value) std::printf(" = %d", *event.remote->value);
        std::printf("\n");
      }
    }
    std::printf("ghost flag: set %d, clear %d\n", set, clear);
    return 0;
  }

  // One minute of server ticks (30 a second, `WorldPref::mTickTime`).
  constexpr std::int64_t kJump = 30 * 60;
  std::int64_t lastTime = -1;
  int flagged = 0;
  for (std::size_t i = 0; i < packets.size(); ++i) {
    const auto& packet = packets[i];
    obf2::net::bf2::EventWalk walk;
    const auto events = obf2::net::bf2::readEvents(packet, &walk);
    const auto header = obf2::net::bf2::readGhostHeader(packet);
    const auto control = obf2::net::bf2::readControlObjectState(packet);

    bool bad = false;
    std::string why;
    if (header) {
      const auto time = static_cast<std::int64_t>(header->time);
      if (lastTime >= 0 && (time - lastTime > kJump || lastTime - time > kJump)) {
        bad = true;
        why += " time jumps " + std::to_string(lastTime) + " -> " + std::to_string(time) + ";";
      } else {
        lastTime = time;
      }
    }
    if (control && control->networkId == 0) {
      bad = true;
      why += " controlled object 0 (counter " + std::to_string(control->counter) + ");";
    }
    if (!bad && !all) continue;
    if (bad) ++flagged;
    std::printf("packet %zu, %zu bytes%s%s\n", i, packet.size(), bad ? " — ASTRAY:" : "",
                why.c_str());
    std::printf("  events %zu of %u:", events.size(), walk.declared);
    for (const auto& event : events) std::printf(" %u", event.type);
    if (walk.brokeOn) std::printf("  — BROKE on type %u", *walk.brokeOn);
    std::printf("\n");
    if (header) {
      std::printf("  ghosts: time %u, records %u, controlled state %d\n", header->time,
                  header->records, header->controlObjectState ? 1 : 0);
    } else {
      std::printf("  ghosts: no header (the events could not all be walked, or none)\n");
    }
  }
  std::printf("%d packets astray\n", flagged);
  return flagged > 0 ? 2 : 0;
}
