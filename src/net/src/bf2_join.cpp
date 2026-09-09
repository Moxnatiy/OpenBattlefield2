#include "obf2/net/bf2_join.h"

namespace obf2::net::bf2 {

const char* joinStepName(JoinStep step) {
  switch (step) {
    case JoinStep::Level: return "the level is loaded";
    case JoinStep::Content: return "the content check";
    case JoinStep::Database: return "the player base was received";
    case JoinStep::Simulation: return "start counting";
    case JoinStep::Ready: return "the spawn screen";
    case JoinStep::Team: return "team";
    case JoinStep::Kit: return "kit";
    case JoinStep::Group: return "spawn point";
    case JoinStep::Done: return "done";
  }
  return "?";
}

std::optional<JoinStep> JoinSequence::next(Clock::time_point now) {
  if (step_ == JoinStep::Done) return std::nullopt;
  // Until the server has said which level it is playing there is nothing to send.
  if (!levelReady_ || !clientLoaded_) return std::nullopt;
  // The first step goes without waiting — the pause is kept only between steps.
  // After the spawn screen there is no pause at all: the three choice events go in a row.
  const bool afterChoice = step_ == JoinStep::Team || step_ == JoinStep::Kit ||
                           step_ == JoinStep::Group;
  // The content check goes together with "the level is loaded", without a pause:
  // in the dump it is one packet. A pause before it made the impossible happen —
  // the check arrived after the player had spawned.
  const auto delay = step_ == JoinStep::Content ? kContentDelay
                     : afterChoice              ? kChoiceDelay
                                                : kStepDelay;
  const auto wait = std::chrono::duration_cast<Clock::duration>(delay);
  if (started_ && now - last_ < wait) return std::nullopt;

  // Steps that send nothing themselves are passed through right here — otherwise
  // a whole pause would burn on each of them for nothing.
  for (int guard = 0; guard < 8; ++guard) {
    if (step_ == JoinStep::Content && skipContent_) {
      step_ = JoinStep::Database;
      continue;
    }
    if (step_ == JoinStep::Database && skipDatabase_) {
      step_ = JoinStep::Simulation;
      continue;
    }
    if (step_ == JoinStep::Simulation && skipSimulation_) {
      step_ = JoinStep::Ready;
      continue;
    }
    if (step_ == JoinStep::Ready) {
      if (!asked_) return std::nullopt;  // waiting for DONE
      step_ = JoinStep::Team;
      continue;
    }
    break;
  }
  return step_;
}

void JoinSequence::commit(Clock::time_point now) {
  last_ = now;
  started_ = true;
  switch (step_) {
    case JoinStep::Level: step_ = JoinStep::Content; break;
    case JoinStep::Content: step_ = JoinStep::Database; break;
    case JoinStep::Database: step_ = JoinStep::Simulation; break;
    case JoinStep::Simulation: step_ = JoinStep::Ready; break;
    case JoinStep::Ready: step_ = JoinStep::Team; break;
    case JoinStep::Team: step_ = JoinStep::Kit; break;
    case JoinStep::Kit: step_ = JoinStep::Group; break;
    case JoinStep::Group: step_ = JoinStep::Done; break;
    case JoinStep::Done: break;
  }
}

}  // namespace obf2::net::bf2
