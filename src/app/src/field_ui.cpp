#include "obf2/app/field_ui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

#include "obf2/con/interpreter.h"
#include "obf2/hud/manager.h"

namespace obf2::app {

std::optional<std::string> roseCommand(std::string_view retail) {
  // The console commands the rose tables run (HUD/HudSetup/Communication/*.con),
  // on our session's `communicate`. The ids are the tables' own.
  std::istringstream words{std::string(retail)};
  std::string verb, first, second;
  words >> verb >> first >> second;
  const auto lower = [](std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
  };
  verb = lower(verb);
  if (verb == "squadleader.sendorder") {
    // SquadLeader.con: 5 attack, 4 move, 8 destroy, 6 defend, 7 repair.
    static constexpr std::array<const char*, 9> names{"",       "",       "",       "",       "move",
                                                      "attack", "defend", "repair", "destroy"};
    const int id = std::atoi(first.c_str());
    if (id >= 4 && id <= 8) return std::string("order ") + names[static_cast<std::size_t>(id)];
    return std::nullopt;
  }
  if (verb == "squadleader.sendrequest") {
    // SquadLeader.con: 3 supplies, 0 artillery, 1 UAV, 2 vehicle.
    static constexpr std::array<const char*, 4> names{"artillery", "uav", "vehicle", "supplies"};
    const int id = std::atoi(first.c_str());
    if (id >= 0 && id <= 3) return std::string("request ") + names[static_cast<std::size_t>(id)];
    return std::nullopt;
  }
  if (verb == "radio.sendspottedmessage") return std::string("spot");
  if (verb == "radio.callforammo") return std::string("radio ammo");
  if (verb == "radio.callformedic") return std::string("radio medic");
  if (verb == "radio.callforrepair") return std::string("radio repair");
  if (verb == "commander.uav") return std::string("commander uav");
  if (verb == "commander.artillery") return std::string("commander artillery");
  if (verb == "commander.supplies") return std::string("commander supplies");
  if (verb == "commander.vehicledrop") return std::string("commander vehicle");
  if (verb == "game.radiomessage") {
    // The voice names of Radio.con and RadioVehicle.con onto the calls our
    // session knows; their channels are the session's.
    static constexpr std::array<std::pair<const char*, const char*>, 10> calls{{
        {"PLAYER_TACTICS_gogogo", "go"},
        {"req_pickup", "pickup"},
        {"negative", "negative"},
        {"PLAYER_TACTICS_followme", "follow"},
        {"roger_that", "roger"},
        {"backup", "backup"},
        {"PLAYER_CONFIRM_thankyou", "thanks"},
        {"PLAYER_CONFIRM_sorry", "sorry"},
        {"get_in", "get-in"},
        {"bail_out", "bail-out"},
    }};
    for (const auto& [voice, call] : calls) {
      if (second == voice) return std::string("radio ") + call;
    }
    // Squad.con's own calls, all on TCSquad.
    static constexpr std::array<std::pair<const char*, const char*>, 4> squad{{
        {"req_new_order", "squad-orders"},
        {"req_artillery", "squad-artillery"},
        {"PLAYER_REQUEST_requestsupplies", "squad-supplies"},
        {"PLAYER_REQUEST_requestvehicledrop", "squad-vehicle"},
    }};
    if (lower(first) == "tcsquad") {
      for (const auto& [voice, call] : squad) {
        if (second == voice) return std::string("radio ") + call;
      }
    }
  }
  // 0x7afe90: the radio message `req_new_order` on TCCommander.
  if (verb == "squadleader.sendrequestfororder") return std::string("radio new-order");
  return std::nullopt;
}

std::optional<RoseKind> roseFor(bool tacticalKey, bool squadLeader, bool commander, bool inSquad,
                                bool inVehicle) {
  if (!tacticalKey) return inVehicle ? RoseKind::RadioVehicle : RoseKind::Radio;
  // 0x758540: the leader's test comes first (state 7), then the commander's (8),
  // then a squad (3).
  if (squadLeader) return RoseKind::SquadLeader;
  if (commander) return RoseKind::Commander;
  if (inSquad) return RoseKind::Squad;
  return std::nullopt;
}

namespace {
// The layout and the table of each rose (HUD/HudSetup/Communication), and the
// console object its buttons call `selectOrder` on.
struct RoseFiles {
  const char* layout;
  const char* table;
  const char* object;
};
constexpr std::array<RoseFiles, kRoseKinds> kRoseFiles{{
    {"HudElementsRadioComm.con", "Radio.con", "RadioInterface"},
    {"HudElementsRadioVehicleComm.con", "RadioVehicle.con", "RadioVehicleInterface"},
    {"HudElementsSquadComm.con", "Squad.con", "SquadInterface"},
    {"HudElementsSquadLeaderComm.con", "SquadLeader.con", "SquadLeaderInterface"},
    {"HudElementsCommanderComm.con", "Commander.con", "CommanderInterface"},
}};

// `hudManager.setCommMousePos 400 300` (HUD/HudSetup/GeneralHudSettings.con,
// line 12): where the rose's pointer starts.
constexpr float kCommMouseX = 400.0f;
constexpr float kCommMouseY = 300.0f;
}  // namespace

void FieldUi::prepareRadio(FileSystem& files) {
  if (roseLoaded_) return;
  roseLoaded_ = true;
  const std::string folder = "menu/HUD/HudSetup/Communication/";
  for (int kind = 0; kind < kRoseKinds; ++kind) {
    const RoseFiles& source = kRoseFiles[static_cast<std::size_t>(kind)];
    hud::Builder builder;
    con::Interpreter layout(files, [&](const con::Command& c) { builder.feed(c); });
    layout.runFile(folder + source.layout);
    builder.finish();
    auto& slots = roseSlots_[static_cast<std::size_t>(kind)];
    // A button belongs to the rose when its command is `<Object>.selectOrder N`.
    const std::string call = std::string(source.object) + ".selectOrder %d";
    for (const auto& node : builder.nodes()) {
      if (node.type != hud::NodeType::Button) continue;
      int order = 0;
      if (std::sscanf(node.command.c_str(), call.c_str(), &order) == 1 && order > 0) {
        slots.push_back({node, order, {}});
      }
    }
    // Its caption is the text node named after it with `Label`.
    for (auto& slot : slots) {
      const auto label = std::ranges::find(builder.nodes(), slot.node.name + "Label", &hud::Node::name);
      if (label != builder.nodes().end()) slot.label = *label;
    }
    auto& rose = roses_[static_cast<std::size_t>(kind)];
    con::Interpreter table(files, [&](const con::Command& c) { rose.feed(c); });
    table.runFile(folder + source.table);
    std::printf("  rose: %s, %zu entries\n", source.object, slots.size());
  }
}

std::string FieldUi::roseLabel(const RoseSlot& slot) const {
  const auto entry = roses_[static_cast<std::size_t>(kind_)].entry(slot.order);
  if (!entry || entry->label.find("NOT USED") != std::string::npos) return {};
  return localize_ ? localize_(entry->label) : entry->label;
}

std::optional<std::string> FieldUi::radioRelative(bool held, float dx, float dy, bool click,
                                                 float scale, std::optional<RoseKind> kind) {
  const int before = selected_;
  const bool wasOpen = radioOpen_;
  const std::string path = roses_[static_cast<std::size_t>(kind_)].path();
  const auto finish = [&](std::optional<std::string> answer) {
    if (radioOpen_ != wasOpen || selected_ != before ||
        roses_[static_cast<std::size_t>(kind_)].path() != path)
      ++revision_;
    return answer;
  };
  if (held && !radioOpen_) {
    if (!kind) return {};
    kind_ = *kind;
    roses_[static_cast<std::size_t>(kind_)].reset();
    radioOpen_ = true;
    committed_ = false;
    selected_ = -1;
    roseX_ = kCommMouseX;
    roseY_ = kCommMouseY;
    return finish({});
  }
  if (!radioOpen_) return {};
  const auto& slots = roseSlots_[static_cast<std::size_t>(kind_)];
  if (slots.empty()) {
    if (!held) radioOpen_ = false;
    return finish({});
  }
  scale = std::max(scale, 0.001f);
  // The pointer moves in HUD units and stays within the buttons' mouse areas
  // (`setButtonNodeMouseArea`), so it never strays out of reach. Ours: how the
  // engine bounds it is not read (`hudManager.setCommPos 150 150` and
  // `setCommSize 490 300`, GeneralHudSettings.con lines 10–11, are the likely
  // bound), and neither is its sensitivity.
  float left = kCommMouseX, right = kCommMouseX, top = kCommMouseY, bottom = kCommMouseY;
  for (const auto& slot : slots) {
    const auto& n = slot.node;
    left = std::min(left, n.absX + n.mouseX);
    right = std::max(right, n.absX + n.mouseX + n.mouseWidth);
    top = std::min(top, n.absY + n.mouseY);
    bottom = std::max(bottom, n.absY + n.mouseY + n.mouseHeight);
  }
  roseX_ = std::clamp(roseX_ + dx / scale, left, right - 0.001f);
  roseY_ = std::clamp(roseY_ + dy / scale, top, bottom - 0.001f);
  // The entry under the pointer: the first button whose mouse area holds it.
  selected_ = -1;
  for (const auto& slot : slots) {
    const auto& n = slot.node;
    if (roseX_ < n.absX + n.mouseX || roseX_ >= n.absX + n.mouseX + n.mouseWidth ||
        roseY_ < n.absY + n.mouseY || roseY_ >= n.absY + n.mouseY + n.mouseHeight)
      continue;
    if (!roseLabel(slot).empty()) selected_ = slot.order;
    break;
  }
  std::optional<std::string> command;
  // A release over an entry chooses it, as the button's `selectOrder` does; an
  // entry with a level under it opens that level and the rose stays.
  if (!committed_ && selected_ > 0 && (click || !held)) {
    const auto selection = roses_[static_cast<std::size_t>(kind_)].selectOrder(selected_);
    if (selection.command && !selection.command->empty()) command = *selection.command;
    committed_ = !selection.descended;
  }
  if (!held) radioOpen_ = false;
  return finish(command);
}

std::vector<hud::DrawPiece> FieldUi::build(hud::Manager& hud) {
  std::vector<hud::DrawPiece> pieces;
  drawn_.clear();
  if (!radioOpen_) return pieces;
  const auto& slots = roseSlots_[static_cast<std::size_t>(kind_)];
  // Every node is kept before any is built: the pieces point at them.
  drawn_.reserve(slots.size() * 2);
  for (const auto& slot : slots) {
    const std::string label = roseLabel(slot);
    hud::Node button = slot.node;
    // `setButtonNodeTexture 2` under the pointer, 1 otherwise.
    if (!label.empty() && slot.order == selected_ && !button.hoverTexture.empty())
      button.texture = button.hoverTexture;
    button.text.clear();
    drawn_.push_back(std::move(button));
    if (label.empty() || !slot.label) continue;
    hud::Node caption = *slot.label;
    caption.text = label;
    caption.textVariable.clear();
    drawn_.push_back(std::move(caption));
  }
  for (const hud::Node& node : drawn_) {
    for (auto& piece : hud.buildLoose(node)) pieces.push_back(std::move(piece));
  }
  return pieces;
}

}  // namespace obf2::app
