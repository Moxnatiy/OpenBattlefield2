#include <array>
#include <chrono>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include "check.h"
#include "obf2/app/match_voice.h"
#include "obf2/net/retail_voice.h"
#include "obf2/session/remote_world.h"

namespace {
using namespace obf2;
namespace bf2 = net::bf2;
using namespace std::chrono_literals;

void setupClient(session::RemoteWorld& match, unsigned player, std::uint16_t port) {
  match.socket = net::UdpSocket::connect("127.0.0.1", port);
  match.databaseSentAt = std::chrono::steady_clock::now();
  match.serverMatchOptions = bf2::ServerMatchOptions{false, false, false, true, 1, port, 0, 3};
  match.voiceSession = 400 + player;
  match.world.setOwnConnection(player);
  bf2::Event first, second, role;
  first.player = bf2::CreatePlayer{1, 0, 1, 101, false, "Voice A"};
  second.player = bf2::CreatePlayer{2, 0, 2, 102, false, "Voice B"};
  role.commanderAssignment = bf2::Event::CommanderAssignment{player, static_cast<int>(player)};
  match.world.feedEvents({first, second, role});
}

void testVoiceRelay() {
  CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
  const int relay = ::socket(AF_INET, SOCK_DGRAM, 0);
  CHECK(relay >= 0);
  if (relay < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addressLength = sizeof(address);
  if (::bind(relay, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
      ::getsockname(relay, reinterpret_cast<sockaddr*>(&address), &addressLength)) {
    CHECK(false);
    ::close(relay);
    return;
  }

  {
    FileSystem files;
    session::RemoteWorld first({}, files), second({}, files);
    setupClient(first, 1, ntohs(address.sin_port));
    setupClient(second, 2, ntohs(address.sin_port));
    app::MatchVoice voiceA, voiceB;
    voiceA.update(first, false, false, true);
    voiceB.update(second, false, false, true);
    CHECK(voiceA.available() && voiceB.available());

    std::array<sockaddr_in, 2> peers{};
    std::array<std::byte, 1024> packet{};
    for (unsigned i = 0; i < peers.size(); ++i) {
      socklen_t length = sizeof(sockaddr_in);
      const auto size = ::recvfrom(relay, packet.data(), packet.size(), MSG_DONTWAIT,
                                   reinterpret_cast<sockaddr*>(&peers[i]), &length);
      CHECK(size == 3 && packet[0] == std::byte(401 + i - 256) &&
            packet[1] == std::byte{1} && packet[2] == std::byte{6});
    }

    std::array<bool, 2> relayed{};
    auto codec = bf2::RetailVoiceCodec::create(3);
    CHECK(codec);
    for (int attempt = 0; codec && attempt < 50 && !(relayed[0] && relayed[1]); ++attempt) {
      voiceA.update(first, false, true, true);
      voiceB.update(second, false, true, true);
      for (int budget = 0; budget < 16; ++budget) {
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        const auto size = ::recvfrom(relay, packet.data(), packet.size(), MSG_DONTWAIT,
                                     reinterpret_cast<sockaddr*>(&from), &length);
        if (size < 0) break;
        const auto frameBytes = codec->frameBytes();
        CHECK_EQ(size, static_cast<ssize_t>(frameBytes + 5));
        if (size != static_cast<ssize_t>(frameBytes + 5)) continue;
        const unsigned sender = from.sin_port == peers[0].sin_port ? 0 : 1;
        CHECK_EQ(from.sin_port, peers[sender].sin_port);
        CHECK(packet[frameBytes + 2] == std::byte(401 + sender - 256) &&
              packet[frameBytes + 3] == std::byte{1} && packet[frameBytes + 4] == std::byte{1});
        std::array<std::int16_t, bf2::kVoiceFrameSamples> pcm{};
        CHECK(codec->decode(std::span(packet).first(frameBytes), pcm));
        packet[frameBytes + 2] = std::byte(sender + 1);
        packet[frameBytes + 3] = std::byte{0};
        const auto& destination = peers[1 - sender];
        CHECK_EQ(::sendto(relay, packet.data(), frameBytes + 4, 0,
                          reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)),
                 static_cast<ssize_t>(frameBytes + 4));
        relayed[sender] = true;
      }
      std::this_thread::sleep_for(20ms);
    }
    CHECK(relayed[0] && relayed[1]);
    voiceA.update(first, false, false, true);
    voiceB.update(second, false, false, true);
    voiceA.stop(first);
    voiceB.stop(second);
  }
  SDL_Quit();
  ::close(relay);
}

}  // namespace

TEST_MAIN({ testVoiceRelay(); });
