#pragma once
#include <string>
#include <string_view>

namespace obf2 {

// "Ingame\Weapons\Icons\Hud\icon.tga" -> "ingame/weapons/icons/hud/icon.tga"
// Зворотні слеші -> прямі, нижній регістр, схлопнуті та обрізані слеші,
// прибрані сегменти "." та розгорнуті "..". Це канонічний ключ, за яким
// шукаємо файл і в zip-архіві, і на диску.
std::string normalizeAssetPath(std::string_view raw);

// Об'єднання двох ассетних шляхів із наступною нормалізацією.
std::string joinAssetPath(std::string_view base, std::string_view rel);

// Каталог, у якому лежить шлях ("a/b/c.con" -> "a/b"). Без кінцевого слеша.
std::string_view assetParentDir(std::string_view normalized);

// Розширення без крапки ("a/b.tweak" -> "tweak"). Очікує вже нормалізований
// шлях, тому регістр не чіпає — повертає view у той самий буфер.
std::string_view assetExtension(std::string_view normalized);

}  // namespace obf2
