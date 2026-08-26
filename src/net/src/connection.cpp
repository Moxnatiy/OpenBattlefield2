#include "obf2/net/connection.h"

namespace obf2::net {

bool LoopbackConnection::send(std::span<const std::byte> data) {
  if (peer_ == nullptr) return false;
  peer_->incoming_.emplace_back(data.begin(), data.end());
  ++sent_;
  return true;
}

std::optional<Packet> LoopbackConnection::receive() {
  if (incoming_.empty()) return std::nullopt;
  Packet packet = std::move(incoming_.front());
  incoming_.pop_front();
  ++received_;
  return packet;
}

void LoopbackConnection::close() {
  // Обидва кінці мають дізнатися про розрив, інакше другий чекав би вічно.
  if (peer_ != nullptr) peer_->peer_ = nullptr;
  peer_ = nullptr;
  incoming_.clear();
}

std::pair<std::unique_ptr<LoopbackConnection>, std::unique_ptr<LoopbackConnection>>
LoopbackConnection::createPair() {
  auto first = std::unique_ptr<LoopbackConnection>(new LoopbackConnection());
  auto second = std::unique_ptr<LoopbackConnection>(new LoopbackConnection());
  first->peer_ = second.get();
  second->peer_ = first.get();
  return {std::move(first), std::move(second)};
}

}  // namespace obf2::net
