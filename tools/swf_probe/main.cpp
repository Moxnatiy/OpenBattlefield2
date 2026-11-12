// swf_probe — play one of the game's Flash movies on our gameswf and count what
// it asks for.
//
//   swf_probe [movie] [frames] [--mod <dir>] [--log]
//
// The movie is read the way the engine reads it: the mod's archives mounted from
// ServerArchives.con / ClientArchives.con, the path through our file system
// (default `Menu/External/FlashMenu/mainMenu.swf`). The renderer draws nothing;
// it counts every call, which is the list `obf2::gfx` has to answer later. The
// library's own complaints are counted by text, the most frequent first — the
// work list for the player, the same way `--hud-vars` is for the HUD.
//
// It runs on a thread with a large stack: an ActionScript loop in the movie
// recurses, and the report must survive to say where.
#include <pthread.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "base/tu_file.h"
#include "gameswf/gameswf.h"
#include "gameswf/gameswf_impl.h"
#include "gameswf/gameswf_types.h"
#include "obf2/vfs/filesystem.h"

namespace {

obf2::FileSystem* g_files = nullptr;
std::vector<std::vector<std::byte>> g_buffers;  // kept alive for gameswf's readers
std::map<std::string, int> g_messages;
std::map<std::string, int> g_opened;
bool g_printLog = false;

tu_file* openFile(const char* path) {
  std::string name = path;
  for (char& c : name) {
    if (c == '\\') c = '/';
  }
  auto bytes = g_files->read(name);
  if (!bytes) bytes = g_files->read("Menu/External/FlashMenu/" + name);
  g_opened[name] += bytes ? 1 : 0;
  if (!bytes) {
    g_messages["file not found: " + name]++;
    return nullptr;
  }
  g_buffers.push_back(std::move(*bytes));
  auto& kept = g_buffers.back();
  return new tu_file(tu_file::memory_buffer, static_cast<int>(kept.size()), kept.data());
}

void logLine(bool error, const char* message) {
  std::string text = message;
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  if (g_printLog) std::printf("%s %s\n", error ? "[error]" : "[log]", text.c_str());
  // The count goes by the text with its numbers blanked, so one message with
  // different addresses is counted once.
  std::string key;
  for (char c : text) key += (c >= '0' && c <= '9') ? '#' : c;
  if (error) g_messages[key]++;
}

struct Counts {
  long beginDisplay = 0, matrices = 0, cxforms = 0, meshStrips = 0, meshVertices = 0;
  long lineStrips = 0, colourFills = 0, bitmapFills = 0, bitmaps = 0, masks = 0;
  long bitmapsMade = 0;
};

struct CountingRenderer : gameswf::render_handler {
  Counts counts;
  gameswf::bitmap_info* create_bitmap_info_empty() override { return made(); }
  gameswf::bitmap_info* create_bitmap_info_alpha(int, int, unsigned char*) override { return made(); }
  gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb*) override { return made(); }
  gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba*) override { return made(); }
  void delete_bitmap_info(gameswf::bitmap_info* bi) override { delete bi; }
  void begin_display(gameswf::rgba, int, int, int, int, float, float, float, float) override {
    ++counts.beginDisplay;
  }
  void end_display() override {}
  void set_matrix(const gameswf::matrix&) override { ++counts.matrices; }
  void set_cxform(const gameswf::cxform&) override { ++counts.cxforms; }
  void draw_mesh_strip(const void*, int vertexCount) override {
    ++counts.meshStrips;
    counts.meshVertices += vertexCount;
  }
  void draw_line_strip(const void*, int) override { ++counts.lineStrips; }
  void fill_style_disable(int) override {}
  void fill_style_color(int, gameswf::rgba) override { ++counts.colourFills; }
  void fill_style_bitmap(int, const gameswf::bitmap_info*, const gameswf::matrix&,
                         bitmap_wrap_mode) override {
    ++counts.bitmapFills;
  }
  void line_style_disable() override {}
  void line_style_color(gameswf::rgba) override {}
  void line_style_width(float) override {}
  void draw_bitmap(const gameswf::matrix&, const gameswf::bitmap_info*, const gameswf::rect&,
                   const gameswf::rect&, gameswf::rgba) override {
    ++counts.bitmaps;
  }
  void set_antialiased(bool) override {}
  void begin_submit_mask() override { ++counts.masks; }
  void end_submit_mask() override {}
  void disable_mask() override {}

 private:
  gameswf::bitmap_info* made() {
    ++counts.bitmapsMade;
    return new gameswf::bitmap_info;
  }
};

