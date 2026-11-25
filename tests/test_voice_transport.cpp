// The game's voice datagrams and their Speex codec (net/retail_voice.h,
// docs/functions/voice-transport.md).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "check.h"
#include "obf2/net/retail_voice.h"

namespace {

using namespace obf2::net::bf2;

void testFraming() {
  const std::array<std::byte, 2> audio{std::byte{0xab}, std::byte{0xcd}};
  const std::array<std::byte, 3> keepAlive{std::byte{0x65}, std::byte{0x34}, std::byte{6}};
  CHECK(voiceKeepAlive(0x3465) == keepAlive);

  // The stamp goes behind the audio little-endian, then the route and the session.
  constexpr std::uint16_t stamp = 0xff02;
  const std::byte stampLow{0x02}, stampHigh{0xff};
  const auto squad = outgoingVoiceFrame(audio, stamp, 0x3465, VoiceRoute::Default);
  const auto command = outgoingVoiceFrame(audio, stamp, 0x3465, VoiceRoute::FirstAlternate);
  CHECK(squad == (std::vector<std::byte>{audio[0], audio[1], stampLow, stampHigh,
                                        keepAlive[0], keepAlive[1], std::byte{1}}));
  CHECK(command == (std::vector<std::byte>{audio[0], audio[1], stampLow, stampHigh,
                                          std::byte{1}, keepAlive[0], keepAlive[1], std::byte{2}}));

  // The relay's answer: the audio, the stamp, the speaker.
  const std::array<std::byte, 6> reply{audio[0], audio[1], stampLow, stampHigh,
                                       std::byte{7}, std::byte{0}};
  const auto incoming = readRelayedVoiceFrame(reply);
  CHECK(incoming && incoming->player == 7 && incoming->frameStamp == stamp &&
        std::ranges::equal(incoming->payload, audio));

  // Nothing but a trailer, more than the 0x400-byte receive buffer, a speaker
  // past one byte: none of them is a frame.
  CHECK(!readRelayedVoiceFrame(std::span(reply).last(4)));
  CHECK(!readRelayedVoiceFrame(std::vector<std::byte>(1025)));
  const std::array<std::byte, 6> wide{audio[0], audio[1], stampLow, stampHigh,
                                      std::byte{0}, std::byte{1}};
  CHECK(!readRelayedVoiceFrame(wide));

  CHECK(!outgoingVoiceFrame({}, stamp, 0x3465, VoiceRoute::Default));
  CHECK(!outgoingVoiceFrame(audio, stamp, 0, VoiceRoute::Default));
  CHECK(!outgoingVoiceFrame(std::vector<std::byte>(1021), stamp, 0x3465, VoiceRoute::Default));
  CHECK(outgoingVoiceFrame(std::vector<std::byte>(1020), stamp, 0x3465, VoiceRoute::Default));
}

void testCodec() {
  // The frame sizes the client's own arithmetic gives (BF2.exe 0x68b809: Speex's
  // narrowband bitrate / 50 / 8) at the qualities 0x736d80 and 0x65f080 pick for
  // the server's levels 1..5: 1, 2, 4, 7 and 10.
  constexpr std::array<std::size_t, 5> frameBytes{9, 14, 20, 37, 61};
  for (int level = 1; level <= 5; ++level) {
    const auto encoder = RetailVoiceCodec::create(level);
    const auto decoder = RetailVoiceCodec::create(level);
    CHECK(encoder && decoder);
    if (!encoder || !decoder) continue;
    CHECK_EQ(encoder->frameBytes(), frameBytes[level - 1]);

    std::vector<std::byte> payload(encoder->frameBytes());
    std::array<std::int16_t, kVoiceFrameSamples> pcm{}, received{};
    for (std::size_t i = 0; i < pcm.size(); ++i) {
      pcm[i] = static_cast<std::int16_t>(5000 * std::sin(i * 0.17));
    }
    // A few frames: the decoder's first ones are its own warm-up.
    for (int frame = 0; frame < 5; ++frame) {
      CHECK(encoder->encode(pcm, payload));
      CHECK(decoder->decode(payload, received));
    }
    CHECK(std::ranges::any_of(received, [](int sample) { return std::abs(sample) > 100; }));
    // A frame of the wrong size is refused both ways.
    CHECK(!decoder->decode(std::span(payload).subspan(1), received));
    std::vector<std::byte> longer(encoder->frameBytes() + 1);
    CHECK(!encoder->encode(pcm, longer));
  }

  // A level outside 1..5 is level 2's codec (0x736d80's default).
  for (const int level : {0, 6, -1}) {
    const auto codec = RetailVoiceCodec::create(level);
    CHECK(codec && codec->frameBytes() == frameBytes[1]);
  }
}

}  // namespace

TEST_MAIN({
  testFraming();
  testCodec();
})
