#include "obf2/game/weapon_recoil.h"

#include <cmath>

#include "obf2/game/object_template.h"

namespace obf2::game {
namespace {

// BF2.exe 0x43ffa0: two modular multiplications per sample. 0x440030
// converts the upper 24 bits, plus one, to the uniform distribution.
std::uint32_t nextSeed(std::uint32_t seed) {
  return static_cast<std::uint32_t>((std::uint64_t{seed & 0x7fffffff} * 0x5e30 * 0x661f)
                                    % 0x7fffffff);
}

}  // namespace

void WeaponRecoil::setWeapon(const ObjectTemplate* weapon) {
  graph_.clear();
  firstRecoilAction_.reset();
  const auto* recoil = weapon ? weapon->component("recoil") : nullptr;
  if (!recoil || !weapon->component("SoldierBasedRecoilComp")) return;
  const auto property = [&](const char* name) -> const Property* {
    const auto found = recoil->properties.find(name);
    return found == recoil->properties.end() || found->second.empty() ? nullptr : &found->second.back();
  };
  const auto* enabled = property("hasrecoilforce");
  if (!enabled || !enabled->asBool().value_or(false)) return;
  const auto range = [&](const char* name) {
    const auto* value = property(name);
    if (!value) return ForceRange{};
    const auto args = value->value();
    const auto slash = args.find('/');
    if (slash == std::string_view::npos) return ForceRange{};
    const auto type = args.substr(0, slash);
    if (type != "CRD_UNIFORM" && type != "CRD_NONE") return ForceRange{};
    con::Command tuple;
    tuple.args.emplace_back(args.substr(slash + 1));
    const auto values = tuple.argVec3(0);
    if (!values || !std::isfinite(values->x) || !std::isfinite(values->y)) return ForceRange{};
    return type == "CRD_UNIFORM" ? ForceRange{values->x, values->y, true}
                                 : ForceRange{values->x, values->x, false};
  };
  const auto number = [&](const char* key, float fallback) {
    const auto* value = property(key);
    return value ? value->asFloat().value_or(fallback) : fallback;
  };
  up_ = range("recoilforceup");
  side_ = range("recoilforceleftright");
  // SoldierBasedRecoilCompTemplate constructor, BF2.exe 0x609739..0x60974f.
  zoomModifier_ = number("zoommodifier", 1.0f);
  // The graph defaults 8, 2 and 2 are not measured: the constructor's stores
  // for them are not read (docs/functions/weapon-recoil.md).
  const auto* frames = property("recoilgraphframecount");
  const int count = frames ? frames->asInt().value_or(8) : 8;
  const float exponent = number("recoilgraphexponent", 2.0f);
  const float movement = number("recoilgraphtotalmovement", 2.0f);
  if (count < 2 || count > 1024 || !std::isfinite(exponent) || !std::isfinite(movement) ||
      !std::isfinite(zoomModifier_)) return;
  graph_.resize(count);
  float sum = 0.0f;
  for (int i = 0; i < count; ++i) {
    graph_[i] = std::pow(static_cast<float>(i + 1), exponent);
    sum += graph_[i];
  }
  if (!std::isfinite(sum) || sum == 0.0f) {
    graph_.clear();
    return;
  }
  // BF2.exe 0x609800: kick weights, newest read from the end of the graph.
  for (auto& weight : graph_) weight *= -movement / sum;
  const auto* goBack = property("gobackonrecoil");
  if (goBack && !goBack->asBool().value_or(true)) return;

  // BF2.exe 0x6098a4..0x6099cd: prepend the return samples, offset and
  // amplify the original weights, then balance positive against negative.
  const int returnFrames = count / 4;
  const float offset = graph_.back() * -0.5f;
  graph_.insert(graph_.begin(), returnFrames, offset);
  for (int i = 0; i < returnFrames; ++i) graph_[i] -= graph_[2 * returnFrames - i] * 1.5f;
  float positive = 0.0f, negative = 0.0f;
  for (int i = 0; i < static_cast<int>(graph_.size()); ++i) {
    auto& weight = graph_[i];
    if (i >= returnFrames) weight = weight * 1.5f + offset;
    if (weight > 0.0f) positive += weight;
    else negative += weight;
  }
  if (positive > 0.0f) {
    for (auto& weight : graph_) {
      if (weight > 0.0f) weight *= -negative / positive;
    }
  }
}

void WeaponRecoil::onRound(std::uint32_t action, bool zoomed) {
  if (graph_.empty()) return;
  // BF2.exe 0x609024 reads GameClient's input index (getter 0x615910,
  // set from the replayed action at 0x4d4c3b), then discards a draw at 0x609032.
  auto seed = nextSeed(action);
  const auto sample = [&](const ForceRange& range) {
    if (!range.uniform) return range.low;
    seed = nextSeed(seed);
    const float unit = static_cast<float>((seed >> 7) + 1) * 0x1p-24f;
    return range.low + (range.high - range.low) * unit;
  };
  const float modifier = zoomed ? zoomModifier_ : 1.0f;  // BF2.exe 0x608d10
  force_.pitch = sample(up_) * modifier;
  force_.yaw = sample(side_) * modifier;
  // The firearm confirms the round after the action's look has already run
  // in RemoteWorld. Start the graph with its peak on the next input.
  firstRecoilAction_ = action + 1;
}

RecoilDelta WeaponRecoil::delta(std::uint32_t action) const {
  if (!firstRecoilAction_) return {};
  const auto age = action - *firstRecoilAction_;
  // BF2.exe 0x60923c and 0x609269: index length - 1 - age, excluding index zero.
  if (age + std::uint64_t{1} >= graph_.size()) return {};
  const float weight = graph_[graph_.size() - 1 - age];
  return {force_.yaw * weight, force_.pitch * weight};
}

}  // namespace obf2::game
