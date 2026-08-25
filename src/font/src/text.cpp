#include "obf2/font/text.h"

#include "obf2/core/math.h"

namespace obf2::font {
namespace {

// Нормаль уздовж джерела світла з фрагментного шейдера: так півламбертів
// множник дорівнює одиниці й текст не темніє. Тимчасово, доки немає
// окремого пайплайна для інтерфейсу (див. docs/TODO.md).
mesh::Vec3 unlitNormal() {
  const Vec3f light = normalize(Vec3f{0.4f, 0.9f, 0.35f});
  return mesh::Vec3{light.x, light.y, light.z};
}

}  // namespace

std::vector<std::string> wrapText(const Font& font, std::string_view text, float maxWidth,
                                  float scale) {
  std::vector<std::string> lines;
  std::string current;

  std::size_t position = 0;
  while (position <= text.size()) {
    const std::size_t space = text.find(' ', position);
    const std::string_view word =
        text.substr(position, space == std::string_view::npos ? space : space - position);

    const std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
    if (!current.empty() && textWidth(font, candidate, scale) > maxWidth) {
      lines.push_back(current);
      current = std::string(word);
    } else {
      current = candidate;
    }

    if (space == std::string_view::npos) break;
    position = space + 1;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

float textWidth(const Font& font, std::string_view text, float scale) {
  return font.measure(text) * scale;
}

mesh::RenderMesh buildText(const Font& font, std::string_view text, const TextLayout& layout,
                           const std::string& atlasPath) {
  mesh::RenderMesh out;
  if (font.atlasWidth <= 0 || font.atlasHeight <= 0 || layout.screenWidth <= 0 ||
      layout.screenHeight <= 0) {
    return out;
  }

  const mesh::Vec3 normal = unlitNormal();
  const float atlasW = static_cast<float>(font.atlasWidth);
  const float atlasH = static_cast<float>(font.atlasHeight);

  // Пікселі екрана -> NDC. Вісь Y у NDC дивиться вгору, у тексті — вниз.
  auto toNdcX = [&](float pixels) { return pixels / static_cast<float>(layout.screenWidth) * 2.0f - 1.0f; };
  auto toNdcY = [&](float pixels) { return 1.0f - pixels / static_cast<float>(layout.screenHeight) * 2.0f; };

  float penX = layout.x;
  std::uint32_t previous = 0;

  std::size_t position = 0;
  while (position < text.size()) {
    const std::uint32_t code = nextCodepoint(text, position);
    const Glyph* glyph = font.glyph(code);
    if (glyph == nullptr) {
      previous = 0;
      continue;
    }
    if (previous != 0) penX += font.kerning(previous, code) * layout.scale;

    if (glyph->pixelWidth() > 0 && glyph->pixelHeight() > 0) {
      const float x0 = penX + glyph->bearingLeft * layout.scale;
      const float y0 = layout.y + static_cast<float>(glyph->offsetY) * layout.scale;
      const float x1 = x0 + static_cast<float>(glyph->pixelWidth()) * layout.scale;
      const float y1 = y0 + static_cast<float>(glyph->pixelHeight()) * layout.scale;

      const float u0 = static_cast<float>(glyph->left) / atlasW;
      const float v0 = static_cast<float>(glyph->top) / atlasH;
      const float u1 = static_cast<float>(glyph->right) / atlasW;
      const float v1 = static_cast<float>(glyph->bottom) / atlasH;

      const auto base = static_cast<std::uint32_t>(out.vertices.size());
      out.vertices.push_back(mesh::Vertex{{toNdcX(x0), toNdcY(y0), 0.0f}, normal, {u0, v0}});
      out.vertices.push_back(mesh::Vertex{{toNdcX(x1), toNdcY(y0), 0.0f}, normal, {u1, v0}});
      out.vertices.push_back(mesh::Vertex{{toNdcX(x0), toNdcY(y1), 0.0f}, normal, {u0, v1}});
      out.vertices.push_back(mesh::Vertex{{toNdcX(x1), toNdcY(y1), 0.0f}, normal, {u1, v1}});

      // Обхід проти годинникової — той самий, що й у мешах гри.
      out.indices.insert(out.indices.end(),
                         {base, base + 2, base + 1, base + 1, base + 2, base + 3});
    }

    penX += glyph->advance() * layout.scale;
    previous = code;
  }

  if (!out.indices.empty()) {
    mesh::DrawRange range;
    range.indexCount = static_cast<std::uint32_t>(out.indices.size());
    range.maps.push_back(atlasPath);
    out.ranges.push_back(std::move(range));
  }
  return out;
}

}  // namespace obf2::font
