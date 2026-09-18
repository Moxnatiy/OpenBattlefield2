#include "obf2/app/calibrate.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "obf2/level/level.h"
#include "obf2/net/bf2_events.h"
#include "obf2/session/remote_world.h"

namespace obf2::app {

using session::KnownObject;
using session::buildKnownObjects;
using session::buildRegistry;
using session::nearestKnown;

// Matches template numbers to names.
//
// The server sends objects by template number, and the number is the order of
// creation (`ObjectTemplateManager::createTemplate`), so the name cannot be got
// from the number itself. But the positions match: we read the level ourselves and
// know what stands where, and the server gives numbers for those same places.
//
// The packet file is made by `tools/linuxded/capture.py --stage world --out`.
int runCalibrate(const Args& args, FileSystem& files) {
  if (args.levelName.empty()) {
    std::fprintf(stderr, "give a level: --level <name>\n");
    return 1;
  }
  std::string error;
  if (!level::mountLevel(files, args.modDir, args.levelName, &error)) {
    std::fprintf(stderr, "the level was not mounted: %s\n", error.c_str());
    return 1;
  }

  auto known = buildKnownObjects(files, args.levelName, &error);
  std::printf("level %s: known objects %zu\n", args.levelName.c_str(), known.size());

  const auto packets = net::bf2::loadCapture(args.calibrate);
  if (packets.empty()) {
    std::fprintf(stderr, "%s holds no packets\n", args.calibrate.c_str());
    return 1;
  }

  std::map<std::uint32_t, std::string> mapping;
  std::map<std::uint32_t, Vec3f> unmatched;
  int fromServer = 0, matched = 0;
  for (const auto& packet : packets) {
    for (const auto& event : net::bf2::readEvents(packet)) {
      if (!event.object || !event.object->position) continue;
      ++fromServer;
      const auto& at = *event.object->position;

      const KnownObject* best = nearestKnown(known, at);
      if (best) {
        ++matched;
        mapping[event.object->templateId] = best->name;
      } else {
        unmatched[event.object->templateId] = at;
      }
    }
  }

  std::printf("objects from the server: %d, matched: %d\n", fromServer, matched);
  for (const auto& [id, name] : mapping) {
    std::printf("  %6u  %s\n", id, name.c_str());
  }

  // A check of the guess about where the numbers come from.
  //
  // The server sends a template number rather than a name, and passes no list
  // block at all (the conversation holds only three: 0, 2 and 5). The engine takes
  // the template from `ObjectTemplateManager::getTemplate(unsigned)` — an ordinary
  // `std::map` by number (Linux server, 0x6a1110), so the numbers are handed out
  // somewhere during loading and have to match on both sides.
  //
  // The simplest assumption: the number is the order of a template's creation. It
  // is checked rather than taken on trust: we have "number -> name" pairs won by
  // matching on position, and our own list in creation order. If the assumption is
  // wrong, that is visible right here.
  {
    game::Registry registry = buildRegistry(files);
    const std::vector<const game::ObjectTemplate*> ordered = registry.all();
    int hit = 0, miss = 0;
    for (const auto& [id, name] : mapping) {
      if (id >= ordered.size()) { ++miss; continue; }
      if (ordered[id]->name == name) ++hit; else ++miss;
    }
    std::printf("number = creation order? matched %d, not %d (our templates %zu)\n",
                hit, miss, ordered.size());

    // If the numbers do not match, the question is whether at least the **order**
    // matches: then the difference is only that we count something extra or fail to
    // load something, while the idea "the number grows with creation order" is
    // right.
    std::map<std::string, std::size_t> indexByName;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
      indexByName.emplace(ordered[i]->name, i);
    }
    std::printf("order: the server's number -> ours\n");
    long long previous = -1;
    int rising = 0, falling = 0, absent = 0;
    for (const auto& [id, name] : mapping) {
      const auto found = indexByName.find(name);
      if (found == indexByName.end()) {
        std::printf("  %6u  -> not in our list  %s\n", id, name.c_str());
        ++absent;
        continue;
      }
      const auto ours = static_cast<long long>(found->second);
      std::printf("  %6u  -> %6lld  %s\n", id, ours, name.c_str());
      if (previous >= 0) (ours > previous ? rising : falling)++;
      previous = ours;
    }
    std::printf("the order matches: %d times, violated: %d, absent here: %d\n", rising, falling,
                absent);
  }
  if (!unmatched.empty()) {
    std::printf("%zu numbers not recognised:\n", unmatched.size());
    for (const auto& [id, at] : unmatched) {
      std::printf("  %6u  @ %.1f %.1f %.1f\n", id, at.x, at.y, at.z);
    }
  }
  return 0;
}


}  // namespace obf2::app
