// The level's reverb (app/environment_audio.h) and our weapon's sounds through it
// (app/combat_feedback.h); docs/functions/environment-audio.md.
//
// Runs on SDL's dummy audio driver and OpenAL Soft's loopback device, so it needs
// no sound card. The preset below is Gulf of Oman's (`City.eax`, the numbers of
// `Common_client.zip:Sound/LevelAmbients/City.eax`).
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "check.h"
#include "obf2/app/combat_feedback.h"
#include "obf2/app/environment_audio.h"

namespace {

using namespace obf2;

std::filesystem::path scratch() {
  const auto dir = std::filesystem::temp_directory_path() / "obf2_test_environment_audio";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "Levels" / "Test");
  std::filesystem::create_directories(dir / "Sound");
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

// A tenth of a second of 16-bit mono at 22050 Hz with one full-scale click at
// its start, as a RIFF file.
std::string clickWav() {
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
  put16(wav, 0x7fff);
  wav.append((samples - 1) * 2, '\0');
  return wav;
}

const char* kCity =
    "EAX ENVIRONMENT PROPERTIES\r\ndwEnvironment\r\n26.\r\nflEnvironmentSize\r\n4.897637844\r\n"
    "flEnvironmentDiffusion\r\n0.236220479\r\nlRoom\r\n-629.\r\nlRoomHF\r\n-3779.\r\nlRoomLF\r\n-236.\r\n"
    "flDecayTime\r\n1.333559155\r\nflDecayHFRatio\r\n0.159842521\r\nflDecayLFRatio\r\n1.31181097\r\n"
    "lReflections\r\n133.\r\nflReflectionsDelay\r\n0.1535433084\r\nvReflectionsPan\r\n0.\r\n0.\r\n0.\r\n"
    "lReverb\r\n-1118.\r\nflReverbDelay\r\n2.379763313e-002\r\nvReverbPan\r\n0.\r\n0.\r\n0.\r\n"
    "flEchoTime\r\n7.500000298e-002\r\nflEchoDepth\r\n0.259842515\r\nflModulationTime\r\n3.999999911e-002\r\n"
    "flModulationDepth\r\n0.2047244161\r\nflAirAbsorptionHF\r\n-5.37007904\r\nflHFReference\r\n9377.953125\r\n"
    "flLFReference\r\n89.44882202\r\nflRoomRolloffFactor\r\n10.\r\ndwFlags\r\n255.\r\n";

void testReverb() {
  const auto dir = scratch();
  write(dir / "Levels" / "Test" / "Sounds.con", "sound.setReverb \"sound/city.eax\"\n");
  write(dir / "Sound" / "City.eax", kCity);
  write(dir / "Levels" / "Bad" / "Sounds.con", "");
  write(dir / "click.wav", clickWav());
  FileSystem files;
  CHECK(files.mountDirectory(dir));

  app::EnvironmentAudio none;
  CHECK(!none.load(files, "Missing"));
  CHECK(!none.ready());

  app::EnvironmentAudio reverb;
  CHECK(reverb.load(files, "Test"));
  CHECK(reverb.ready());
  // The click through the reverb: stereo at the output rate, as long as the
  // input plus the 1.33 s decay, and not silent after the click.
  const SDL_AudioSpec input{SDL_AUDIO_S16, 1, 22050};
  const std::string wav = clickWav();
  const std::span<const Uint8> pcm(reinterpret_cast<const Uint8*>(wav.data()) + 44, wav.size() - 44);
  const auto wet = reverb.render(input, pcm, 1.0f);
  const std::size_t frames = wet.size() / 4;
  CHECK(frames >= static_cast<std::size_t>(1.3 * app::EnvironmentAudio::outputFormat.freq));
  int tail = 0;
  for (std::size_t i = frames / 4; i < frames / 2; ++i) {
    const auto* sample = reinterpret_cast<const Sint16*>(wet.data()) + i * 2;
    tail = std::max(tail, std::abs(static_cast<int>(sample[0])));
  }
  CHECK(tail > 0);
  // No send, no wet signal.
  CHECK(reverb.render(input, pcm, 0.0f).empty());

  // A sample played through it, on the dummy driver.
  app::CombatFeedback sounds;
  sounds.prepareAudio();
  sounds.setEnvironment(&reverb);
  CHECK(sounds.play(files, "click.wav", 1.0f, 1.0f));
  CHECK(!sounds.play(files, "missing.wav", 1.0f, 1.0f));
  CHECK_EQ(sounds.played(), 1);
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
  testReverb();
  SDL_Quit();
  if (obf2test::g_failures) {
    std::fprintf(stderr, "\n%d checks failed\n", obf2test::g_failures);
    return 1;
  }
  std::puts("OK");
  return 0;
}
