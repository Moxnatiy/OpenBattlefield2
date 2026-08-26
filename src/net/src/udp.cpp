#include "obf2/net/udp.h"

#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
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

// Найбільший пакет, який приймає рушій: у `NetServer::_update` буфер
// читається шматками по 0x5c0 байтів.
constexpr std::size_t kMaxPacket = 0x5C0;

}  // namespace

UdpSocket::~UdpSocket() {
#if !defined(_WIN32)
  if (handle_ >= 0) ::close(handle_);
#endif
}

std::unique_ptr<UdpSocket> UdpSocket::connect(const std::string& host, std::uint16_t port,
                                              std::string* error) {
#if defined(_WIN32)
  if (error) *error = "UDP під Windows ще не піднято";
  return nullptr;
#else
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  addrinfo* found = nullptr;
  const std::string service = std::to_string(port);
  if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
    if (error) *error = "не знайдено адресу " + host;
    return nullptr;
  }

  const int handle = ::socket(found->ai_family, found->ai_socktype, found->ai_protocol);
  if (handle < 0) {
    ::freeaddrinfo(found);
    if (error) *error = "не вдалося відкрити сокет";
    return nullptr;
  }

  // `connect` на UDP лише запам'ятовує адресу — так можна слати без неї
  // і заодно отримувати помилки на кшталт «порт закритий».
  if (::connect(handle, found->ai_addr, found->ai_addrlen) != 0) {
    ::freeaddrinfo(found);
    ::close(handle);
    if (error) *error = "не вдалося прив'язатися до " + host;
    return nullptr;
  }
  ::freeaddrinfo(found);

  auto socket = std::unique_ptr<UdpSocket>(new UdpSocket());
  socket->handle_ = handle;
  socket->description_ = host + ":" + service;
  return socket;
#endif
}

bool UdpSocket::send(std::span<const std::byte> data) {
#if defined(_WIN32)
  (void)data;
  return false;
#else
  if (handle_ < 0 || data.empty()) return false;
  const ssize_t sent = ::send(handle_, data.data(), data.size(), 0);
  return sent == static_cast<ssize_t>(data.size());
#endif
}

std::optional<std::vector<std::byte>> UdpSocket::receive(int timeoutMs) {
#if defined(_WIN32)
  (void)timeoutMs;
  return std::nullopt;
#else
  if (handle_ < 0) return std::nullopt;

  pollfd waiting{};
  waiting.fd = handle_;
  waiting.events = POLLIN;
  if (::poll(&waiting, 1, timeoutMs) <= 0) return std::nullopt;

  std::vector<std::byte> buffer(kMaxPacket);
  const ssize_t got = ::recv(handle_, buffer.data(), buffer.size(), 0);
  if (got <= 0) return std::nullopt;
  buffer.resize(static_cast<std::size_t>(got));
  return buffer;
#endif
}

}  // namespace obf2::net
