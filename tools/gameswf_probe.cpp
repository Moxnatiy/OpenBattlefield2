// A gameswf trial: will it read and will it draw the real BF2 menu.
//
//   gameswf_probe <file.swf>              the header only
//   gameswf_probe <file.swf> <frames>     also play it and count the calls
//
// Building it is described in docs/research/10-gameswf-on-arm64.md; before
// that, `tools/gameswf_macos.patch` has to be applied to `reference/gameswf`.
//
// This is **not** our renderer. Here `render_handler` draws nothing — it only
// counts what is asked of it. That very list is what has to land on
// `obf2::gfx` later, so first we need to see what it is made of on a real
// movie.
#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gameswf/gameswf.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_movie_def.h"
#include "base/tu_file.h"
#include "gameswf_bridge.h"

namespace {

// gameswf asks us for bitmaps; for counting, an empty stub that remembers
// only the size is enough.
struct CountedBitmap : public gameswf::bitmap_info {
  int width = 0;
  int height = 0;
  int get_width() const override { return width; }
  int get_height() const override { return height; }
};

struct Counters {
  int bitmapsEmpty = 0, bitmapsAlpha = 0, bitmapsRgb = 0, bitmapsRgba = 0;
  int beginDisplay = 0, endDisplay = 0;
  int setMatrix = 0, setCxform = 0;
  int meshStrips = 0, meshVertices = 0;
  int triangleLists = 0, triangleVertices = 0;
  int lineStrips = 0, lineVertices = 0;
  int fillColor = 0, fillBitmap = 0, fillDisable = 0;
  int lineColor = 0, lineWidth = 0, lineDisable = 0;
  int bitmapQuads = 0;
  int masksBegin = 0, masksEnd = 0;
  float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  gameswf::rgba background;
};

// A renderer that counts rather than draws.
struct CountingRenderer : public gameswf::render_handler {
  Counters& n;
  explicit CountingRenderer(Counters& counters) : n(counters) {}

  gameswf::bitmap_info* create_bitmap_info_empty() override {
    ++n.bitmapsEmpty;
    return new CountedBitmap();
  }
  gameswf::bitmap_info* create_bitmap_info_alpha(int w, int h, unsigned char*) override {
    ++n.bitmapsAlpha;
    CountedBitmap* bitmap = new CountedBitmap();
    bitmap->width = w;
    bitmap->height = h;
    return bitmap;
  }
  gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb* im) override {
    ++n.bitmapsRgb;
    CountedBitmap* bitmap = new CountedBitmap();
    if (im != nullptr) { bitmap->width = im->m_width; bitmap->height = im->m_height; }
    return bitmap;
  }
  gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba* im) override {
    ++n.bitmapsRgba;
    CountedBitmap* bitmap = new CountedBitmap();
    if (im != nullptr) { bitmap->width = im->m_width; bitmap->height = im->m_height; }
    return bitmap;
  }
  gameswf::video_handler* create_video_handler() override { return nullptr; }

  void begin_display(gameswf::rgba background, int, int, int, int,
                     float x0, float x1, float y0, float y1) override {
    ++n.beginDisplay;
    n.background = background;
    n.x0 = x0; n.x1 = x1; n.y0 = y0; n.y1 = y1;
  }
  void end_display() override { ++n.endDisplay; }

  void set_matrix(const gameswf::matrix&) override { ++n.setMatrix; }
  void set_cxform(const gameswf::cxform&) override { ++n.setCxform; }

  void draw_mesh_strip(const void*, int vertexCount) override {
    ++n.meshStrips;
    n.meshVertices += vertexCount;
  }
  void draw_triangle_list(const void*, int vertexCount) override {
    ++n.triangleLists;
    n.triangleVertices += vertexCount;
  }
  void draw_line_strip(const void*, int vertexCount) override {
    ++n.lineStrips;
    n.lineVertices += vertexCount;
  }

  void fill_style_disable(int) override { ++n.fillDisable; }
  void fill_style_color(int, const gameswf::rgba&) override { ++n.fillColor; }
  void fill_style_bitmap(int, gameswf::bitmap_info*, const gameswf::matrix&,
                         bitmap_wrap_mode, bitmap_blend_mode) override { ++n.fillBitmap; }

  void line_style_disable() override { ++n.lineDisable; }
  void line_style_color(gameswf::rgba) override { ++n.lineColor; }
  void line_style_width(float) override { ++n.lineWidth; }

  void draw_bitmap(const gameswf::matrix&, gameswf::bitmap_info*, const gameswf::rect&,
                   const gameswf::rect&, gameswf::rgba) override { ++n.bitmapQuads; }
  void set_antialiased(bool) override {}

  bool test_stencil_buffer(const gameswf::rect&, Uint8) override { return false; }
  void begin_submit_mask() override { ++n.masksBegin; }
  void end_submit_mask() override { ++n.masksEnd; }
  void disable_mask() override {}

  bool is_visible(const gameswf::rect&) override { return true; }
  void open() override {}
};

tu_file* openFile(const char* url) { return new tu_file(url, "rb"); }

}  // namespace

namespace {

struct Args {
  int count;
  char** values;
  int result;
};

void* run(void* raw);

}  // namespace

