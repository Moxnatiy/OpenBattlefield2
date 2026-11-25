#include "obf2/net/udp.h"

#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace obf2::net {
namespace {

// The largest packet the engine accepts: in `NetServer::_update` the buffer is
// read in chunks of 0x5c0 bytes.
constexpr std::size_t kMaxPacket = 0x5C0;

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
struct Winsock {
  WSADATA data{};
  int status = WSAStartup(MAKEWORD(2, 2), &data);
  ~Winsock() { if (status == 0) WSACleanup(); }
};
void closeSocket(SocketHandle handle) { ::closesocket(handle); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle handle) { ::close(handle); }
#endif

}  // namespace

UdpSocket::~UdpSocket() {
  if (handle_ != kInvalidSocket) closeSocket(handle_);
}

std::unique_ptr<UdpSocket> UdpSocket::connect(const std::string& host, std::uint16_t port,
                                              std::string* error) {
#if defined(_WIN32)
  static Winsock winsock;
  if (winsock.status != 0) {
    if (error) *error = "could not start Winsock";
    return nullptr;
  }
#endif
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  addrinfo* found = nullptr;
  const std::string service = std::to_string(port);
  if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
    if (error) *error = "could not resolve " + host;
    return nullptr;
  }

  const SocketHandle handle = ::socket(found->ai_family, found->ai_socktype, found->ai_protocol);
  if (handle == kInvalidSocket) {
    ::freeaddrinfo(found);
    if (error) *error = "could not open the socket";
    return nullptr;
  }

  // `connect` on UDP only remembers the address — that way we can send without it
  // and also receive errors such as "port closed".
#if defined(_WIN32)
  const int addressLength = static_cast<int>(found->ai_addrlen);  // size_t in Winsock's addrinfo
#else
  const socklen_t addressLength = found->ai_addrlen;
#endif
  if (::connect(handle, found->ai_addr, addressLength) != 0) {
    ::freeaddrinfo(found);
    closeSocket(handle);
    if (error) *error = "could not bind to " + host;
    return nullptr;
  }
  // The peer as a numeric address, for a second socket to the same host (the
  // voice relay, app/match_voice.cpp).
  char numericHost[INET_ADDRSTRLEN]{};
  ::getnameinfo(found->ai_addr, addressLength, numericHost, sizeof(numericHost), nullptr, 0,
                NI_NUMERICHOST);
  ::freeaddrinfo(found);

  auto socket = std::unique_ptr<UdpSocket>(new UdpSocket());
  socket->handle_ = handle;
  socket->description_ = host + ":" + service;
  if (*numericHost) socket->peerEndpoint_ = std::string(numericHost) + ":" + service;
  return socket;
}

bool UdpSocket::send(std::span<const std::byte> data) {
  // 65507: the largest UDP payload over IPv4 (65535 less the 20-byte IP and
  // 8-byte UDP headers, RFC 791 and RFC 768). Winsock's send takes an int.
  if (handle_ == kInvalidSocket || data.empty() || data.size() > 65507) return false;
#if defined(_WIN32)
  const int sent = ::send(handle_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
#else
  const ssize_t sent = ::send(handle_, data.data(), data.size(), 0);
#endif
  return sent >= 0 && static_cast<std::size_t>(sent) == data.size();
}

std::optional<std::vector<std::byte>> UdpSocket::receive(int timeoutMs) {
  if (handle_ == kInvalidSocket) return std::nullopt;
  std::vector<std::byte> buffer(kMaxPacket);
#if defined(_WIN32)
  WSAPOLLFD waiting{handle_, POLLRDNORM, 0};
  if (::WSAPoll(&waiting, 1, timeoutMs) <= 0 || !(waiting.revents & POLLRDNORM)) return std::nullopt;
  const int got = ::recv(handle_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
#else
  pollfd waiting{};
  waiting.fd = handle_;
  waiting.events = POLLIN;
  if (::poll(&waiting, 1, timeoutMs) <= 0) return std::nullopt;

  const ssize_t got = ::recv(handle_, buffer.data(), buffer.size(), 0);
#endif
  if (got <= 0) return std::nullopt;
  buffer.resize(static_cast<std::size_t>(got));
  return buffer;
}

}  // namespace obf2::net
