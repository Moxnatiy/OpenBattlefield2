#pragma once

// The in-game help box at the top of the screen (`IngameHelpShow`,
// `IngameHelpString`; HUD/HudSetup/HudElementsHelp.con).
// docs/functions/hud-help-messages.md.

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace obf2::hud {

// Message ids of the help table (`BF2.exe` 0x5ed220). Each id has one string
// slot at +0x200 + id * 0x1c. Only the ones we raise are named.
enum class HelpMessage : int {
  EnterVehicle = 0x43,  // HUD_HELP_VEHICLE_GENERAL_CONTROLS_enterVehicle, slot +0x954
  ExitVehicle = 0x44,   // HUD_HELP_VEHICLE_CONTROLS_exitVehicle, slot +0x970
};

// The table's size: ids above 0x51 are refused (0x5ede47), and every per-message
// array of the help class holds 0x52 entries (0x5ed258, 0x5f155f).
inline constexpr int kHelpMessageCount = 0x52;

// The localisation key of a message (`BF2.exe` 0x5ed7ce, 0x5ed7de).
std::string_view helpMessageKey(HelpMessage message);

// The voice-over name a case hands to 0x5ed1e0 (`enter_Vehicle_help`
// 0x8efd84, pushed at 0x5efd0d; `exit_Vehicle_help` 0x8efd70, 0x5efde3).
// 0x5ed1e0 passes it to GameLogic's VoiceMessages (0x4bdbb0) and
// playHelpMessage (0x4f8110), which looks it up after 0x431450 (a lower-case
// copy: the data writes `exit_vehicle_help`, Sound/VoiceMessages_Help.con).
std::string_view helpVoiceName(HelpMessage message);

// How long the box stays: 9.0 s, the 0x41100000 both vehicle cases push
// (`BF2.exe` 0x5efd26, 0x5efdfd) into showHelp.
inline constexpr float kHelpMessageSeconds = 9.0f;

// The `#Xname#` markers of a help text become the bound key's name
// (`BF2.exe` 0x752490): `P` takes the action from
// `defaultPlayerInputControlMap`, `G` from `defaultGameControlMap`, and the
// action is `c_` + X + name (`#PIUse#` -> `c_PIUse`). `keyName` answers for one
// action ("" when unbound); a marker it cannot answer is left as it is.
std::string expandHelpKeys(std::string_view text,
                           const std::function<std::string(std::string_view action)>& keyName);

// What the per-message cases of the dispatcher 0x5ede00 test, worked out by
// the caller for this frame.
struct HelpConditions {
  // 0x43, case 0x5efc64: not ported (docs/functions/hud-help-messages.md).
  bool enterVehicle = false;
  // 0x44, case 0x5efd74: the controlled object (player vtable +0x44) is not
  // the soldier (+0xa0); player vtable +0x68 is true; the controlled object's
  // template class is not LadderContainer (`[0x91c3a4]` = 0x9c46, the Linux
  // server's `CID_LadderContainerTemplate`); and its VehicleHud (+0x54), if it
  // has one, has `standardHelpEnabled` (byte +0x96 of vtable +0x64).
  bool exitVehicle = false;
};

// The scheduler of the help class, 0x5f1460, run every frame for the local
// player (0x50537b, Player +0x2a4). tick() answers the message to show now.
class HelpScheduler {
 public:
  // +0x1fc: nothing runs while it is 0 (0x5f1466). Cleared by the
  // constructor (0x5ed244), set by Player::enableVOHelp (0x4f9f00, Player
  // vtable 0x8ac4d8 +0x268), whose caller is not traced; the game data turns
  // the game-wide switch on (`gameLogic.enableVoHelp 1`, GameLogicInit.con).
  void setEnabled(bool on) { enabled_ = on; }
  // +0x0[id]: the message has been shown and is skipped for good (0x5f153e).
  // Set when it shows (0x5edb20) and, at start, for every key the profile
  // lists with `GeneralSettings.setPlayedVOHelp` (0x5edb80, GeneralSettings
  // vtable +0x118; the command 0x414d6d adds through +0x114).
  void markPlayed(HelpMessage message);
  bool played(HelpMessage message) const;
  // `dt` is the frame time. 0x5f14cf..0x5f152f: after a message has shown
  // (+0x1ec), none shows until more than 10.0 s (+0x1f0, 0x5ed230) have
  // passed (+0x1f4). Then the ids are walked in order (0x5f1537..0x5f1562),
  // skipping the played ones and those suppressed (+0x52, Player
  // disableVOHelpMessage 0x4f9f20 / enableVOHelpMessage 0x4f9f40, unused
  // here); each id's condition is looked at only when its repeat timer
  // (0x5ed150) fires. The first that holds wins: it is marked played and the
  // 10 s wait begins (0x5f156b..0x5f1585).
  std::optional<HelpMessage> tick(float dt, const HelpConditions& conditions);

 private:
  bool due(int id, float dt);

  // {interval, remaining} per id, both 1/30 s (0x3d088889) at start
  // (0x5ed270..0x5ed293). 0x5ed150: remaining <= 0 reloads it and fires;
  // otherwise it loses dt.
  static constexpr float kRepeatInterval = 0.0333333351f;
  bool enabled_ = true;
  std::array<bool, kHelpMessageCount> played_{};
  std::array<float, kHelpMessageCount> remaining_ = [] {
    std::array<float, kHelpMessageCount> all{};
    all.fill(kRepeatInterval);
    return all;
  }();
  bool recent_ = false;     // +0x1ec
  float sinceShown_ = 0.0f;  // +0x1f4
};

// The box: show(text, seconds) is HudManager::showHelp (`BF2.exe` 0x74f7f0),
// tick(dt) the countdown of 0x755fd0.
class HelpBox {
 public:
  // An empty text or a time of 0.0 or less hides the box (0x74f7fb, 0x74f804
  // against 0.0 at 0x93c850). Otherwise the string is set, the box shown and
  // the countdown (+0x22c) set to `seconds`. Not ported: the extra check of
  // 0x99e348 vtable +0xdc (0x74f821), purpose not established.
  void show(std::string text, float seconds);
  // While shown, the countdown loses dt; below zero the box hides (0x755fd0,
  // `IngameHelpShow` read through layer vtable +0x10, hidden through +0xc).
  void tick(float dt);

  bool shown() const { return shown_; }
  const std::string& text() const { return text_; }

 private:
  bool shown_ = false;
  std::string text_;
  float remaining_ = 0.0f;
};

}  // namespace obf2::hud
