#include "raster.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "base/image.h"

namespace obf2::swf {
namespace {

// A picture the movie uses: RGBA texels kept on the CPU.
struct Image : gameswf::bitmap_info {
  int w = 0, h = 0;
  std::vector<std::uint8_t> rgba;
};

gameswf::point transform(const gameswf::matrix& m, float x, float y) {
  gameswf::point out;
  m.transform(&out, gameswf::point(x, y));
  return out;
}

void colourOf(const gameswf::cxform& cx, gameswf::rgba c, float out[4]) {
  const gameswf::rgba t = cx.transform(c);
  out[0] = t.m_r / 255.0f;
  out[1] = t.m_g / 255.0f;
  out[2] = t.m_b / 255.0f;
  out[3] = t.m_a / 255.0f;
}

}  // namespace

void Raster::resize(int width, int height) {
  width_ = width;
  height_ = height;
  pixels_.assign(static_cast<std::size_t>(width) * height * 4, 0);
  mask_.assign(static_cast<std::size_t>(width) * height, 0);
}

gameswf::bitmap_info* Raster::create_bitmap_info_empty() { return new Image(); }

gameswf::bitmap_info* Raster::create_bitmap_info_alpha(int w, int h, unsigned char* data) {
  // Glyph textures: white, the coverage as alpha.
  auto* im = new Image();
  im->w = im->m_original_width = w;
  im->h = im->m_original_height = h;
  im->rgba.resize(static_cast<std::size_t>(w) * h * 4);
  for (int i = 0; i < w * h; ++i) {
    im->rgba[i * 4 + 0] = im->rgba[i * 4 + 1] = im->rgba[i * 4 + 2] = 255;
    im->rgba[i * 4 + 3] = data[i];
  }
  return im;
}

gameswf::bitmap_info* Raster::create_bitmap_info_rgb(image::rgb* src) {
  auto* im = new Image();
  if (src == nullptr) return im;
  im->w = im->m_original_width = src->m_width;
  im->h = im->m_original_height = src->m_height;
  im->rgba.resize(static_cast<std::size_t>(im->w) * im->h * 4);
  for (int y = 0; y < im->h; ++y) {
    const std::uint8_t* row = src->m_data + y * src->m_pitch;
    for (int x = 0; x < im->w; ++x) {
      std::uint8_t* o = &im->rgba[(static_cast<std::size_t>(y) * im->w + x) * 4];
      o[0] = row[x * 3 + 0];
      o[1] = row[x * 3 + 1];
      o[2] = row[x * 3 + 2];
      o[3] = 255;
    }
  }
  return im;
}

gameswf::bitmap_info* Raster::create_bitmap_info_rgba(image::rgba* src) {
  auto* im = new Image();
  if (src == nullptr) return im;
  im->w = im->m_original_width = src->m_width;
  im->h = im->m_original_height = src->m_height;
  im->rgba.resize(static_cast<std::size_t>(im->w) * im->h * 4);
  for (int y = 0; y < im->h; ++y) {
    std::memcpy(&im->rgba[static_cast<std::size_t>(y) * im->w * 4], src->m_data + y * src->m_pitch,
                static_cast<std::size_t>(im->w) * 4);
  }
  return im;
}

void Raster::delete_bitmap_info(gameswf::bitmap_info* bi) { delete bi; }

gameswf::bitmap_info* Raster::makeImage(int width, int height, const std::uint8_t* rgba) {
  auto* im = new Image();
  im->w = im->m_original_width = width;
  im->h = im->m_original_height = height;
  im->rgba.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
  return im;
}

void Raster::updateImage(gameswf::bitmap_info* image, const std::uint8_t* src, bool bgra) {
  auto* im = static_cast<Image*>(image);
  const std::size_t n = im->rgba.size();
  if (!bgra) {
    std::memcpy(im->rgba.data(), src, n);
    return;
  }
  for (std::size_t i = 0; i + 3 < n; i += 4) {
    im->rgba[i + 0] = src[i + 2];
    im->rgba[i + 1] = src[i + 1];
    im->rgba[i + 2] = src[i + 0];
    im->rgba[i + 3] = 255;
  }
}

void Raster::begin_display(gameswf::rgba, int, int, int, int, float x0, float x1, float y0, float y1) {
  // The background colour is not painted: SwiffPlayer leaves the menu's
  // background to its host (the stage's colour goes to an unused argument),
  // and ours composes the frame over the engine's own picture.
  x0_ = x0;
  x1_ = x1;
  y0_ = y0;
  y1_ = y1;
  std::fill(pixels_.begin(), pixels_.end(), 0);
  maskMode_ = MaskMode::Off;
}

void Raster::toPixels(float tx, float ty, float* px, float* py) const {
  *px = (tx - x0_) / (x1_ - x0_) * static_cast<float>(width_);
  *py = (ty - y0_) / (y1_ - y0_) * static_cast<float>(height_);
}

void Raster::fill_style_disable(int side) {
  if (side >= 0 && side < 2) fill_[side].enabled = false;
}

void Raster::fill_style_color(int side, gameswf::rgba color) {
  if (side < 0 || side > 1) return;
  Fill& f = fill_[side];
  f.enabled = true;
  f.bitmap = false;
  colourOf(cxform_, color, f.color);
  prepare(f);
}

void Raster::prepare(Fill& f) {
  for (int k = 0; k < 4; ++k) {
    f.rgba[k] = static_cast<std::uint8_t>(std::lround(std::clamp(f.color[k], 0.0f, 1.0f) * 255.0f));
    f.mul[k] = static_cast<int>(std::lround(f.transform.m_[k][0] * 256.0f));
    f.add[k] = static_cast<int>(std::lround(f.transform.m_[k][1]));
  }
  f.identity = true;
  for (int k = 0; k < 4; ++k) {
    if (f.mul[k] != 256 || f.add[k] != 0) f.identity = false;
  }
  // Alpha can only be 0: a solid colour with none, or a bitmap whose transform
  // multiplies alpha by nothing and adds none.
  f.invisible = f.bitmap ? (f.mul[3] <= 0 && f.add[3] <= 0) : f.rgba[3] == 0;
}

void Raster::fill_style_bitmap(int side, const gameswf::bitmap_info* bi, const gameswf::matrix& m,
                               bitmap_wrap_mode wm) {
  if (side < 0 || side > 1) return;
  Fill& f = fill_[side];
  f.enabled = bi != nullptr;
  f.bitmap = true;
  f.image = bi;
  f.map = m;
  f.wrap = wm == WRAP_REPEAT;
  f.transform = cxform_;
  prepare(f);
}

void Raster::line_style_color(gameswf::rgba color) {
  line_.enabled = true;
  line_.bitmap = false;
  colourOf(cxform_, color, line_.color);
  prepare(line_);
}

namespace {

// Straight-alpha "over" in integers: the source's colour and alpha (0..255)
// onto the destination, which keeps straight alpha too.
inline void blendOver(std::uint8_t* d, int r, int g, int b, int a) {
  if (a <= 0) return;
  const int da = d[3];
  if (a >= 255 || da == 0) {
    d[0] = static_cast<std::uint8_t>(r);
    d[1] = static_cast<std::uint8_t>(g);
    d[2] = static_cast<std::uint8_t>(b);
    d[3] = static_cast<std::uint8_t>(a >= 255 ? 255 : a);
    return;
  }
  const int t = (da * (255 - a) + 127) / 255;  // what of the destination shows through
  const int outA = a + t;
  const int half = outA / 2;
  d[0] = static_cast<std::uint8_t>((r * a + d[0] * t + half) / outA);
  d[1] = static_cast<std::uint8_t>((g * a + d[1] * t + half) / outA);
  d[2] = static_cast<std::uint8_t>((b * a + d[2] * t + half) / outA);
  d[3] = static_cast<std::uint8_t>(outA);
}

inline int wrapIndex(int i, int n) {
  i %= n;
  return i < 0 ? i + n : i;
}

inline int clampIndex(int i, int n) { return i < 0 ? 0 : (i >= n ? n - 1 : i); }

}  // namespace

// One row of a triangle, pixels x0..x1 inclusive, (u, v) at the first pixel's
// centre stepping by (du, dv). Bilinear sampling with texel centres at .5, the
// colour transform as mult × c + add, then "over".
void Raster::span(int y, int x0, int x1, float u, float v, float du, float dv, const Fill& fill) {
  const std::size_t row = static_cast<std::size_t>(y) * width_;
  if (maskMode_ == MaskMode::Writing) {
    std::fill(mask_.begin() + static_cast<std::ptrdiff_t>(row + x0),
              mask_.begin() + static_cast<std::ptrdiff_t>(row + x1 + 1), std::uint8_t{1});
    return;
  }
  const bool testing = maskMode_ == MaskMode::Testing;
  const std::uint8_t* mask = mask_.data() + row;
  std::uint8_t* out = pixels_.data() + row * 4;
  if (!fill.bitmap) {
    const int r = fill.rgba[0], g = fill.rgba[1], b = fill.rgba[2], a = fill.rgba[3];
    for (int x = x0; x <= x1; ++x) {
      if (testing && mask[x] == 0) continue;
      blendOver(out + x * 4, r, g, b, a);
    }
    return;
  }
  const auto* im = static_cast<const Image*>(fill.image);
  if (im == nullptr || im->w == 0 || im->h == 0) return;
  const int w = im->w, h = im->h;
  const std::uint8_t* tex = im->rgba.data();
  for (int x = x0; x <= x1; ++x, u += du, v += dv) {
    if (testing && mask[x] == 0) continue;
    const float fx = u - 0.5f, fy = v - 0.5f;
    const float flx = std::floor(fx), fly = std::floor(fy);
    int tx = static_cast<int>(flx), ty = static_cast<int>(fly);
    const int ax = static_cast<int>((fx - flx) * 256.0f), ay = static_cast<int>((fy - fly) * 256.0f);
    int tx1, ty1;
    if (fill.wrap) {
      tx = wrapIndex(tx, w);
      ty = wrapIndex(ty, h);
      tx1 = tx + 1 == w ? 0 : tx + 1;
      ty1 = ty + 1 == h ? 0 : ty + 1;
    } else {
      tx1 = clampIndex(tx + 1, w);
      ty1 = clampIndex(ty + 1, h);
      tx = clampIndex(tx, w);
      ty = clampIndex(ty, h);
    }
    const std::uint8_t* p00 = tex + (static_cast<std::size_t>(ty) * w + tx) * 4;
    const std::uint8_t* p10 = tex + (static_cast<std::size_t>(ty) * w + tx1) * 4;
    const std::uint8_t* p01 = tex + (static_cast<std::size_t>(ty1) * w + tx) * 4;
    const std::uint8_t* p11 = tex + (static_cast<std::size_t>(ty1) * w + tx1) * 4;
    int c[4];
    for (int k = 0; k < 4; ++k) {
      const int top = p00[k] * (256 - ax) + p10[k] * ax;
      const int bottom = p01[k] * (256 - ax) + p11[k] * ax;
      c[k] = (top * (256 - ay) + bottom * ay + (1 << 15)) >> 16;
    }
    if (!fill.identity) {
      for (int k = 0; k < 4; ++k) {
        const int t = ((c[k] * fill.mul[k]) >> 8) + fill.add[k];
        c[k] = t < 0 ? 0 : (t > 255 ? 255 : t);
      }
    }
    blendOver(out + x * 4, c[0], c[1], c[2], c[3]);
  }
}

void Raster::triangle(const Vertex& a, const Vertex& b, const Vertex& c, const Fill& fill) {
  if (fill.invisible && maskMode_ != MaskMode::Writing) return;
  const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  if (std::fabs(area) < 1e-6f) return;
  const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
  const int maxX = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
  const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
  const int maxY = std::min(height_ - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
  if (minX > maxX || minY > maxY) return;
  // The barycentric weights are linear in the pixel centre: w = A·px + B·py + C.
  const float inv = 1.0f / area;
  const float A[3] = {(b.y - c.y) * inv, (c.y - a.y) * inv, 0};
  const float B[3] = {(c.x - b.x) * inv, (a.x - c.x) * inv, 0};
  const float C[3] = {(b.x * c.y - b.y * c.x) * inv, (c.x * a.y - c.y * a.x) * inv, 0};
  const float A2 = -A[0] - A[1], B2 = -B[0] - B[1], C2 = 1.0f - C[0] - C[1];
  const float Ae[3] = {A[0], A[1], A2}, Be[3] = {B[0], B[1], B2}, Ce[3] = {C[0], C[1], C2};
  // u and v are linear too.
  const float Au = A[0] * a.u + A[1] * b.u + A2 * c.u, Bu = B[0] * a.u + B[1] * b.u + B2 * c.u,
              Cu = C[0] * a.u + C[1] * b.u + C2 * c.u;
  const float Av = A[0] * a.v + A[1] * b.v + A2 * c.v, Bv = B[0] * a.v + B[1] * b.v + B2 * c.v,
              Cv = C[0] * a.v + C[1] * b.v + C2 * c.v;
  // The same small inclusive tolerance the per-pixel test had (in place of a
  // top-left rule).
  constexpr float kEps = 1e-5f;
  for (int y = minY; y <= maxY; ++y) {
    const float py = y + 0.5f;
    // Each weight >= -eps bounds the pixel centre on one side: the row's span.
    float lo = static_cast<float>(minX) + 0.5f, hi = static_cast<float>(maxX) + 0.5f;
    bool empty = false;
    for (int e = 0; e < 3 && !empty; ++e) {
      const float k = Be[e] * py + Ce[e] + kEps;
      if (Ae[e] > 0) {
        lo = std::max(lo, -k / Ae[e]);
      } else if (Ae[e] < 0) {
        hi = std::min(hi, -k / Ae[e]);
      } else if (k < 0) {
        empty = true;
      }
    }
    if (empty) continue;
    const int x0 = std::max(minX, static_cast<int>(std::ceil(lo - 0.5f)));
    const int x1 = std::min(maxX, static_cast<int>(std::floor(hi - 0.5f)));
    if (x0 > x1) continue;
    const float px = x0 + 0.5f;
    span(y, x0, x1, Au * px + Bu * py + Cu, Av * px + Bv * py + Cv, Au, Av, fill);
  }
}

void Raster::draw_mesh_strip(const void* coords, int vertex_count) {
  // gameswf draws a fill strip with its left style (the OpenGL handler's
  // LEFT_STYLE, side 0).
  const Fill& fill = fill_[0];
  if (!fill.enabled && maskMode_ != MaskMode::Writing) return;
  const auto* c = static_cast<const std::int16_t*>(coords);
  std::vector<Vertex> v(static_cast<std::size_t>(vertex_count));
  for (int i = 0; i < vertex_count; ++i) {
    const float ox = c[i * 2], oy = c[i * 2 + 1];
    const gameswf::point s = transform(matrix_, ox, oy);
    toPixels(s.m_x, s.m_y, &v[i].x, &v[i].y);
    if (fill.bitmap) {
      // The bitmap matrix maps the shape's coordinates to texels (the OpenGL
      // handler's texgen planes, before its division by the size).
      const gameswf::point t = transform(fill.map, ox, oy);
      v[i].u = t.m_x;
      v[i].v = t.m_y;
    } else {
      v[i].u = v[i].v = 0;
    }
  }
  for (int i = 0; i + 2 < vertex_count; ++i) triangle(v[i], v[i + 1], v[i + 2], fill);
}

void Raster::draw_line_strip(const void* coords, int vertex_count) {
  if (!line_.enabled || vertex_count < 2) return;
  const auto* c = static_cast<const std::int16_t*>(coords);
  // The width: twips at the stage's scale, at least a pixel. How the
  // original sizes lines is not measured.
  const float scale = static_cast<float>(width_) / (x1_ - x0_);
  const float half = std::max(lineWidth_ * scale, 1.0f) * 0.5f;
  for (int i = 0; i + 1 < vertex_count; ++i) {
    const gameswf::point p0 = transform(matrix_, c[i * 2], c[i * 2 + 1]);
    const gameswf::point p1 = transform(matrix_, c[i * 2 + 2], c[i * 2 + 3]);
    float ax, ay, bx, by;
    toPixels(p0.m_x, p0.m_y, &ax, &ay);
    toPixels(p1.m_x, p1.m_y, &bx, &by);
    // A line a pixel wide has its ends put on pixel centres: centred on a
    // pixel boundary, its quad passes exactly through a row of pixel centres,
    // and rounding let every other one fall out — the panels' edges came out
    // dashed. How SwiffRender itself places a thin line is not measured.
    if (half <= 0.5f) {
      ax = std::floor(ax) + 0.5f;
      ay = std::floor(ay) + 0.5f;
      bx = std::floor(bx) + 0.5f;
      by = std::floor(by) + 0.5f;
    }
    const float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) continue;
    const float nx = -dy / len * half, ny = dx / len * half;
    const Vertex q0{ax + nx, ay + ny, 0, 0}, q1{bx + nx, by + ny, 0, 0};
    const Vertex q2{ax - nx, ay - ny, 0, 0}, q3{bx - nx, by - ny, 0, 0};
    triangle(q0, q1, q2, line_);
    triangle(q1, q3, q2, line_);
  }
}

void Raster::draw_bitmap(const gameswf::matrix& m, const gameswf::bitmap_info* bi, const gameswf::rect& coords,
                         const gameswf::rect& uv, gameswf::rgba color) {
  const auto* im = static_cast<const Image*>(bi);
  if (im == nullptr) return;
  Fill f;
  f.enabled = true;
  f.bitmap = true;
  f.image = bi;
  f.wrap = false;
  // The colour multiplies the texels (glyphs: white × the text colour).
  f.transform = gameswf::cxform();
  f.transform.m_[0][0] = color.m_r / 255.0f;
  f.transform.m_[1][0] = color.m_g / 255.0f;
  f.transform.m_[2][0] = color.m_b / 255.0f;
  f.transform.m_[3][0] = color.m_a / 255.0f;
  prepare(f);
  const auto corner = [&](float x, float y, float u, float v) {
    const gameswf::point s = transform(m, x, y);
    Vertex out;
    toPixels(s.m_x, s.m_y, &out.x, &out.y);
    out.u = u * im->w;
    out.v = v * im->h;
    return out;
  };
  const Vertex a = corner(coords.m_x_min, coords.m_y_min, uv.m_x_min, uv.m_y_min);
  const Vertex b = corner(coords.m_x_max, coords.m_y_min, uv.m_x_max, uv.m_y_min);
  const Vertex c = corner(coords.m_x_min, coords.m_y_max, uv.m_x_min, uv.m_y_max);
  const Vertex d = corner(coords.m_x_max, coords.m_y_max, uv.m_x_max, uv.m_y_max);
  triangle(a, b, c, f);
  triangle(b, d, c, f);
}

void Raster::begin_submit_mask() {
  std::fill(mask_.begin(), mask_.end(), 0);
  maskMode_ = MaskMode::Writing;
}

void Raster::end_submit_mask() { maskMode_ = MaskMode::Testing; }

void Raster::disable_mask() { maskMode_ = MaskMode::Off; }

}  // namespace obf2::swf
