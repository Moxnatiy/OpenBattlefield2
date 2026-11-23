#include "obf2/app/awards.h"

#include <cstdio>

#include "obf2/hud/manager.h"
#include "obf2/session/remote_world.h"

namespace obf2::app {

std::string medalIconPath(unsigned id, unsigned level) {
  std::string name = "AWARD_NAME_" + std::to_string(id);
  if (level > 0) name += "_" + std::to_string(level);
  return "Ingame/GeneralIcons/Medals/" + name + ".tga";
}

std::string rankIconPath(int rank) {
  char path[64];
  std::snprintf(path, sizeof path, "Ingame/GeneralIcons/Ranks/rank_%02i_Large.tga", rank);
  return path;
}

void takeAwards(session::RemoteWorld& remote, hud::Manager& hud) {
  for (const auto& notice : remote.awardNotices) {
    if (notice.rank) {
      hud.showAward(true, rankIconPath(notice.newRank), kPromotionSeconds);
    } else {
      hud.showAward(false, medalIconPath(notice.id, notice.level), kAwardSeconds);
    }
    std::printf("  award: %s %u level %u rank %d\n", notice.rank ? "promotion" : "medal",
                notice.id, notice.level, notice.newRank);
  }
  remote.awardNotices.clear();
}

}  // namespace obf2::app
