#pragma once
// The values the HUD takes its contents from: show flags, captions, bar fills
// and alpha.
//
// The name is not ours: in the original this is `Code/BF2/Menu/Hud/HudItems.cpp`
// (the path is visible in `BF2_r.exe`), and the command the interface changes
// its own flags with is called `hudItems.setBool`.
//
// There is no window and no geometry here — only values by name. That is exactly
// why it was pulled out: a thing like this can be checked by a test.
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "obf2/engine/console.h"
#include "obf2/hud/states.h"

namespace obf2::hud {

class HudItems {
 public:
  // `hudItems.setBool <name> <0|1>` — this is what the interface turns its own
  // flags on with, `SetSpawnPoint` among them.
  void bind(engine::Console& console);

  VariableMap& flags() { return flags_; }
  const VariableMap& flags() const { return flags_; }

  void setText(std::string name, std::string value) { text_[std::move(name)] = std::move(value); }
  void setValue(std::string name, float value) { values_[std::move(name)] = value; }
  void setAlpha(std::string name, float value) { alpha_[std::move(name)] = value; }

  std::string_view text(std::string_view name) const;
  float value(std::string_view name) const;
  // nullopt means we know nothing about that variable. The node then stays
  // visible: most of these variables are smooth fades, and by default they are
  // on. **That is our decision, not the engine's behaviour**: in the original the
  // HUD writes the values itself (`BF2.exe`, 0x789480 registers them as its
  // object's fields), and who exactly writes them has not been worked out.
  std::optional<float> alpha(std::string_view name) const;

  // Whether anything has changed since the screen was last drawn.
  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  void markDirty() { dirty_ = true; }

 private:
  VariableMap flags_;
  std::map<std::string, std::string, std::less<>> text_;
  std::map<std::string, float, std::less<>> values_;
  std::map<std::string, float, std::less<>> alpha_;
  bool dirty_ = false;
};

}  // namespace obf2::hud
