#pragma once
// A video file decoded frame by frame: the stand-in for the engine's
// `dice.hfe.geom.Video` (`RendDX9.dll`, created by name at 0x101ba58e for a menu
// fill and by `FSMoviePlayer::load` for a startup movie).
//
// The original's Video runs on Bink's own clock: a frame is decoded when Bink
// says its time has come (`BinkWait`), so a movie plays at its own rate whatever
// the frame rate is. Here the caller says how long the movie has been playing and
// gets the frame that belongs to that moment.
//
// Only the picture. The movies carry sound (`binkaudio_dct`, 48 kHz stereo in
// EA.bik); the engine plays it (`videoInitSound` in FSMoviePlayer::load) and we do
// not yet.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace obf2::video {

class Video {
 public:
  Video();
  ~Video();
  Video(const Video&) = delete;
  Video& operator=(const Video&) = delete;

  // false when the file does not open or holds no video stream it can decode.
  bool open(const std::string& path);
  void close();
  bool isOpen() const;

  std::uint32_t width() const { return width_; }
  std::uint32_t height() const { return height_; }
  // From the container: frames over the rate. Zero when unknown.
  double seconds() const { return seconds_; }

  // Decode up to the frame shown at `seconds` from the start. True when the
  // picture changed. Past the last frame the last one stays.
  bool advanceTo(double seconds);
  // The current picture, width × height × 4, BGRA — the order our texture
  // loader takes (a DDS's).
  const std::vector<std::uint8_t>& bgra() const { return bgra_; }
  // Frames decoded so far.
  int framesDecoded() const { return frames_; }
  bool ended() const { return ended_; }
  // The file it was opened from (to open it again at its end).
  const std::string& path() const { return path_; }

 private:
  std::string path_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  double seconds_ = 0.0;
  std::vector<std::uint8_t> bgra_;
  int frames_ = 0;
  bool ended_ = false;
};

}  // namespace obf2::video
