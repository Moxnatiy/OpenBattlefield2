#include "obf2/video/video.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace obf2::video {

struct Video::Impl {
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  SwsContext* scale = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  int stream = -1;
  AVRational timeBase{1, 1};
  // The presentation time of the frame shown, and of the one decoded ahead.
  double shownAt = 0.0;
  double pendingAt = 0.0;
  bool pending = false;
  bool draining = false;
  bool gray = false;

  ~Impl() {
    if (scale != nullptr) sws_freeContext(scale);
    if (frame != nullptr) av_frame_free(&frame);
    if (packet != nullptr) av_packet_free(&packet);
    if (codec != nullptr) avcodec_free_context(&codec);
    if (format != nullptr) avformat_close_input(&format);
  }

  // The next decoded frame into `frame`; false at the end of the stream.
  bool decodeNext() {
    for (;;) {
      const int got = avcodec_receive_frame(codec, frame);
      if (got == 0) return true;
      if (got == AVERROR_EOF) return false;
      if (got != AVERROR(EAGAIN)) return false;
      if (draining) return false;
      if (av_read_frame(format, packet) < 0) {
        draining = true;
        avcodec_send_packet(codec, nullptr);
        continue;
      }
      if (packet->stream_index == stream) avcodec_send_packet(codec, packet);
      av_packet_unref(packet);
    }
  }
};

Video::Video() = default;
Video::~Video() = default;

bool Video::isOpen() const { return impl_ != nullptr; }

void Video::close() {
  impl_.reset();
  width_ = height_ = 0;
  seconds_ = 0.0;
  bgra_.clear();
  frames_ = 0;
  ended_ = false;
}

bool Video::open(const std::string& path) {
  const std::string opened = path;  // `path` may be our own path_, which close() keeps
  close();
  path_ = opened;
  auto impl = std::make_unique<Impl>();
  if (avformat_open_input(&impl->format, opened.c_str(), nullptr, nullptr) < 0) return false;
  if (avformat_find_stream_info(impl->format, nullptr) < 0) return false;
  const AVCodec* decoder = nullptr;
  impl->stream = av_find_best_stream(impl->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  if (impl->stream < 0 || decoder == nullptr) return false;
  const AVStream* stream = impl->format->streams[impl->stream];
  impl->codec = avcodec_alloc_context3(decoder);
  if (impl->codec == nullptr) return false;
  if (avcodec_parameters_to_context(impl->codec, stream->codecpar) < 0) return false;
  if (avcodec_open2(impl->codec, decoder, nullptr) < 0) return false;
  impl->packet = av_packet_alloc();
  impl->frame = av_frame_alloc();
  if (impl->packet == nullptr || impl->frame == nullptr) return false;
  impl->timeBase = stream->time_base;

  width_ = static_cast<std::uint32_t>(impl->codec->width);
  height_ = static_cast<std::uint32_t>(impl->codec->height);
  if (width_ == 0 || height_ == 0) return false;
  // A grey Bink codes no chroma at all: flag 0x20000 of the video flags at
  // +0x24 of the file's header (`mods/bf2/Movies/menu.bik` carries it; the
  // demuxer hands those four bytes on as the stream's extradata). FFmpeg's
  // decoder leaves the chroma planes at zero, which converts to green, so
  // such a video is converted from its luma plane alone.
  impl->gray = impl->codec->extradata != nullptr && impl->codec->extradata_size >= 4 &&
               (impl->codec->extradata[2] & 0x02) != 0;
  impl->scale = sws_getContext(impl->codec->width, impl->codec->height,
                               impl->gray ? AV_PIX_FMT_GRAY8 : impl->codec->pix_fmt,
                               impl->codec->width, impl->codec->height, AV_PIX_FMT_BGRA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (impl->scale == nullptr) return false;
  if (stream->duration > 0) {
    seconds_ = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
  }
  bgra_.assign(static_cast<std::size_t>(width_) * height_ * 4, 0);
  impl_ = std::move(impl);
  return true;
}

bool Video::advanceTo(double seconds) {
  if (!impl_ || ended_) return false;
  bool changed = false;
  // `frame` holds the next decoded frame, not yet shown (`pending`). Show it
  // once its time has come and decode the one after — Bink's rule: a frame is
  // never shown early. The first frame is shown at once.
  for (;;) {
    if (!impl_->pending) {
      if (!impl_->decodeNext()) {
        ended_ = true;
        break;
      }
      impl_->pending = true;
      const std::int64_t pts = impl_->frame->best_effort_timestamp;
      impl_->pendingAt = pts == AV_NOPTS_VALUE
                             ? impl_->shownAt
                             : static_cast<double>(pts) * av_q2d(impl_->timeBase);
    }
    if (frames_ > 0 && impl_->pendingAt > seconds) break;
    std::uint8_t* planes[4] = {bgra_.data(), nullptr, nullptr, nullptr};
    int strides[4] = {static_cast<int>(width_ * 4), 0, 0, 0};
    const std::uint8_t* const luma[4] = {impl_->frame->data[0], nullptr, nullptr, nullptr};
    const int lumaStride[4] = {impl_->frame->linesize[0], 0, 0, 0};
    sws_scale(impl_->scale, impl_->gray ? luma : impl_->frame->data,
              impl_->gray ? lumaStride : impl_->frame->linesize, 0, static_cast<int>(height_), planes, strides);
    av_frame_unref(impl_->frame);
    impl_->pending = false;
    impl_->shownAt = impl_->pendingAt;
    ++frames_;
    changed = true;
  }
  return changed;
}

}  // namespace obf2::video
