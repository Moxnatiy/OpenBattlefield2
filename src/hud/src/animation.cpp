#include "obf2/hud/animation.h"

#include <cmath>

namespace obf2::hud {

void Animator::setVisible(const Node& node, bool visible) {
  auto [it, inserted] = entries_.try_emplace(node.name);
  Entry& entry = it->second;
  if (inserted) {
    // Перший кадр: без переходу. Інакше весь HUD «в'їжджав» би на старті.
    entry.progress = visible ? 1.0f : 0.0f;
  }
  entry.visible = visible;
  entry.inTime = node.inTime;
  entry.outTime = node.outTime;
}

void Animator::advance(float dt) {
  animating_ = false;
  for (auto& [name, entry] : entries_) {
    const float target = entry.visible ? 1.0f : 0.0f;
    if (entry.progress != target) animating_ = true;
  }
  if (dt <= 0.0f) return;
  for (auto& [name, entry] : entries_) {
    const float time = entry.visible ? entry.inTime : entry.outTime;
    if (time <= 0.0f) {
      entry.progress = entry.visible ? 1.0f : 0.0f;
      continue;
    }
    // Рівномірно: клас руху зветься Bf2MoveEffect, а не Bf2SinMoveEffect.
    const float step = dt / time;
    entry.progress += entry.visible ? step : -step;
    if (entry.progress > 1.0f) entry.progress = 1.0f;
    if (entry.progress < 0.0f) entry.progress = 0.0f;
  }
}

ShowState Animator::state(const Node& node) const {
  ShowState out;
  const auto it = entries_.find(node.name);
  // Про вузол ще не чули — хай вирішує звичайна умова показу.
  if (it == entries_.end()) return out;
  out.known = true;
  out.progress = it->second.progress;
  if (node.showEffects.empty()) {
    // Без ефекту перехід не має чим себе показати: вузол просто
    // з'являється і зникає.
    out.progress = out.progress > 0.0f ? 1.0f : 0.0f;
    return out;
  }
  for (const ShowEffectInfo& effect : node.showEffects) {
    switch (effect.kind) {
      case ShowEffect::Alpha:
      case ShowEffect::Blend:
        out.alpha *= out.progress;
        break;
      case ShowEffect::Move: {
        // Формула — дослівно з `dice::meme::MoveEffect::picturePaint`
        // (`MemeDll.dll`, 0x10001b27; бібліотека лежить у теці мода й
        // експортує повні символи C++):
        //
        //   кут    = «Move direction»
        //   довжина = «Move length»
        //   хід     = EffectPipe+0x18
        //   зсув    = (1 - хід) * довжина
        //   dx = -cos(кут) * зсув
        //   dy = +sin(кут) * зсув
        //
        // **Обидва знаки в нас були протилежні.** Раніше вони стояли
        // `(+cos, -sin)`, виведені з міркування «панель голосування має
        // виїжджати знизу». Міркування не витримало перевірки джерелом:
        // елементи в нас прилітали з протилежного боку, ніж в оригіналі.
        const float left = 1.0f - out.progress;
        out.offsetX += -std::cos(effect.angle) * effect.distance * left;
        out.offsetY += std::sin(effect.angle) * effect.distance * left;
        break;
      }
    }
  }
  return out;
}

}  // namespace obf2::hud
