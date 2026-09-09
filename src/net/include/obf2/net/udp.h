#pragma once
// A UDP channel to a real server.
//
// Until now the network worked only as an in-memory loop. Here is an ordinary
// socket, so that we can talk to an original BF2 server.
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace obf2::net {

class UdpSocket {
 public:
  ~UdpSocket();

  // Opens the socket and remembers where to send. The host is an address or a name.
  static std::unique_ptr<UdpSocket> connect(const std::string& host, std::uint16_t port,
                                            std::string* error = nullptr);

  bool send(std::span<const std::byte> data);

  // Waits for a packet no longer than timeoutMs. nullopt means nothing arrived.
  std::optional<std::vector<std::byte>> receive(int timeoutMs);

  const std::string& describe() const { return description_; }

 private:
  int handle_ = -1;
  std::string description_;
};

}  // namespace obf2::net
