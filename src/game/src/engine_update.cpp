#include "obf2/game/engine_update.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace obf2::game {

unsigned engineTypeFromName(std::string_view name) {
  // operator<<(EngineType), Linux 0x630fa0.
  std::string key(name);
  for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (key == "c_etplane") return 0x1;
  if (key == "c_etcar") return 0x2;
  if (key == "c_ettank") return 0x6;
  if (key == "c_etship") return 0x9;
  if (key == "c_etrocket") return 0x11;
  if (key == "c_ettorpedo") return 0x19;
  if (key == "c_ethelicopter") return 0x21;
  if (key == "c_etnewcar") return 0x42;
  if (key == "c_etnewtank") return 0x46;
  if (key == "c_etnewcar2") return 0xc2;
  return 0;
}

EngineUpdateSettings engineUpdateSettings(const ObjectTemplate& engine) {
  EngineUpdateSettings s;
  s.type = engineTypeFromName(engine.text("setEngineType"));
  if (const auto* property = engine.property("setMaxRotation")) {
    if (const auto v = property->asVec3(0)) s.maxRotation = {v->x, v->y, v->z};
  }
  s.numberOfGears = static_cast<int>(engine.number("setNumberOfGears").value_or(1));
  s.differential = engine.number("setDifferential").value_or(0);
  s.gearUp = engine.number("setGearUp").value_or(0);
  s.gearDown = engine.number("setGearDown").value_or(0);
  s.gearChangeTime = engine.number("setGearChangeTime").value_or(0);
  return s;
}

bool engineHandleUpdate(EngineUpdateState& state, const EngineUpdateSettings& settings, Vec3f angle,
                        float input, float speed, float dt, bool ghost) {
  // Type bit 0x20 (the helicopter's wanted-angle branch, 0x62f0a0…) is not ported.
  if (settings.type & 0x20u) return false;
  // Type bit 0x10 (rocket, torpedo): the input is held at 1 (0x62f1b0).
  if (settings.type & 0x10u) input = 1.0f;

  // After calculateAndClipAngle: w = angle.z (+0x248) over maxRotation.z
  // (template +0x208); +0x2e8 = angle.x / maxRotation.x when that is > 0.
  const float w = angle.z;
  const float maxW = settings.maxRotation.z;
  state.field2e8 = settings.maxRotation.x > 0.0f ? angle.x / settings.maxRotation.x : 0.0f;

  if (!ghost && state.load > 0.0f) {
    state.load = std::max(0.0f, state.load - dt / settings.gearChangeTime);
  }
  // +0x2f0: the input pushes against the rpm's sign (±0.1).
  state.reverse = (input < -0.1f && state.rpm > 0.0f) || (input > 0.1f && state.rpm < 0.0f);

  // rpm' = rpm + inertia · (w / maxW − feedback − 0.5 · rpm).
  float feedback = state.feedback;
  float inertia = kPhyEngineInertia;
  if ((settings.type & 6u) == 6u) {
    // Tank: the feedback falls off with speed; c1 = 1 (0xf67948), maxSpeed =
    // differential · 0.75 (the function's static c2_maxSpeed).
    const float fade = std::clamp(1.0f - speed / (settings.differential * 0.75f), 0.0f, 1.0f);
    feedback = fade * feedback * kFeedbackMod;
    inertia = kPhyTankEngineInertia;
  }
  float rpm = ((w / maxW - feedback) + state.rpm * -0.5f) * inertia + state.rpm;

  float magnitude;
  if (!state.on) {
    rpm = 0.0f;
    state.field2ec = 0.0f;
    magnitude = 0.0f;
  } else if (rpm >= 1.2f) {
    rpm = 1.2f;
    magnitude = 1.2f;
  } else if (rpm >= -1.0f || std::isnan(rpm)) {
    magnitude = std::fabs(rpm);
  } else {
    rpm = -1.0f;
    magnitude = 1.0f;
  }
  if (settings.heatRise > 0.0f) {
    state.heat = state.heat + settings.heatRise * magnitude - settings.heatFall;
    if (state.heat >= 1.0f) {
      state.heat = 1.0f;
      state.on = false;
      rpm = 0.0f;
      magnitude = 0.0f;
    } else if (state.heat < 0.0f) {
      state.heat = 0.0f;
    }
  }
  state.rpm = magnitude >= 0.01f ? rpm : 0.0f;
  if (state.rpm > settings.gearUp && state.load == 0.0f && state.gear < settings.numberOfGears) ++state.gear;
  if (state.rpm < settings.gearDown && state.gear > 1) --state.gear;
  state.feedback = 0.0f;
  return true;
}

}  // namespace obf2::game
