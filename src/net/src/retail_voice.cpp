#include "obf2/net/retail_voice.h"

namespace obf2::net::bf2 {
namespace {

// The client receives a voice datagram into a 0x400-byte buffer
// (`BF2.exe` 0x45148d), so nothing longer can be one.
constexpr std::size_t kMaxClientDatagram = 1024;
// What the relay puts behind the audio: the stamp, then the speaker
// (0x45165b reads the speaker from the last two bytes, 0x45168a the stamp from
// the two before them).
constexpr std::size_t kRelayedTrailerSize = 4;
// The routing bytes behind an outgoing frame's stamp: the frame type (1 on the
// default set, 2 on the alternate), the alternate set's own byte, and the
// keep-alive's type. Where the client writes them is not established — the
// stamp is appended at 0x45155b and the datagram handed to the socket object's
// slot 0x6c (0x45157f), which is not opened.
constexpr std::byte kDefaultChannelFrame{1};
constexpr std::byte kAlternateChannelFrame{2};
constexpr std::byte kAlternateSet{1};
constexpr std::byte kKeepAlive{6};

std::byte lowByte(std::uint16_t value) {
  return static_cast<std::byte>(value & 0xff);
}

std::byte highByte(std::uint16_t value) {
  return static_cast<std::byte>(value >> 8);
}

std::uint16_t word(std::span<const std::byte, 2> bytes) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[0]) |
                                    (std::to_integer<unsigned>(bytes[1]) << 8));
}

}  // namespace

std::array<std::byte, 3> voiceKeepAlive(std::uint16_t session) {
  return {lowByte(session), highByte(session), kKeepAlive};
}

std::optional<std::vector<std::byte>> outgoingVoiceFrame(std::span<const std::byte> payload,
                                                        std::uint16_t frameStamp,
                                                        std::uint16_t session, VoiceRoute route) {
  if (session == 0 || payload.empty() || payload.size() > kMaxClientDatagram - kRelayedTrailerSize) {
    return std::nullopt;
  }
  const bool alternate = route == VoiceRoute::FirstAlternate;
  std::vector<std::byte> datagram(payload.begin(), payload.end());
  // The stamp goes straight behind the audio, little-endian (0x45155b).
  datagram.push_back(lowByte(frameStamp));
  datagram.push_back(highByte(frameStamp));
  if (alternate) datagram.push_back(kAlternateSet);
  datagram.push_back(lowByte(session));
  datagram.push_back(highByte(session));
  datagram.push_back(alternate ? kAlternateChannelFrame : kDefaultChannelFrame);
  return datagram;
}

std::optional<RelayedVoiceFrame> readRelayedVoiceFrame(std::span<const std::byte> datagram) {
  if (datagram.size() <= kRelayedTrailerSize || datagram.size() > kMaxClientDatagram) {
    return std::nullopt;
  }
  // The speaker from the last two bytes (0x45165b), the stamp from the two
  // before (0x45168a); what is left is the audio.
  const auto trailer = datagram.last<kRelayedTrailerSize>();
  const auto player = word(trailer.last<2>());
  // A player id is one byte everywhere else in the protocol.
  if (player > 255) return std::nullopt;
  return RelayedVoiceFrame{player, word(trailer.first<2>()),
                           datagram.first(datagram.size() - kRelayedTrailerSize)};
}

}  // namespace obf2::net::bf2
