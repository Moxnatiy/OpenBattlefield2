#pragma once
// gameswf's draw calls painted into an RGBA image.
//
// The original draws the menu with Direct3D through `SwiffRender`
// (RendDX9.dll): triangle strips gameswf tessellates, coloured or bitmap-filled
// through the colour transform, masks through the stencil. This does the same
// on the CPU into the frame the application shows as a texture — the menu is a
// few hundred triangles at 1024 × 768. What the original's states are
// (filtering, blending, antialiasing) is not measured; here: alpha blending,
// bilinear sampling, no antialiasing.
#include <cstdint>
#include <vector>

#include "gameswf/gameswf.h"
#include "gameswf/gameswf_types.h"

namespace obf2::swf {

class Raster : public gameswf::render_handler {
 public:
  void resize(int width, int height);
  int width() const { return width_; }
  int height() const { return height_; }
  // RGBA, top row first.
  const std::vector<std::uint8_t>& pixels() const { return pixels_; }

  // A picture from RGBA texels (loadMovie's pictures and video frames).
  gameswf::bitmap_info* makeImage(int width, int height, const std::uint8_t* rgba);
  // Replace a picture's texels, same size (a video's next frame).
  static void updateImage(gameswf::bitmap_info* image, const std::uint8_t* rgba, bool bgra);

  // render_handler
  gameswf::bitmap_info* create_bitmap_info_empty() override;
  gameswf::bitmap_info* create_bitmap_info_alpha(int w, int h, unsigned char* data) override;
  gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb* im) override;
  gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba* im) override;
  void delete_bitmap_info(gameswf::bitmap_info* bi) override;
  void begin_display(gameswf::rgba background, int vx, int vy, int vw, int vh, float x0, float x1,
                     float y0, float y1) override;
  void end_display() override {}
  void set_matrix(const gameswf::matrix& m) override { matrix_ = m; }
  void set_cxform(const gameswf::cxform& cx) override { cxform_ = cx; }
  void draw_mesh_strip(const void* coords, int vertex_count) override;
  void draw_line_strip(const void* coords, int vertex_count) override;
  void fill_style_disable(int side) override;
  void fill_style_color(int side, gameswf::rgba color) override;
  void fill_style_bitmap(int side, const gameswf::bitmap_info* bi, const gameswf::matrix& m,
                         bitmap_wrap_mode wm) override;
  void line_style_disable() override { line_.enabled = false; }
  void line_style_color(gameswf::rgba color) override;
  void line_style_width(float width) override { lineWidth_ = width; }
  void draw_bitmap(const gameswf::matrix& m, const gameswf::bitmap_info* bi, const gameswf::rect& coords,
                   const gameswf::rect& uv, gameswf::rgba color) override;
  void set_antialiased(bool) override {}
  void begin_submit_mask() override;
  void end_submit_mask() override;
  void disable_mask() override;

 private:
  struct Fill {
    bool enabled = false;
    bool bitmap = false;
    float color[4] = {0, 0, 0, 0};  // after the colour transform, 0..1
    const gameswf::bitmap_info* image = nullptr;
    gameswf::matrix map;           // object twips → texture texels
    bool wrap = true;
    gameswf::cxform transform;     // for bitmap texels
    // The same in integers, made by `prepare`: the solid colour 0..255; for a
    // bitmap the transform's multipliers × 256 and its additions 0..255.
    std::uint8_t rgba[4] = {0, 0, 0, 0};
    int mul[4] = {256, 256, 256, 256};
    int add[4] = {0, 0, 0, 0};
    bool identity = true;   // the bitmap's transform changes nothing
    bool invisible = false; // nothing it paints can show (alpha 0)
  };
  static void prepare(Fill& fill);
  struct Vertex {
    float x, y;  // pixels
    float u, v;  // texels (bitmap fills)
  };

  void toPixels(float twipsX, float twipsY, float* px, float* py) const;
  void triangle(const Vertex& a, const Vertex& b, const Vertex& c, const Fill& fill);
  void span(int y, int x0, int x1, float u, float v, float du, float dv, const Fill& fill);

  int width_ = 0, height_ = 0;
  std::vector<std::uint8_t> pixels_;
  std::vector<std::uint8_t> mask_;
  enum class MaskMode { Off, Writing, Testing } maskMode_ = MaskMode::Off;
  float x0_ = 0, x1_ = 1, y0_ = 0, y1_ = 1;  // stage twips mapped to the image
  gameswf::matrix matrix_;
  gameswf::cxform cxform_;
  Fill fill_[2];
  Fill line_;
  float lineWidth_ = 1.0f;
};

}  // namespace obf2::swf
