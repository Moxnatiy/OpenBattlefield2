#include "obf2/hud/help_messages.h"

namespace obf2::hud {

std::string_view helpMessageKey(HelpMessage message) {
  switch (message) {
    case HelpMessage::EnterVehicle: return "HUD_HELP_VEHICLE_GENERAL_CONTROLS_enterVehicle";
    case HelpMessage::ExitVehicle: return "HUD_HELP_VEHICLE_CONTROLS_exitVehicle";
  }
  return {};
}

std::string_view helpVoiceName(HelpMessage message) {
  switch (message) {
    case HelpMessage::EnterVehicle: return "enter_Vehicle_help";
    case HelpMessage::ExitVehicle: return "exit_Vehicle_help";
  }
  return {};
}

std::string expandHelpKeys(std::string_view text,
                           const std::function<std::string(std::string_view action)>& keyName) {
  std::string out;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t open = text.find('#', at);
    if (open == std::string_view::npos) break;
    const std::size_t close = text.find('#', open + 1);
    if (close == std::string_view::npos) break;
    out.append(text.substr(at, open - at));
    const std::string_view marker = text.substr(open + 1, close - open - 1);
    std::string name;
    if (!marker.empty() && (marker.front() == 'P' || marker.front() == 'G')) {
      name = keyName("c_" + std::string(marker));
    }
    if (name.empty()) {
      out.append(text.substr(open, close - open + 1));
    } else {
      out += name;
    }
    at = close + 1;
  }
  out.append(text.substr(at));
  return out;
}

void HelpScheduler::markPlayed(HelpMessage message) {
  played_[static_cast<int>(message)] = true;
}

bool HelpScheduler::played(HelpMessage message) const {
  return played_[static_cast<int>(message)];
}

bool HelpScheduler::due(int id, float dt) {
  if (remaining_[id] <= 0.0f) {
    remaining_[id] = kRepeatInterval;
    return true;
  }
  remaining_[id] -= dt;
  return false;
}

std::optional<HelpMessage> HelpScheduler::tick(float dt, const HelpConditions& conditions) {
  if (!enabled_) return std::nullopt;
  sinceShown_ += dt;
  if (recent_) {
    if (!(sinceShown_ > 10.0f)) return std::nullopt;
    recent_ = false;
    sinceShown_ = 0.0f;
  }
  for (int id = 0; id < kHelpMessageCount; ++id) {
    if (played_[id] || !due(id, dt)) continue;
    bool holds = false;
    switch (static_cast<HelpMessage>(id)) {
      case HelpMessage::EnterVehicle: holds = conditions.enterVehicle; break;
      case HelpMessage::ExitVehicle: holds = conditions.exitVehicle; break;
    }
    if (!holds) continue;
    played_[id] = true;
    recent_ = true;
    sinceShown_ = 0.0f;
    return static_cast<HelpMessage>(id);
  }
  return std::nullopt;
}

void HelpBox::show(std::string text, float seconds) {
  if (text.empty() || !(seconds > 0.0f)) {
    shown_ = false;
    return;
  }
  text_ = std::move(text);
  shown_ = true;
  remaining_ = seconds;
}

void HelpBox::tick(float dt) {
  if (!shown_) return;
  remaining_ -= dt;
  if (remaining_ < 0.0f) shown_ = false;
}

}  // namespace obf2::hud
