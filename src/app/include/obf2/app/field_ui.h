#pragma once
// The communication roses on screen (docs/functions/command-rose.md): which
// rose a key opens, the pointer over it, the entry chosen on release, and the
// table's console command routed onto the session's `communicate`.
//
// The layouts and the tables are the game's own
// (`Menu_client.zip`, `HUD/HudSetup/Communication/`): the buttons and their
// labels from `HudElements<Name>Comm.con`, the entries from `<Name>.con`
// (`hud::CommInterface`). The rose builds its pieces through the HUD
// (`hud::Manager::buildLoose`) and the session draws them.
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/hud/comm_interface.h"
#include "obf2/hud/hud.h"
#include "obf2/hud/render.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::hud {
class Manager;
}

namespace obf2::app {

// The five roses of HUD/HudSetup/Communication, in the HUD states that show
// them (docs/functions/hud-states.md): 4 radio, 5 radio in a vehicle, 3 squad
// member, 7 squad leader, 8 commander.
enum class RoseKind { Radio, RadioVehicle, Squad, SquadLeader, Commander };
inline constexpr int kRoseKinds = 5;

// A rose table's console command (`SquadLeader.sendOrder 5 0`) as our session's
// communication line (`order attack`); nullopt when it is not routed yet.
std::optional<std::string> roseCommand(std::string_view retail);

// Which rose a key opens, as 0x758540 (T, `c_GITacticalComm`, manager slot 0x208)
// and slot 0x204 (Q, `c_GIRadioComm`) choose: T opens the squad leader's when he
// leads (state 7), the commander's when he commands (8), the squad member's when
// he is in a squad (3), and nothing otherwise; Q the radio, in a vehicle its own.
std::optional<RoseKind> roseFor(bool tacticalKey, bool squadLeader, bool commander, bool inSquad,
                                bool inVehicle);

class FieldUi {
 public:
  // Reads the five layouts and tables once.
  void prepareRadio(FileSystem& files);
  // The rose's labels are localisation keys (`HUD_COMROSE_SL_attack`).
  void setLocalize(std::function<std::string(std::string_view)> localize) {
    localize_ = std::move(localize);
  }
  // One frame of the rose: `held` is the key, (dx, dy) the mouse's move in
  // pixels, `pixelsPerHudUnit` the screen's scale over 800x600. `kind` is the
  // rose the key opens, taken when it opens. The answer is the chosen entry's
  // console command (the game's own, from the table).
  std::optional<std::string> radioRelative(bool held, float dx, float dy, bool click,
                                          float pixelsPerHudUnit, std::optional<RoseKind> kind);
  int radioSelection() const { return selected_; }
  bool radioOpen() const { return radioOpen_; }
  RoseKind kind() const { return kind_; }
  // Changes whenever what the rose shows does (opened, closed, another entry lit).
  int revision() const { return revision_; }
  // The rose as it stands, through the HUD's fonts; empty while it is closed.
  // The pieces point at nodes this object keeps until the next build.
  std::vector<hud::DrawPiece> build(hud::Manager& hud);

 private:
  struct RoseSlot {
    hud::Node node;
    int order = 0;
    std::optional<hud::Node> label;
  };
  std::string roseLabel(const RoseSlot& slot) const;

  std::array<std::vector<RoseSlot>, kRoseKinds> roseSlots_;
  std::array<hud::CommInterface, kRoseKinds> roses_;
  std::vector<hud::Node> drawn_;
  RoseKind kind_ = RoseKind::Radio;
  std::function<std::string(std::string_view)> localize_;
  bool roseLoaded_ = false;
  float roseX_ = 400.0f, roseY_ = 300.0f;
  bool radioOpen_ = false;
  bool committed_ = false;
  int selected_ = -1;
  int revision_ = 0;
};

}  // namespace obf2::app
