#pragma once

// The game's voice chat on the wire: the datagrams a client sends to the voice
// relay and the ones it gets back, and the Speex codec that fills them
// (docs/functions/voice-transport.md). Nothing here opens a socket or a device.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace obf2::net::bf2 {

// Which set of listeners a frame is for. `Default` is the squad channel;
// `FirstAlternate` is the command channel a squad leader talks to his
// commander on. Which roles may use which is the caller's business.
enum class VoiceRoute { Default, FirstAlternate };

// A frame the relay passed on: who spoke, the speaker's frame stamp, and the
// encoded audio (a whole number of codec frames).
struct RelayedVoiceFrame {
  std::uint16_t player;
  std::uint16_t frameStamp;
  std::span<const std::byte> payload;
};

// The relay learns the listener's UDP endpoint from this datagram; it carries
// nothing but the voice session and the keep-alive type.
std::array<std::byte, 3> voiceKeepAlive(std::uint16_t session);
// One outgoing datagram: the payload, the stamp, the route and the session.
// Empty for session 0, an empty payload or one too long for the receive buffer.
std::optional<std::vector<std::byte>> outgoingVoiceFrame(std::span<const std::byte> payload,
                                                        std::uint16_t frameStamp,
                                                        std::uint16_t session, VoiceRoute route);
// The relay's datagram taken apart, or empty when it cannot be one.
std::optional<RelayedVoiceFrame> readRelayedVoiceFrame(std::span<const std::byte> datagram);

// Speex's narrowband mode, which the codec is built on: 8 kHz.
inline constexpr int kVoiceSampleRate = 8000;
// The samples in one frame: `BF2.exe` 0x68b880 returns 0xa0.
inline constexpr std::size_t kVoiceFrameSamples = 160;

// The codec at one of the server's voice quality levels (1..5).
class RetailVoiceCodec {
 public:
  // Null when Speex refuses the mode or the quality.
  static std::unique_ptr<RetailVoiceCodec> create(int serverQuality);
  ~RetailVoiceCodec();

  RetailVoiceCodec(const RetailVoiceCodec&) = delete;
  RetailVoiceCodec& operator=(const RetailVoiceCodec&) = delete;

  // The encoded size of one frame, fixed by the quality.
  std::size_t frameBytes() const;
  // `encoded` must be exactly frameBytes() long.
  bool encode(std::span<const std::int16_t, kVoiceFrameSamples> samples, std::span<std::byte> encoded);
  bool decode(std::span<const std::byte> encoded, std::span<std::int16_t, kVoiceFrameSamples> samples);

 private:
  RetailVoiceCodec();
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace obf2::net::bf2
