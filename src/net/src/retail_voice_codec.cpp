#include "obf2/net/retail_voice.h"

#include <algorithm>

#include <speex/speex.h>

namespace obf2::net::bf2 {

struct RetailVoiceCodec::State {
  void* encoder = speex_encoder_init(&speex_nb_mode);
  void* decoder = speex_decoder_init(&speex_nb_mode);
  SpeexBits bits{};
  std::size_t frameBytes = 0;

  State() { speex_bits_init(&bits); }
  ~State() {
    speex_bits_destroy(&bits);
    if (decoder) speex_decoder_destroy(decoder);
    if (encoder) speex_encoder_destroy(encoder);
  }
};

RetailVoiceCodec::RetailVoiceCodec() : state_(std::make_unique<State>()) {}
RetailVoiceCodec::~RetailVoiceCodec() = default;

std::unique_ptr<RetailVoiceCodec> RetailVoiceCodec::create(int serverQuality) {
  // The server's level 1..5 is turned around first: `BF2.exe` 0x736d80 maps it
  // to 4..0 through its jump table at 0x736de8, and anything outside 1..5 to 3.
  // 0x65f080 then picks the Speex quality: 0 → 10, 1 → 7, 2 → 4, 3 → 2, 4 → 1.
  // So level 1 is quality 1, level 5 quality 10, and an unknown level is level 2's.
  constexpr std::array qualityByLevel{1, 2, 4, 7, 10};
  if (serverQuality < 1 || static_cast<std::size_t>(serverQuality) > qualityByLevel.size()) {
    serverQuality = 2;
  }
  auto codec = std::unique_ptr<RetailVoiceCodec>(new RetailVoiceCodec);
  auto& state = *codec->state_;
  if (!state.encoder || !state.decoder) return nullptr;

  int quality = qualityByLevel[serverQuality - 1];
  int bitrate = 0;
  int samples = 0;
  if (speex_encoder_ctl(state.encoder, SPEEX_SET_QUALITY, &quality) != 0 ||
      speex_encoder_ctl(state.encoder, SPEEX_GET_BITRATE, &bitrate) != 0 ||
      speex_encoder_ctl(state.encoder, SPEEX_GET_FRAME_SIZE, &samples) != 0 ||
      samples != static_cast<int>(kVoiceFrameSamples) || bitrate <= 0) {
    return nullptr;
  }

  // 0x68b804 asks for the bitrate (ctl 0x13, SPEEX_GET_BITRATE) and 0x68b809
  // divides it by 50 frames a second and then by eight, truncating, into the
  // frame size it keeps at 0x9fe9a0 (read back by 0x68b890).
  state.frameBytes = static_cast<std::size_t>(bitrate / 50 / 8);
  return state.frameBytes ? std::move(codec) : nullptr;
}

std::size_t RetailVoiceCodec::frameBytes() const { return state_->frameBytes; }

bool RetailVoiceCodec::encode(std::span<const std::int16_t, kVoiceFrameSamples> samples,
                              std::span<std::byte> encoded) {
  if (encoded.size() != frameBytes()) return false;

  // Speex's floating-point encoder takes the 16-bit samples as floats of the
  // same scale.
  std::array<float, kVoiceFrameSamples> pcm;
  std::copy(samples.begin(), samples.end(), pcm.begin());
  speex_bits_reset(&state_->bits);
  return speex_encode(state_->encoder, pcm.data(), &state_->bits) > 0 &&
         speex_bits_write(&state_->bits, reinterpret_cast<char*>(encoded.data()),
                          static_cast<int>(encoded.size())) == static_cast<int>(encoded.size());
}

bool RetailVoiceCodec::decode(std::span<const std::byte> encoded,
                              std::span<std::int16_t, kVoiceFrameSamples> samples) {
  if (encoded.size() != frameBytes()) return false;
  speex_bits_read_from(&state_->bits, reinterpret_cast<const char*>(encoded.data()),
                       static_cast<int>(encoded.size()));
  return speex_decode_int(state_->decoder, &state_->bits, samples.data()) == 0;
}

}  // namespace obf2::net::bf2
