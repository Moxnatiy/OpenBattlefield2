#pragma once
// Побудова геометрії тексту: рядок -> прямокутники з координатами в атласі.
//
// Рендерер малює це тим самим пайплайном, що й усе інше, тому текст стає
// звичайним RenderMesh з однією текстурою — атласом шрифту.
#include <string>
#include <string_view>
#include <vector>

#include "obf2/font/dif_font.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::font {

struct TextLayout {
  float x = 0.0f;      // ліворуч-угору, у пікселях екрана
  float y = 0.0f;
  float scale = 1.0f;  // множник до кегля
  int screenWidth = 1280;
  int screenHeight = 720;
};

// Геометрія в координатах NDC, готова до малювання без матриці.
// Порожній результат означає, що жодного гліфа не знайшлося.
mesh::RenderMesh buildText(const Font& font, std::string_view text, const TextLayout& layout,
                           const std::string& atlasPath);

// Розбиває текст на рядки, що вміщаються в задану ширину. Перенос лише по
// пробілах: слово, довше за рядок, лишається цілим і вилазить за межу —
// так само поводиться й оригінал.
std::vector<std::string> wrapText(const Font& font, std::string_view text, float maxWidth,
                                  float scale);

// Скільки пікселів займе рядок — щоб центрувати чи вирівнювати праворуч.
float textWidth(const Font& font, std::string_view text, float scale);

}  // namespace obf2::font
