#include "obf2/net/bf2_join.h"

namespace obf2::net::bf2 {

const char* joinStepName(JoinStep step) {
  switch (step) {
    case JoinStep::Level: return "рівень завантажено";
    case JoinStep::Content: return "перевірка вмісту";
    case JoinStep::Database: return "база гравців отримана";
    case JoinStep::Simulation: return "почати відлік";
    case JoinStep::Ready: return "екран появи";
    case JoinStep::Team: return "команда";
    case JoinStep::Kit: return "набір";
    case JoinStep::Group: return "місце появи";
    case JoinStep::Done: return "готово";
  }
  return "?";
}

std::optional<JoinStep> JoinSequence::next(Clock::time_point now) {
  if (step_ == JoinStep::Done) return std::nullopt;
  // Доки сервер не сказав, який рівень він грає, слати нема чого.
  if (!levelReady_ || !clientLoaded_) return std::nullopt;
  // Перший крок іде без чекання — паузу тримаємо лише між кроками. Після
  // екрана появи паузи немає взагалі: три події вибору йдуть поспіль.
  const bool afterChoice = step_ == JoinStep::Team || step_ == JoinStep::Kit ||
                           step_ == JoinStep::Group;
  const auto wait = afterChoice ? std::chrono::duration_cast<Clock::duration>(kChoiceDelay)
                                : std::chrono::duration_cast<Clock::duration>(kStepDelay);
  if (started_ && now - last_ < wait) return std::nullopt;

  // Кроки, які самі нічого не шлють, проходимо тут-таки — інакше на
  // кожен із них марно згорала б ціла пауза.
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
      if (!asked_) return std::nullopt;  // чекаємо на DONE
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
