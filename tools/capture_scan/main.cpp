// capture_scan — walks a `--record` capture packet by packet and shows where the
// walk goes astray.
//
//   capture_scan <capture.bin> [--all]
//
// A capture is what `openbf2 --connect … --record <file>` writes: for each data
// packet a 32-bit length and the bytes. For every packet the tool reads the
// events and the ghost stream's header, the way the session does, and flags a
// packet whose header time jumps by more than a minute of ticks from the last
// good one, or whose controlled-object state names object 0 — the two marks of a
// walk that lost its place among the events. A flagged packet is printed with
// every event type in it, in order, so the event whose length is wrong is in
// front of you. `--all` prints every packet.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    std::fprintf(stderr, "usage: capture_scan <capture.bin> [--all]\n");
    return 1;
  }
  const bool all = argc > 2 && std::strcmp(argv[2], "--all") == 0;
  const auto packets = loadCapture(argv[1]);
  std::printf("%zu packets\n", packets.size());

  // One minute of server ticks (30 a second, `WorldPref::mTickTime`).
  constexpr std::int64_t kJump = 30 * 60;
  std::int64_t lastTime = -1;
  int flagged = 0;
  for (std::size_t i = 0; i < packets.size(); ++i) {
    const auto& packet = packets[i];
    const auto events = obf2::net::bf2::readEvents(packet);
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
    std::printf("  events %zu:", events.size());
    for (const auto& event : events) std::printf(" %u", event.type);
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
