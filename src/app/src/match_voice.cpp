#include "obf2/app/match_voice.h"

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "obf2/net/retail_voice.h"
#include "obf2/session/remote_world.h"

// The voice client of a live round (docs/functions/voice-transport.md and
// voip-mute.md): the relay socket the server's match options name, push to talk
// on B (squad) and V (squad leader to commander), Speex frames out, the relayed
// frames of everyone we have not muted decoded onto one SDL stream each.
namespace obf2::app {
namespace {
namespace bf2 = net::bf2;

using Clock = std::chrono::steady_clock;
constexpr SDL_AudioSpec kVoiceAudio{SDL_AUDIO_S16, 1, bf2::kVoiceSampleRate};
constexpr std::size_t kPcmFrameBytes = bf2::kVoiceFrameSamples * sizeof(std::int16_t);
// One frame's length in milliseconds, the step of the outgoing stamp:
// 160 samples at 8000 Hz (retail_voice.h).
constexpr int kFrameMs = static_cast<int>(bf2::kVoiceFrameSamples * 1000 / bf2::kVoiceSampleRate);
// How often the keep-alive goes to the relay: **not measured**.
constexpr auto kKeepAliveEvery = std::chrono::seconds(2);
// A speaker silent this long loses his stream: ours, **not measured**.
constexpr auto kSpeakerTimeout = std::chrono::seconds(5);
// Frames captured and datagrams read per frame at most, and the queued playback
// past which a stream is cleared (ten frames): ours, **not measured**.
constexpr int kCaptureBudget = 8;
constexpr int kReceiveBudget = 32;
constexpr std::size_t kPlaybackBacklogFrames = 10;

struct CloseStream {
  void operator()(SDL_AudioStream* stream) const { SDL_DestroyAudioStream(stream); }
};
using AudioStream = std::unique_ptr<SDL_AudioStream, CloseStream>;

std::string voiceHost(std::uint32_t address, const net::UdpSocket& gameSocket) {
  // An address of 1 in the server's match options is taken as "the game
  // server's own host" — inferred from live servers, not read in the binary.
  if (address == 1) {
    const auto& peer = gameSocket.peerEndpoint();
    return peer.substr(0, peer.rfind(':'));
  }
  if (!address) return {};
  return std::to_string(address & 255) + "." + std::to_string((address >> 8) & 255) + "." +
         std::to_string((address >> 16) & 255) + "." + std::to_string(address >> 24);
}

// `VoipOnOffEvent` (35): the player in 8 bits and whether his voice is on in 1
// (`VoipOnOffEvent::serialize`, Linux 0x435090).
void announce(session::RemoteWorld& match, bool enabled) {
  const int player = match.world.ownPlayer();
  if (player < 0 || player > 255 || !match.socket) return;
  bf2::EventBits event;
  event.put(35, bf2::kEventTypeBits).put(static_cast<unsigned>(player), 8).put(enabled, 1);
  match.link.queue(std::move(event));
}

}  // namespace

struct MatchVoice::State {
  struct Speaker {
    std::unique_ptr<bf2::RetailVoiceCodec> codec;
    AudioStream audio;
    std::optional<std::uint16_t> nextStamp;
    Clock::time_point lastPacket{};
  };

