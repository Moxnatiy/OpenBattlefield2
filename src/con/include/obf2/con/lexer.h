#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace obf2::con {

// Розбір ОДНОГО рядка .con/.tweak на токени.
//
// Правила, зняті з корпусу гри (mods/bf2, 4155 файлів):
//  * роздільники — пробіл і табуляція, кількість не має значення;
//  * рядки у CRLF, тому '\r' обрізається;
//  * "рядок у лапках" — один токен, лапки не входять у значення;
//  * решта символів (у т.ч. '\', '/', '.', '-') — звичайні символи токена.
//
// Лапки в BF2 не мають escape-послідовностей: зворотний слеш усередині лапок
// це частина шляху ("Ingame\Kits\Icons\kit.tga"), а не екранування.
std::vector<std::string> tokenizeLine(std::string_view line);

// Розбиття першого токена команди на компоненти за крапками:
// "ObjectTemplate.fire.addFireRate" -> {"ObjectTemplate","fire","addFireRate"}
std::vector<std::string> splitCommandPath(std::string_view token);

}  // namespace obf2::con
