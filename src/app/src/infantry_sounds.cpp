#include "obf2/app/infantry_sounds.h"
#include "obf2/con/interpreter.h"
#include "obf2/core/path.h"
#include "obf2/game/object_template.h"

namespace obf2::app {

JumpLandingSoundState::Result JumpLandingSoundState::update(
    bool grounded, const Vec3f& velocity, const server::PhysicsConstants& physics,
    const Trace& trace) {
  Result result;
  if (grounded) { reset(); return result; } // 0x61e1a0 ground branch.
  if (landed_) { result.animationUp = velocity.y; return result; }
  if (velocity.y >= physics.animationLandSpeed) {
    if (!jumped_ && velocity.y >= physics.animationJumpSpeed) {
      jumped_ = true;
      result.jump = true;
    }
  } else if (physics.animationLandHeight > 0) {
    const float height = physics.animationLandHeight;
    Vec3f motion = velocity * height;
    const float squared = dot(motion, motion);
    if (squared != height * height) {
      if (squared < height * height) motion.y = -1;
      motion = normalize(motion) * height;
    }
    if (trace) result.material = trace(motion);
    if (result.material) { landed_ = true; result.landing = true; }
  }
  if (jumped_) result.animationUp = landed_ ? 0.f : 1.f;
  else result.animationUp = !landed_ && velocity.y <= 0 ? velocity.y : 0.f;
  return result;
}

bool InfantrySounds::load(FileSystem& files) {
  materials_.clear();
  std::map<int, std::array<std::string, 4>> names;
  std::array<std::string, 4>* current = nullptr;
  con::Interpreter pairs(files, [&](const con::Command& command) {
    if (command.lowerPath == "materialmanager.createcell") {
      current = nullptr;
      // BF2.exe 0x7e2e00 requests the authored (5500, contact, slot) pair.
      if (command.argInt(0) == 5500) {
        if (const auto material = command.argInt(1)) current = &names[*material];
      }
    } else if (current && command.lowerPath == "materialmanager.setsoundtemplate") {
      if (const auto slot = command.argInt(0); slot && *slot >= 0 && *slot < 4)
        (*current)[*slot] = command.argStr(1);
    }
  });
  if (!pairs.runFile("common/material/materialManagerSettings.con")) return false;
  game::Registry definitions;
  con::Interpreter sounds(files, [&](const con::Command& command) { definitions.feed(command); });
  if (!sounds.runFile("common/material/ImpactSounds.con")) return false;
  for (const auto& [material, slots] : names) {
    auto& destination = materials_[material];
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
      const auto* sound = definitions.find(slots[slot]);
      if (!sound || sound->className != "Sound") continue;
      const auto gain = sound->number("volume");
      const auto reverb = sound->number("reverbLevel");
      if (!gain || !reverb) continue; // No invented sound defaults.
      InfantrySoundCue cue;
      cue.name = sound->name;
      cue.gain = *gain;
      cue.reverb = *reverb;
      auto list = sound->text("soundFilename");
      while (!list.empty()) {
        const auto comma = list.find(',');
        const auto path = normalizeAssetPath(list.substr(0, comma));
        if (!path.empty()) cue.files.push_back(path);
        if (comma == std::string_view::npos) break;
        list.remove_prefix(comma + 1);
      }
      if (!cue.files.empty()) destination[slot] = std::move(cue);
    }
  }
  return true;
}

const InfantrySoundCue* InfantrySounds::cue(int material, unsigned slot) const {
  const auto found = materials_.find(material);
  if (found == materials_.end() || slot >= 4) return nullptr;
  const auto& sounds = found->second;
  // BF2.exe 0x7e2e00 aliases missing nonzero slots to slot zero.
  const auto& selected = sounds[slot] ? sounds[slot] : sounds[0];
  return selected ? &*selected : nullptr;
}

}  // namespace obf2::app
