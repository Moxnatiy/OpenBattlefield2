// The mod's `<music>` and the stream that plays it (app/menu_music.h,
// docs/functions/menu-music.md).
//
// Runs on SDL's dummy audio driver, so it needs no sound card. The `.ogg` half
// needs the game's own music, which is not in the repository: pass the game's
// folder (the one holding `mods/bf2`) as the first argument to run it; without
// one it says so and passes.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "check.h"
#include "obf2/app/menu_music.h"
#include "obf2/engine/engine.h"

namespace {

using namespace obf2;

std::filesystem::path scratch() {
  const auto dir = std::filesystem::temp_directory_path() / "obf2_test_menu_music";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void write(const std::filesystem::path& file, const std::string& text) {
  std::ofstream(file, std::ios::binary) << text;
}

void put16(std::string& out, std::uint16_t value) {
  out.push_back(static_cast<char>(value & 0xff));
  out.push_back(static_cast<char>(value >> 8));
}

void put32(std::string& out, std::uint32_t value) {
  put16(out, static_cast<std::uint16_t>(value & 0xffff));
  put16(out, static_cast<std::uint16_t>(value >> 16));
}

// A tenth of a second of 16-bit mono silence at 22050 Hz, as a RIFF file.
std::string silentWav() {
  constexpr std::uint32_t rate = 22050, samples = rate / 10;
  std::string wav = "RIFF";
  put32(wav, 36 + samples * 2);
  wav += "WAVEfmt ";
  put32(wav, 16);
  put16(wav, 1);  // PCM
  put16(wav, 1);  // mono
  put32(wav, rate);
  put32(wav, rate * 2);
  put16(wav, 2);
  put16(wav, 16);
  wav += "data";
  put32(wav, samples * 2);
  wav.append(samples * 2, '\0');
  return wav;
}

// mod.desc's <music>, trimmed as the retail file needs it: the tag's text has a
// space on each side (`<music> common/sound/menu/music/menu_music.ogg </music>`).
void testModMusic() {
  const auto dir = scratch();
  write(dir / "mod.desc",
        "<mod>\n\t<title> MODDESC_BF2_TITLE </title>\n"
        "\t<music> common/sound/menu/music/menu_music.ogg </music> \n</mod>\n");
  FileSystem files;
  CHECK(files.mountDirectory(dir));
  CHECK_EQ(engine::readModMusic(files), std::string("common/sound/menu/music/menu_music.ogg"));

  FileSystem empty;
  CHECK(engine::readModMusic(empty).empty());
  std::filesystem::remove_all(dir);
}

void testWavPlaysOnce() {
  const auto dir = scratch();
  write(dir / "beep.wav", silentWav());
  write(dir / "broken.wav", "RIFF....not a wave");
  FileSystem files;
  CHECK(files.mountDirectory(dir));

  app::MenuMusic music;
  music.open(files, "beep.wav", 0.5f, false);
  CHECK(music.active());
  music.update(0.25f);
  music.stop();
  CHECK(!music.active());

  music.open(files, "broken.wav", 1.0f, false);
  CHECK(!music.active());
  music.open(files, "missing.ogg", 1.0f, true);
  CHECK(!music.active());
  music.open(files, "", 1.0f, true);
  CHECK(!music.active());
  std::filesystem::remove_all(dir);
}

// The game's own menu music through the same path the mod's `<music>` names.
void testGameMusic(const std::filesystem::path& game) {
  const auto modDir = game / "mods" / "bf2";
  // Mounted the way the application mounts it (src/app/main.cpp).
  FileSystem files;
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += mountArchivesFromCon(files, modDir, modDir / list);
  }
  files.mountDirectory(modDir);
  const auto path = engine::readModMusic(files);
  std::printf("mod music: %s (%d archives)\n", path.c_str(), mounted);
  CHECK(!path.empty());
  CHECK(files.exists(path));

  app::MenuMusic music;
  music.open(files, path, 1.0f, true);
  CHECK(music.active());
  music.update(1.0f);
  music.stop();

  // A level's own <music>, read by the engine's scan of `Levels/`.
  engine::Engine engine;
  engine.boot(files, modDir, {});
  const auto& levels = engine.levels();
  const auto dalian = std::ranges::find_if(levels, [](const engine::LevelEntry& level) {
    return level.directory == "Dalian_plant";
  });
  CHECK(dalian != levels.end());
  if (dalian == levels.end()) return;
  std::printf("Dalian_plant music: %s\n", dalian->music.c_str());
  CHECK_EQ(dalian->music, std::string("common/sound/menu/music/load_CH_music.ogg"));
  music.open(files, dalian->music, 1.0f, false);
  CHECK(music.active());
  music.stop();
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
  testModMusic();
  testWavPlaysOnce();
  if (argc > 1) {
    testGameMusic(argv[1]);
  } else {
    std::printf("no game folder given: the .ogg half is skipped\n");
  }
  SDL_Quit();
  if (obf2test::g_failures) {
    std::fprintf(stderr, "\n%d checks failed\n", obf2test::g_failures);
    return 1;
  }
  std::puts("OK");
  return 0;
}