  std::unique_ptr<net::UdpSocket> relay;
  std::unique_ptr<bf2::RetailVoiceCodec> encoder;
  AudioStream capture;
  std::map<unsigned, Speaker> speakers;
  std::string host;
  std::uint16_t port = 0;
  std::uint16_t session = 0;
  int quality = 0;
  Clock::time_point keepAliveAt{};
  Clock::time_point startedAt{};
  std::uint16_t nextSendStamp = 0;
  bool capturing = false;
  bool talkHeld = false;
};

MatchVoice::MatchVoice() = default;
MatchVoice::~MatchVoice() = default;

bool MatchVoice::available() const { return state_ && state_->relay != nullptr; }

void MatchVoice::stop(session::RemoteWorld& match) {
  if (available()) announce(match, false);
  state_.reset();
}

void MatchVoice::update(session::RemoteWorld& match, bool squadTalk, bool commandTalk, bool focused) {
  const auto& settings = match.serverMatchOptions;
  if (!match.socket || !match.databaseSentAt || match.world.ownPlayer() < 0 ||
      !settings || !settings->voice || !settings->voiceServerPort || !match.voiceSession ||
      *match.voiceSession == 0) {
    if (state_) stop(match);
    return;
  }
  const auto host = voiceHost(settings->voiceAddress, *match.socket);
  if (host.empty()) return;
  const auto now = Clock::now();
  if (!state_ || state_->host != host || state_->port != settings->voiceServerPort ||
      state_->session != *match.voiceSession || state_->quality != settings->voiceQuality) {
    stop(match);
    state_ = std::make_unique<State>();
    auto& voice = *state_;
    voice.host = host;
    voice.port = settings->voiceServerPort;
    voice.session = static_cast<std::uint16_t>(*match.voiceSession);
    voice.quality = settings->voiceQuality;
    voice.startedAt = now;
    voice.encoder = bf2::RetailVoiceCodec::create(voice.quality);
    if (!voice.encoder) return;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
    voice.relay = net::UdpSocket::connect(host, voice.port);
    if (voice.relay) announce(match, true);
  }

  auto& voice = *state_;
  if (!voice.relay) return;
  if (now >= voice.keepAliveAt) {
    voice.relay->send(bf2::voiceKeepAlive(voice.session));
    voice.keepAliveAt = now + kKeepAliveEvery;
  }

  const auto local = match.world.players().find(static_cast<unsigned>(match.world.ownPlayer()));
  const bool commander = local != match.world.players().end() && local->second.commander.value_or(false);
  const bool squadMember = local != match.world.players().end() && local->second.squad.value_or(0) != 0;
  const bool leader = squadMember && local->second.squadLeader;
  const bool talking = focused && !match.endOfRound &&
      ((squadTalk && squadMember) || (commandTalk && (leader || commander)));
  const auto route = commandTalk && leader && !commander
      ? bf2::VoiceRoute::FirstAlternate : bf2::VoiceRoute::Default;
  if (talking && !voice.talkHeld && !voice.capture) {
    voice.capture.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
                                                    &kVoiceAudio, nullptr, nullptr));
    if (!voice.capture) SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
  }
  voice.talkHeld = talking;
  if (voice.capture && talking != voice.capturing) {
    SDL_ClearAudioStream(voice.capture.get());
    if (talking) {
      voice.capturing = SDL_ResumeAudioStreamDevice(voice.capture.get());
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - voice.startedAt);
      voice.nextSendStamp = static_cast<std::uint16_t>(elapsed.count() / kFrameMs);
    } else {
      SDL_PauseAudioStreamDevice(voice.capture.get());
      voice.capturing = false;
    }
  }

  std::array<std::int16_t, bf2::kVoiceFrameSamples> pcm{};
  if (voice.capturing) {
    std::vector<std::byte> payload(voice.encoder->frameBytes());
    for (int budget = 0; budget < kCaptureBudget &&
         SDL_GetAudioStreamAvailable(voice.capture.get()) >= static_cast<int>(kPcmFrameBytes); ++budget) {
      if (SDL_GetAudioStreamData(voice.capture.get(), pcm.data(), kPcmFrameBytes) !=
          static_cast<int>(kPcmFrameBytes)) break;
      const auto stamp = voice.nextSendStamp++;
      if (voice.encoder->encode(pcm, payload)) {
        if (const auto packet = bf2::outgoingVoiceFrame(payload, stamp, voice.session, route)) {
          voice.relay->send(*packet);
        }
      }
    }
  }

  if (!focused) voice.speakers.clear();
  std::erase_if(voice.speakers, [now](const auto& entry) {
    return now - entry.second.lastPacket > kSpeakerTimeout;
  });
  for (int budget = 0; budget < kReceiveBudget; ++budget) {
    const auto packet = voice.relay->receive(0);
    if (!packet) break;
    const auto frame = bf2::readRelayedVoiceFrame(*packet);
    const auto frameBytes = voice.encoder->frameBytes();
    if (!focused || !frame || frame->player == static_cast<unsigned>(match.world.ownPlayer()) ||
        !match.world.players().contains(frame->player) || frame->payload.size() % frameBytes != 0) continue;
    // A muted player: what is already queued of him stops with his stream.
    if (match.muted(frame->player)) {
      voice.speakers.erase(frame->player);
      continue;
    }

    match.noteVoice(frame->player);
    auto& speaker = voice.speakers[frame->player];
    if (!speaker.audio) {
      speaker.codec = bf2::RetailVoiceCodec::create(voice.quality);
      speaker.audio.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                      &kVoiceAudio, nullptr, nullptr));
      if (!speaker.audio || !speaker.codec || !SDL_ResumeAudioStreamDevice(speaker.audio.get())) {
        voice.speakers.erase(frame->player);
        continue;
      }
    }
    speaker.lastPacket = now;
    auto stamp = frame->frameStamp;
    for (std::size_t at = 0; at < frame->payload.size(); at += frameBytes, ++stamp) {
      if (speaker.nextStamp && static_cast<std::uint16_t>(stamp - *speaker.nextStamp) >= 0x8000) continue;
      speaker.nextStamp = static_cast<std::uint16_t>(stamp + 1);
      if (speaker.codec->decode(frame->payload.subspan(at, frameBytes), pcm)) {
        if (SDL_GetAudioStreamQueued(speaker.audio.get()) > static_cast<int>(kPcmFrameBytes * kPlaybackBacklogFrames)) {
          SDL_ClearAudioStream(speaker.audio.get());
        }
        SDL_PutAudioStreamData(speaker.audio.get(), pcm.data(), kPcmFrameBytes);
      }
    }
  }
}

}  // namespace obf2::app