// The main macOS thread has an 8 MB stack, and for a large movie that is not
// enough: the ActionScript interpreter in gameswf is recursive. So we do all
// the work in a separate thread with a large stack — that way it is visible
// whether the recursion is merely deep or endless.
int main(int argc, char** argv) {
  Args args{argc, argv, 0};
  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  const char* wanted = std::getenv("GAMESWF_STACK_MB");
  const size_t megabytes = wanted != nullptr ? size_t(std::atoi(wanted)) : 256;
  pthread_attr_setstacksize(&attributes, megabytes * 1024 * 1024);
  pthread_t thread;
  if (pthread_create(&thread, &attributes, run, &args) != 0) return run(&args) == nullptr ? 1 : 0;
  pthread_join(thread, nullptr);
  return args.result;
}

namespace {

void* run(void* raw) {
  Args& a = *static_cast<Args*>(raw);
  const int argc = a.count;
  char** argv = a.values;
  if (argc < 2) {
    std::printf("usage: gameswf_probe <file.swf> [frames]\n");
    a.result = 2;
    return nullptr;
  }
  const int frames = argc > 2 ? std::atoi(argv[2]) : 0;

  gameswf::register_file_opener_callback(openFile);

  Counters counters;
  CountingRenderer renderer(counters);
  gameswf::set_render_handler(&renderer);

  gameswf::player player;
  gameswf::movie_definition* movie = player.create_movie(argv[1]);
  if (movie == nullptr) {
    std::printf("did not read: %s\n", argv[1]);
    a.result = 1;
    return nullptr;
  }
  std::printf("version %d, frames %d, rate %.2f, stage %d x %d\n",
              movie->get_version(), movie->get_frame_count(), movie->get_frame_rate(),
              int(movie->get_width_pixels()), int(movie->get_height_pixels()));
  if (frames <= 0) return nullptr;

  // We put the bridge in **before** creating the root: the first frame's
  // actions already ask for it. See tools/gameswf_bridge.h.
  openbf2::installBridge(&player);

  auto root = player.load_file(argv[1]);
  if (root == NULL) {
    std::printf("could not create the root\n");
    a.result = 1;
    return nullptr;
  }
  // Once more — now after the root is created: `load_file` sets up an action
  // environment of its own, and the global object may not stay the same.
  openbf2::installBridge(&player);

  root->set_display_viewport(0, 0, int(movie->get_width_pixels()),
                             int(movie->get_height_pixels()));

  const float step = 1.0f / (movie->get_frame_rate() > 0 ? movie->get_frame_rate() : 30.0f);
  for (int i = 0; i < frames; ++i) {
    root->advance(step);
    root->display();
    // After the first frame we look at what the movie managed to put into
    // `_global`: its own class packages should appear there.
    if (i == 0 && std::getenv("GAMESWF_DUMP_GLOBAL") != nullptr) {
      gameswf::as_object* global = player.get_global();
      gameswf::as_value value;
      std::printf("--- _global.dice: %s\n",
                  global->get_member("dice", &value) ? "yes" : "NO");
      if (value.to_object() != nullptr) value.to_object()->dump();
    }
  }

  std::printf("\nframes played: %d (step %.4f s)\n", frames, step);
  std::printf("  begin_display / end_display   %d / %d\n",
              counters.beginDisplay, counters.endDisplay);
  std::printf("  window                        %.1f..%.1f x %.1f..%.1f\n",
              counters.x0, counters.x1, counters.y0, counters.y1);
  std::printf("  background                    %d %d %d %d\n",
              counters.background.m_r, counters.background.m_g,
              counters.background.m_b, counters.background.m_a);
  std::printf("  set_matrix / set_cxform       %d / %d\n",
              counters.setMatrix, counters.setCxform);
  std::printf("  mesh strips                   %d (%d vertices)\n",
              counters.meshStrips, counters.meshVertices);
  std::printf("  triangle lists                %d (%d vertices)\n",
              counters.triangleLists, counters.triangleVertices);
  std::printf("  line strips                   %d (%d vertices)\n",
              counters.lineStrips, counters.lineVertices);
  std::printf("  fill: colour / bitmap / off   %d / %d / %d\n",
              counters.fillColor, counters.fillBitmap, counters.fillDisable);
  std::printf("  line: colour / width / off    %d / %d / %d\n",
              counters.lineColor, counters.lineWidth, counters.lineDisable);
  std::printf("  separate bitmap quads         %d\n", counters.bitmapQuads);
  std::printf("  masks (begin / end)           %d / %d\n",
              counters.masksBegin, counters.masksEnd);
  std::printf("  bitmaps created: empty %d, alpha %d, rgb %d, rgba %d\n",
              counters.bitmapsEmpty, counters.bitmapsAlpha,
              counters.bitmapsRgb, counters.bitmapsRgba);

  const auto& calls = openbf2::bridgeCalls();
  std::printf("\nthe movie called the bridge: %d distinct names\n", int(calls.size()));
  for (const auto& pair : calls) {
    std::printf("  %-44s %d\n", pair.first.c_str(), pair.second);
  }
  return nullptr;
}

}  // namespace
