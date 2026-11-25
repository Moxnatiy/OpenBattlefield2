#include "obf2/hud/strategic_objects.h"

#include <map>
#include <utility>

namespace obf2::hud {

std::vector<StrategicHolder> groupStrategicObjects(std::span<const StrategicCandidate> candidates) {
  std::vector<StrategicHolder> holders;
  std::map<std::pair<std::string, int>, std::size_t> byKey;
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const StrategicCandidate& object = candidates[i];
    int index = 0;
    bool joined = false;
    for (;; ++index) {
      const auto found = byKey.find({object.name, index});
      if (found == byKey.end()) break;
      if (object.onePiece) continue;
      StrategicHolder& holder = holders[found->second];
      if (!holder.members.empty()) {
        const StrategicCandidate& first = candidates[holder.members.front()];
        // The sum as the engine adds it (0x482e00): r_new + r_first + r_new + r_first.
        const float reach = object.radius + first.radius + object.radius + first.radius;
        if (reach <= length(object.position - first.position)) continue;
      }
      holder.members.push_back(i);
      joined = true;
      break;
    }
    if (!joined) {
      byKey.emplace(std::pair{object.name, index}, holders.size());
      holders.push_back(StrategicHolder{object.name, index, {i}, {}});
    }
  }
  for (StrategicHolder& holder : holders) {
    Vec3f sum;
    for (const std::size_t member : holder.members) sum = sum + candidates[member].position;
    holder.position = sum * (1.0f / static_cast<float>(holder.members.size()));
  }
  return holders;
}

}  // namespace obf2::hud
