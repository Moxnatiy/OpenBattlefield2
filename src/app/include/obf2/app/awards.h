#pragma once
// Awards and promotions in play, as the game shows them (BF2.exe 0x759e50,
// HUD events 0x26 rank and 0x27 medal): the HUD's own MedalArea
// (HudElementsActionIcons.con:105, 465 20, awardWindow.tga 158x123, the icon
// 100x100 at 15 12) through hud::Manager::showAward.
//
// The icon paths: a promotion is Ingame/GeneralIcons/Ranks/rank_%02i_Large.tga
// (BF2.exe 0x926738); a medal is Ingame/GeneralIcons/Medals/AWARD_NAME_<id>.tga
// at level 0 and AWARD_NAME_<id>_<level>.tga at levels 1..3 — the HUD's own
// textures carry those names and the server sends (id, level) as
// stats/medal_data.py names its awards ('<id>_<level>'); the client's medal
// table (0x7b4600, filled by `addAward`) is not read.
//
// How long the window stays up is **not measured**: the time 0x759e50 hands
// 0x74f6b0 / 0x74f5b0 is not read. The stingers' lengths stand in —
// awardStinger is Common/Sound/Award.ogg, 3.804 s, and promotionStinger
// Common/Sound/Promotion.ogg, 3.808 s (Sound/GameSounds.con). The stingers
// themselves are not played, and the award's name is not put in the chat.
#include <string>

namespace obf2::session {
struct RemoteWorld;
}
namespace obf2::hud {
class Manager;
}

namespace obf2::app {

inline constexpr float kAwardSeconds = 3.804f;      // Award.ogg's length, not measured (above)
inline constexpr float kPromotionSeconds = 3.808f;  // Promotion.ogg's length, the same

std::string medalIconPath(unsigned id, unsigned level);
std::string rankIconPath(int rank);

// Takes the session's new awards (`RemoteWorld::awardNotices`, ours only) to
// the HUD's window.
void takeAwards(session::RemoteWorld& remote, hud::Manager& hud);

}  // namespace obf2::app
