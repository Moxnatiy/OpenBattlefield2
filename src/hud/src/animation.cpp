#include "obf2/hud/animation.h"

#include <cmath>

namespace obf2::hud {

void Animator::setVisible(const Node& node, bool visible) {
  auto [it, inserted] = entries_.try_emplace(node.name);
  Entry& entry = it->second;
  if (inserted) {
    // Щойно створений `CullNode` має хід -4 (`MemeDll.dll`, 0x10004a57
    // звіряє саме з цією міткою), і **перший же показ іде переходом**: у
    // тій самій гілці хід ставиться в 0 і одразу починає рости. Тобто
    // вузол, видимий від початку, в оригіналі теж в'їжджає — це видно на
    // `GlobalHud`, у якого `setNodeInTime 2`.
    entry.progress = 0.0f;
  }
  entry.visible = visible;
  entry.inTime = node.inTime;
  entry.outTime = node.outTime;
}

void Animator::advance(float dt) {
  animating_ = false;
  for (auto& [name, entry] : entries_) {
    const float target = entry.visible ? 1.0f : 0.0f;
    const float was = entry.progress;
    if (entry.progress != target) {
      const float time = entry.visible ? entry.inTime : entry.outTime;
      if (time <= 0.0f || dt <= 0.0f) {
        // Нульовий час — миттєвий перехід: у 0x10004a57 хід одразу стає
        // міткою «показано» чи «сховано».
        if (time <= 0.0f) entry.progress = target;
      } else {
        // Рівномірно: `хід += dt / «In time»` і `хід -= dt / «Out time»`,
        // дослівно з `CullNode::iterateUpdate`.
        const float step = dt / time;
        entry.progress += entry.visible ? step : -step;
        if (entry.progress > 1.0f) entry.progress = 1.0f;
        if (entry.progress < 0.0f) entry.progress = 0.0f;
      }
    }
    // «Ще рухається» — це і «не доїхав», і «саме цим кроком доїхав»:
    // кадр, у якому вузол став на місце, теж треба перемалювати.
    if (entry.progress != target || entry.progress != was) animating_ = true;
  }
}

ShowState Animator::state(const Node& node) const {
  ShowState out;
  const auto it = entries_.find(node.name);
  // Про вузол ще не чули — хай вирішує звичайна умова показу.
  if (it == entries_.end()) return out;
  out.known = true;
  out.progress = it->second.progress;

  // **Прозорість множить сам cull-вузол, а не ефект.**
  // `dice::meme::CullNode::iteratePaint` (`MemeDll.dll`, 0x1000141a):
  //
  //   хід >= 1  -> діти малюються з батьківською трубою, без жодних змін;
  //   хід <= 0  -> діти не малюються взагалі;
  //   інакше    -> нова труба, і в ній `альфа = батьківська * хід`.
  //
  // Тобто вузол із `setNodeInTime`, але **без** `addNodeAlphaShowEffect`
  // однаково згасає — просто тому, що він під cull-вузлом. Доти ми
  // множили на хід лише за наявності alpha-ефекту, а вузол без ефектів
  // узагалі перемикали миттєво; через це, наприклад, смуга часу
  // (`TimeItems`, у неї лише move-ефект) в оригіналі виїжджає й
  // проявляється, а в нас лише виїжджала.
  out.alpha *= out.progress;

  for (const ShowEffectInfo& effect : node.showEffects) {
    switch (effect.kind) {
      case ShowEffect::Alpha:
      case ShowEffect::Blend:
        // Окремої дії тут немає: згасання вже зробив cull-вузол вище.
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
