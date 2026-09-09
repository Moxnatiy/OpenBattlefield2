#pragma once
// The channel between the client and the server.
//
// In Battlefield 2 **a single-player game is a client and a server too**: the
// engine brings up a local server and connects to it. So the channel is
// abstract: for a single-player game it is an in-memory loop, for a network UDP.
// The game's logic is the same, and that is the main reason the server side cannot be deferred.
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace obf2::net {

using Packet = std::vector<std::byte>;

class Connection {
 public:
  virtual ~Connection() = default;

  virtual bool send(std::span<const std::byte> data) = 0;
  // nullopt means nothing arrived; that is not an error.
  virtual std::optional<Packet> receive() = 0;
  virtual bool connected() const = 0;
  virtual void close() = 0;

  virtual std::string_view describe() const = 0;
};

// An in-memory loop: two ends joined by queues. The packets are not serialised
// twice and do not go through a socket, but the format is the same as on the
// network — otherwise a single-player game would exercise code other than the multiplayer one.
class LoopbackConnection : public Connection {
 public:
  bool send(std::span<const std::byte> data) override;
  std::optional<Packet> receive() override;
  bool connected() const override { return peer_ != nullptr; }
  void close() override;
  std::string_view describe() const override { return "loopback"; }

  std::size_t pending() const { return incoming_.size(); }
  long long sentPackets() const { return sent_; }
  long long receivedPackets() const { return received_; }

  // Creates a pair of joined ends: [client, server].
  static std::pair<std::unique_ptr<LoopbackConnection>, std::unique_ptr<LoopbackConnection>>
  createPair();

 private:
  LoopbackConnection* peer_ = nullptr;
  std::deque<Packet> incoming_;
  long long sent_ = 0;
  long long received_ = 0;
};

}  // namespace obf2::net
