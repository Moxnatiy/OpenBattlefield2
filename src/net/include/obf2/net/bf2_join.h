#pragma once
// The order in which the client drives its conversation with an original BF2
// server up to the player spawning.
//
// This is pure logic with no socket: it only says **what** to send next and
// **when** the time has come. Packets are assembled and sent by whoever uses it.
// That way it can be checked by a test without bringing a server up — and there
// is plenty to check, because every rule here was won by measurement against a
// live server and cost us more than one wrong conclusion.
//
// The rules, and where they came from:
//
//   * **the pause between steps.** Network events execute on the server's next
//     tick, so two steps in one packet meet the old state. Three seconds comes
//     from experiment; less was not tried;
//   * **`NELoadComplete` comes after the level really loaded.** That is what the
//     event means. While we loaded the level (about eleven seconds) and stayed
//     silent, the server managed to disconnect us;
//   * **the choice waits for the player.** The team, the kit and the spawn point
//     are sent not by the sequence itself but by the spawn screen — after DONE.
//     In a headless run the choice is set in advance, and `Ready` then passes at once;
//   * **the order of the choice is exactly this.** `NESelectTeam`, `NESelectKit`,
//     `NESelectSpawnGroup` — verified against an original server, after them
//     `ServerGameLogic::spawnPlayer` is called.
//
// More in docs/functions/network-events.md.
#include <chrono>
#include <optional>

namespace obf2::net::bf2 {

// What we do at this step.
enum class JoinStep {
  Level,       // say "the level is loaded"
  Content,     // the content check
  Database,    // say "the player base was received"
  Simulation,  // NEStartSimulation — "start counting"
  Ready,       // the spawn screen: waiting for the player to press DONE
  Team,        // NESelectTeam
  Kit,         // NESelectKit
  Group,       // NESelectSpawnGroup
  Done,        // we send nothing more
};

const char* joinStepName(JoinStep step);

// What the player chose on the spawn screen.
struct JoinChoice {
  int team = 1;
  int kit = 0;
  // The spawn group's number **from the server's list** (`CreateSpawnGroupEvent`),
  // not the control point's number from the level's data. Zero means "not
  // chosen": the server spawns only those whose `getSpawnGroup() > 0`.
  int group = 0;
};

class JoinSequence {
 public:
  using Clock = std::chrono::steady_clock;

  // The pauses are taken from the original's captured traffic, not from caution.
  //
  // The content check goes **at the end of loading**, together with
  // `NELoadComplete` — in one packet, without a pause. That is how it should be
  // by its meaning: the server asks whether the content matches exactly when the
  // client has just read it. We used to hold three seconds here "to be safe", and
  // on a fast DONE press the check came out **after** the spawn — a sequence
  // impossible in the original.
  static constexpr std::chrono::milliseconds kContentDelay{0};

  // `NEDatabaseComplete` comes 1.1 s after the check. That is a measured number
  // from the dump, not a rounding.
  static constexpr std::chrono::milliseconds kStepDelay{1100};

  // The player's choice, on the other hand, goes **without a pause**: in the dump
  // the three spawn events are separated by the same fractions of a second as the
  // presses, and the server answers `NEPlayerSpawned` within 100 ms. The pause
  // here was our invention and gave almost ten seconds between DONE and the spawn.
  static constexpr std::chrono::milliseconds kChoiceDelay{0};

  // The server said which level it is playing.
  void setLevelReady() { levelReady_ = true; }
  // We really did load the level and can answer pings.
  void setClientLoaded() { clientLoaded_ = true; }

  // Experiments: skip the content check or the message about the base.
  void setSkipContent(bool skip) { skipContent_ = skip; }
  void setSkipDatabase(bool skip) { skipDatabase_ = skip; }
  // The server sent its own `NEDatabaseComplete`: it has pushed the player base.
  // The original client sends ours **only as the answer** to it (`GameClient::
  // handleEvent`, OpenBF2 GameClient.cpp, `BF2.exe` 0x4d6278). Sent earlier, it
  // reaches the server before `clientSendDatabase`, which then puts the
  // connection back from 4 to 3 — and a connection below 4 is ghosted nothing.
  void setServerDatabaseSent() { serverDatabase_ = true; }
  // A round reset (`NEReset`): `Player::reset` cleared our spawn group, so the
  // choice is made on the spawn screen again (OpenBF2 Player.cpp, +0xD4).
  // A reset before the choice went out clears nothing of ours: the choice made
  // in advance (a headless run) still stands.
  void reopen() {
    if (step_ == JoinStep::Done || step_ == JoinStep::Team || step_ == JoinStep::Kit ||
        step_ == JoinStep::Group) {
      step_ = JoinStep::Ready;
      asked_ = false;
    }
  }
  // The server moved to another map (a new `MapInfo`, block 2). The client loads
  // it and starts over from `NELoadComplete` (`GameClient::handleDataBlock`
  // case 2, OpenBF2): the content check follows it, the server pushes the
  // database again and our answer waits for it, and the choice is made on the
  // new map's spawn screen. Until `setLevelReady` and `setClientLoaded` come for
  // the new level nothing is sent.
  void restartForLevel() {
    step_ = JoinStep::Level;
    started_ = false;
    levelReady_ = false;
    clientLoaded_ = false;
    serverDatabase_ = false;
    asked_ = false;
  }
  // `NEStartSimulation` — "start counting". Whether it is needed for spawning we
  // are still working out, so the step is switched by a flag.
  void setSkipSimulation(bool skip) { skipSimulation_ = skip; }

  // The player pressed DONE. It may be called even before the handshake — then
  // the sequence will not stop at `Ready`.
  void ask(const JoinChoice& choice) {
    choice_ = choice;
    asked_ = true;
  }

  // A new life repeats the selection phase on the existing connection.
  void resetSpawnChoice() {
    if (step_ == JoinStep::Done || step_ == JoinStep::Team || step_ == JoinStep::Kit ||
        step_ == JoinStep::Group) step_ = JoinStep::Ready;
    asked_ = false;
  }

  bool asked() const { return asked_; }
  const JoinChoice& choice() const { return choice_; }
  JoinStep step() const { return step_; }
  bool done() const { return step_ == JoinStep::Done; }

  // Whether it is time for the next step. nullopt means we are still waiting: the
  // pause has not passed, we have not loaded, or we stand at `Ready` with no choice.
  //
  // Steps that send nothing (a skipped check, `Ready` with the choice already
  // made) are passed through right here, so the step returned is always one that
  // really has to be sent.
  std::optional<JoinStep> next(Clock::time_point now);

  // The step was sent: the next one comes no sooner than after the pause.
  void commit(Clock::time_point now);

 private:
  JoinStep step_ = JoinStep::Level;
  // A separate flag rather than comparing the time against zero: a zero stamp is
  // a valid moment too, and in a test it occurs straight away.
  bool started_ = false;
  Clock::time_point last_{};
  bool levelReady_ = false;
  bool clientLoaded_ = false;
  bool skipContent_ = false;
  bool skipDatabase_ = false;
  bool skipSimulation_ = true;
  bool serverDatabase_ = false;
  bool asked_ = false;
  JoinChoice choice_;
};

}  // namespace obf2::net::bf2