struct Job {
  std::string movie;
  int frames = 30;
  int framesPlayed = 0;
  int width = 0, height = 0, frameCount = 0;
  bool opened = false;
  CountingRenderer renderer;
};

void* run(void* arg) {
  Job& job = *static_cast<Job*>(arg);
  gameswf::register_file_opener_callback(openFile);
  gameswf::register_log_callback(logLine);
  gameswf::set_render_handler(&job.renderer);

  gameswf::movie_definition* definition = gameswf::create_movie(job.movie.c_str());
  if (definition == nullptr) return nullptr;
  job.opened = true;
  job.width = static_cast<int>(definition->get_width_pixels());
  job.height = static_cast<int>(definition->get_height_pixels());
  job.frameCount = definition->get_frame_count();
  // Which frames carry init actions (DoInitAction) and how many tags each frame has.
  if (auto* sub = dynamic_cast<gameswf::movie_definition_sub*>(definition)) {
    for (int f = 0; f < job.frameCount && f < 4; ++f) {
      const auto* init = sub->get_init_actions(f);
      std::printf("frame %d: %d init actions, %d tags\n", f, init ? init->size() : 0,
                  sub->get_playlist(f).size());
    }
  }
  gameswf::movie_interface* movie = definition->create_instance();
  if (movie == nullptr) return nullptr;
  for (int i = 0; i < job.frames; ++i) {
    movie->advance(1.0f / 30.0f);
    movie->display();
    job.framesPlayed = i + 1;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  Job job;
  job.movie = "Menu/External/FlashMenu/mainMenu.swf";
  std::filesystem::path modDir = "Game Files/mods/bf2";
  int positional = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--mod" && i + 1 < argc) modDir = argv[++i];
    else if (arg == "--log") g_printLog = true;
    else if (positional == 0 && ++positional) job.movie = arg;
    else if (positional == 1 && ++positional) job.frames = std::atoi(arg.c_str());
  }

  obf2::FileSystem files;
  std::vector<std::string> errors;
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += obf2::mountArchivesFromCon(files, modDir, modDir / list, &errors);
  }
  files.mountDirectory(modDir);
  g_files = &files;
  std::printf("swf_probe: %s, %d archives\n", job.movie.c_str(), mounted);

  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  pthread_attr_setstacksize(&attributes, 512u * 1024u * 1024u);
  pthread_t thread;
  pthread_create(&thread, &attributes, run, &job);
  pthread_join(thread, nullptr);

  if (!job.opened) {
    std::printf("the movie did not open\n");
    return 1;
  }
  const Counts& c = job.renderer.counts;
  std::printf("stage %d x %d, %d frames; played %d of %d asked\n", job.width, job.height,
              job.frameCount, job.framesPlayed, job.frames);
  std::printf("display %ld | matrices %ld cxforms %ld | mesh strips %ld (%ld vertices) |"
              " line strips %ld\n",
              c.beginDisplay, c.matrices, c.cxforms, c.meshStrips, c.meshVertices, c.lineStrips);
  std::printf("fills: colour %ld bitmap %ld | bitmaps drawn %ld made %ld | masks %ld\n",
              c.colourFills, c.bitmapFills, c.bitmaps, c.bitmapsMade, c.masks);
  std::printf("files opened:\n");
  for (const auto& [name, ok] : g_opened) std::printf("  %s %s\n", ok ? "  " : "!!", name.c_str());
  std::vector<std::pair<int, std::string>> sorted;
  for (const auto& [text, count] : g_messages) sorted.emplace_back(count, text);
  std::sort(sorted.rbegin(), sorted.rend());
  std::printf("library errors (%zu kinds), most frequent first:\n", sorted.size());
  for (std::size_t i = 0; i < sorted.size() && i < 40; ++i) {
    std::printf("  %6d  %s\n", sorted[i].first, sorted[i].second.c_str());
  }
  return 0;
}
