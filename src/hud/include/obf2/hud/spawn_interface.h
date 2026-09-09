#pragma once
// The spawn screen: the selection state and the console commands that change it.
//
// The name is not ours: in the original this is `Code/BF2/Menu/Hud/SpawnInterface.cpp`
// (the path is visible in `BF2_r.exe`), and our module keeps the same split.
//
// This screen's buttons have no logic of their own — each runs a console command
// from `setButtonNodeConCmd` (docs/functions/hud-commands.md). So the whole
// screen comes down to a few values and the handlers that change them, and that
// can be checked by a test without a window.
#include <functional>
#include <string>
#include <vector>

#include "obf2/engine/console.h"

namespace obf2::hud {

// What the player chose. The team is assigned by the server, not by the player:
// in the captured traffic the original client does not even send `NESelectTeam` —
// it accepts the one the server gave in `CreatePlayerEvent`.
struct SpawnChoice {
  int team = 1;
  int kit = 0;
  // The circle's index in the list of spawn points, not the point's id.
  int marker = 0;
  bool membersTab = false;
};

class SpawnInterface {
 public:
  // Registers the spawn screen's command handlers. `requestSpawn` is called with
  // (team, kit, control point id) and returns whether the request really went to
  // the server: if not, the screen stays where it is. Otherwise the result was a
  // frozen picture with no player.
  void bind(engine::Console& console, std::function<bool(int, int, int)> requestSpawn);

  const SpawnChoice& choice() const { return choice_; }
  // While the spawn screen is still half in `main.cpp`, it needs direct access.
  // This should disappear along with the rest of the move.
  SpawnChoice& mutableChoice() { return choice_; }
  void markDirty() { dirty_ = true; }
  bool requested() const { return requested_; }
  void setRequested(bool value) { requested_ = value; }
  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }

  // The team is set by the server; on a change the point choice is reset, because
  // the circles belong to your own team's flags.
  void setTeamFromServer(int team);
  void setMarkerPoints(std::vector<int> points) { markerPoints_ = std::move(points); }
  const std::vector<int>& markerPoints() const { return markerPoints_; }

  // The id of the control point under the chosen circle; zero means not chosen.
  int chosenPoint() const;

  // The player spawned or died — the screen opens again.
  void reset() { requested_ = false; }

 private:
  SpawnChoice choice_;
  std::vector<int> markerPoints_;
  std::function<bool(int, int, int)> requestSpawn_;
  bool requested_ = false;
  bool dirty_ = false;
};

}  // namespace obf2::hud
