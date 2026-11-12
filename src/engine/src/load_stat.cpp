#include "obf2/engine/load_stat.h"

namespace obf2::engine {

// The window's growth per unit of weight (flt_8898908 in B2BF's reading of
// setNextLoadPart: 0.15000001f).
constexpr float kWindowPerWeight = 0.15000001f;

void LoadStat::nextPart(const std::string& name, int steps, float weight) {
  if (steps <= 0) return;
  done_ = 0;
  steps_ = steps;
  start_ = end_;
  end_ = end_ + kWindowPerWeight * weight;
  if (end_ > 1.0f) end_ = 1.0f;
  partName_ = name;
  currentFile_ = name;  // the part's name stands as the caption until a file is named
}

void LoadStat::step(int count) {
  done_ += count;
  if (done_ > steps_) done_ = steps_;
}

void LoadStat::reset() { *this = LoadStat(); }

float LoadStat::progress() const {
  if (complete_) return 1.0f;
  if (steps_ != 0) {
    const float share = static_cast<float>(done_ < steps_ ? done_ : steps_) / static_cast<float>(steps_);
    return start_ + share * (end_ - start_);
  }
  return start_;
}

}  // namespace obf2::engine
