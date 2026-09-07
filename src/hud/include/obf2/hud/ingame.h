#pragma once
// Складання бойового HUD: корінь `Global` плюс кутові ділянки.
//
// Ділянок у грі сім, і вузли в них дістають координати **від лівого
// верхнього кута ділянки** — це написали самі розробники в
// `Menu/HUD/HudSetup/Readme.txt`. Де ті кути — сказано не в коді, а в
// `Menu/Ingame` (obf2/meme/graph.h, `Graph::layers`):
//
//   BottomLeftAnimate   BfTransformNode 400x64   X<-BottomLeft_XPos   Y=563
//     Next node -> TransformNode  X=-1  Y=563  400x64   (Static)
//   BottomRightAnimate  BfTransformNode 600x100  X<-BottomRight_XPos  Y=497
//     Next node -> TransformNode  X=401 Y=563  400x64   (Static)
//
// X у «рухомих» ділянок — це змінна, і в файлі збережено сховане
// положення (-295 і 503): з ним вміст цілком за краєм екрана.
//
// Бойовий HUD доводиться складати наново, а не пекти назавжди: у грі
// його змінні пише не старт рівня, а щокадрова робота — 0x78d0f0 бере
// поточного гравця і або вмикає `PlayerHealthShow` (0x78d154), або
// гасить увесь набір (0x78d2d9).
#include <functional>
#include <string>
#include <vector>

#include "obf2/hud/render.h"
#include "obf2/meme/graph.h"

namespace obf2::hud {

struct IngameLayer {
  std::string group;
  float x = 0.0f;
  float y = 0.0f;
  // До якого краю ділянка тулиться на широкому екрані. **Це наше**: у грі
  // 800x600 і такого питання немає.
  Anchor anchor = Anchor::Left;
};

// Ділянки за графом. `leftX`/`rightX` — поточні значення змінних, які
// веде граф; решта чисел береться з файлу, а запасні (на випадок, коли
// графа нема) збігаються з ним.
std::vector<IngameLayer> ingameLayers(const meme::Graph& graph, float leftX, float rightX);

// Скласти бойовий HUD: спершу `Global`, далі кожна ділянка своїм
// коренем — у даних ніщо не веде до них із `Global`.
//
// `onLayer` кличеться на кожну непорожню ділянку — для звітів
// (`--hud-rects`) і щоб не тягти сюди друк.
std::vector<DrawPiece> buildIngame(const Builder& builder, const std::vector<IngameLayer>& layers,
                                   const font::Font& font, const std::string& fontAtlas,
                                   const Screen& screen, const Context& context,
                                   const std::function<void(const IngameLayer&,
                                                            const std::vector<DrawPiece>&)>&
                                       onLayer = {});

}  // namespace obf2::hud
