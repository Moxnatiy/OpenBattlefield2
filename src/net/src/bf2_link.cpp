#include "obf2/net/bf2_link.h"

#include <bit>

namespace obf2::net::bf2 {

bool Link::acceptData(const ExtendedHeader& header) {
  // `handleDataPacket` (OpenBF2 NetClient.cpp): the same number as the last, or
  // one more than 31 behind, is dropped before anything else.
  const unsigned local = header.sequence & 0x3F;
  const unsigned gap = (local - remoteCounter_) & 0x3F;
  if (local == remoteCounter_ || gap > 0x1F) return false;
  confirm(header.ack & 0x3F, header.ackBits);
  // The mask: the packets skipped are zeros, this one a one.
  if (gap > 1) receiveMask_ <<= gap - 1;
  receiveMask_ = (receiveMask_ << 1) | 1u;
  remoteCounter_ = local;
  return true;
}

void Link::acceptPing(const ExtendedHeader& header, bool haveSent) {
  if (haveSent) {
    // `pingConfirmation`: the ping's number counts as received, but as a gap —
    // the mask takes a zero for it, not a one.
    const unsigned local = header.sequence & 0x3F;
    const unsigned gap = (local - remoteCounter_) & 0x3F;
    if (local != remoteCounter_ && gap <= 0x1F) {
      if (gap > 1) receiveMask_ <<= gap - 1;
      receiveMask_ <<= 1;
      remoteCounter_ = local;
    }
  }
  confirm(header.ack & 0x3F, header.ackBits);
}

std::vector<std::vector<Event>> Link::receiveBlock(unsigned index, std::vector<Event> events) {
  std::vector<std::vector<Event>> ready;
  index &= 31;
  const unsigned ahead = (index - (nextReceive_ & 31)) & 31;
  if (ahead >= 16 || pending_.count(index) != 0) {
    ++blocksRepeated_;
    return ready;
  }
  if (ahead > 0) ++blocksOutOfOrder_;
  pending_[index] = std::move(events);
  for (;;) {
    const auto found = pending_.find(nextReceive_ & 31);
    if (found == pending_.end()) break;
    ready.push_back(std::move(found->second));
    pending_.erase(found);
    ++nextReceive_;
  }
  return ready;
}

ExtendedHeader Link::nextDataHeader() {
  ExtendedHeader header;
  header.sequence = static_cast<std::uint8_t>(nextSequence_ & 0x3F);
  header.ack = static_cast<std::uint8_t>(remoteCounter_ & 0x3F);
  header.ackBits = receiveMask_;
  lastSentSequence_ = static_cast<int>(nextSequence_ & 0x3F);
  ++nextSequence_;
  return header;
}

ExtendedHeader Link::pingHeader() const {
  ExtendedHeader header;
  header.sequence = static_cast<std::uint8_t>(lastSentSequence_ < 0 ? 63 : lastSentSequence_);
  header.ack = static_cast<std::uint8_t>(remoteCounter_ & 0x3F);
  header.ackBits = receiveMask_;
  return header;
}

bool Link::canSend() const {
  // `SWConnection::send`: no more than 30 ahead of the acknowledged.
  return ((nextSequence_ - ackHorizon_) & 0x3F) <= kSendWindow;
}

bool Link::hasEventsToSend() const {
  return !resend_.empty() || (!sendQueue_.empty() && unconfirmedBlocks() < kEventWindow);
}

void Link::writeEvents(BitWriter& writer) {
  Sent sent;
  bool resend = false;
  if (!resend_.empty()) {
    // The oldest lost block, whole, under its own number.
    sent.block = std::move(resend_.front());
    resend_.pop_front();
    resend = true;
  } else if (!sendQueue_.empty() && unconfirmedBlocks() < kEventWindow) {
    // Every queued event, up to the count's 255.
    sent.block.index = nextSendBlock_ & 31;
    while (!sendQueue_.empty() && sent.block.events.size() < 255) {
      sent.block.events.push_back(std::move(sendQueue_.front()));
      sendQueue_.pop_front();
    }
    ++nextSendBlock_;
  }
  sent.block.sentAt = std::chrono::steady_clock::now();
  sent.block.sequence = (nextSequence_ - 1) & 0x3F;
  if (sent.block.events.empty()) {
    writer.writeBits(0, 1);  // no events
  } else {
    sent.hasBlock = true;
    writer.writeBits(1, 1);
    writer.writeBits(static_cast<std::uint32_t>(sent.block.events.size()), 8);
    writer.writeBits(static_cast<std::uint32_t>(sent.block.index), 5);
    writer.writeBits(resend ? 1u : 0u, 1);
    for (const EventBits& event : sent.block.events) event.write(writer);
    confirmed_[sent.block.index] = false;  // in flight again
    if (resend) {
      ++blocksResent_;
    } else {
      ++blocksSent_;
    }
  }
  inFlight_.push_back(std::move(sent));
}

void Link::confirm(unsigned ack, std::uint32_t mask) {
  // `confirmPacket`: the slots between the last acknowledged and this one, the
  // oldest first; a stale acknowledgement changes nothing.
  const unsigned advance = (ack - ackHorizon_) & 0x3F;
  if (advance > 0x1F) return;
  ackHorizon_ = ack;
  for (unsigned bit = advance; bit-- > 0;) delivered(((1u << bit) & mask) != 0);
}

void Link::delivered(bool ok) {
  if (inFlight_.empty()) return;
  Sent sent = std::move(inFlight_.front());
  inFlight_.pop_front();
  if (!ok) ++packetsLost_;
  if (!sent.hasBlock) return;
  if (onBlockSettled) {
    onBlockSettled(sent.block.index, sent.block.sequence, ok,
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                             sent.block.sentAt)
                       .count(),
                   sent.block.events.size());
  }
  // `handlePacketStatus`.
  if (ok) {
    confirmed_[sent.block.index] = true;
    while (advanceConfirmed()) {
    }
  } else {
    resend_.push_back(std::move(sent.block));
  }
}

bool Link::advanceConfirmed() {
  const int next = (lastConfirmedBlock_ + 1) & 31;
  if (next == (nextSendBlock_ & 31)) return false;  // never overtake
  if (!confirmed_[next]) return false;
  confirmed_[next] = false;
  lastConfirmedBlock_ = next;
  return true;
}

EventBits postRemoteEvent(std::uint32_t category, std::uint32_t number,
                          std::optional<std::int32_t> value) {
  EventBits bits;
  bits.put(kPostRemoteEvent, kEventTypeBits).put(category, 4).put(number, 32).put(0, 32);
  if (value) {
    bits.put(4, 8);
    const auto raw = static_cast<std::uint32_t>(*value);
    for (int i = 0; i < 4; ++i) bits.put((raw >> (i * 8)) & 0xFF, 8);
  } else {
    bits.put(0, 8);
  }
  return bits;
}

EventBits connectionTypeEvent(std::uint32_t type) {
  EventBits bits;
  bits.put(kConnectionTypeEvent, kEventTypeBits).put(type & 0x7u, 3);
  return bits;
}

EventBits contentCheckEvent(const std::array<std::byte, 16>& misc,
                            const std::array<std::byte, 16>& archives,
                            const std::array<std::byte, 16>& level) {
  EventBits bits;
  bits.put(kContentCheckEvent, kEventTypeBits);
  for (const auto* hash : {&misc, &archives, &level}) {
    for (const auto byte : *hash) bits.put(std::to_integer<std::uint32_t>(byte), 8);
  }
  return bits;
}

EventBits challengeResponseEvent(const ChallengeResponse& response) {
  EventBits bits;
  bits.put(2, kEventTypeBits);
  for (const char byte : response) bits.put(static_cast<std::uint8_t>(byte), 8);
  bits.put(0, 32).put(kGameVersion, 32).put(0, 1).put(0x423, 31);
  return bits;
}

EventBits voteEvent(const Vote& vote) {
  EventBits bits;
  bits.put(39, kEventTypeBits).put(vote.item, 8).put(vote.option, 8)
      .put(vote.forCount, 8).put(vote.item, 8).put(vote.againstCount, 8)
      .put(vote.needed, 8).put(vote.caster, 8).put(vote.status, 8)
      .put(vote.type, 8).put(vote.time, 32);
  return bits;
}

EventBits dataBlockHeaderEvent(std::uint32_t blockType, std::uint32_t size) {
  EventBits bits;
  bits.put(kDataBlockEvent, kEventTypeBits).put(1, 1).put(blockType, 32).put(size, 32);
  return bits;
}

EventBits dataBlockChunkEvent(std::span<const std::byte> chunk) {
  EventBits bits;
  bits.put(kDataBlockEvent, kEventTypeBits).put(0, 1).put(static_cast<std::uint32_t>(chunk.size()), 8);
  for (const auto byte : chunk) bits.put(std::to_integer<std::uint32_t>(byte), 8);
  return bits;
}

EventBits joinSquadEvent(int team, int player, int squad) {
  EventBits bits;
  bits.put(16, kEventTypeBits)
      .put(static_cast<std::uint32_t>(team) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(player) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(squad) & 0xFF, 8);
  return bits;
}

EventBits leaveSquadEvent(int team, int player, int squad, bool clearInvite) {
  EventBits bits;
  bits.put(17, kEventTypeBits)
      .put(static_cast<std::uint32_t>(team) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(player) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(squad) & 0xFF, 8)
      .put(clearInvite ? 1u : 0u, 1);
  return bits;
}

EventBits changeSquadNameEvent(int team, int squad, int player, std::string_view name) {
  EventBits bits;
  const std::size_t length = name.size() > 255 ? 255 : name.size();
  bits.put(22, kEventTypeBits)
      .put(static_cast<std::uint32_t>(squad) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(team) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(player) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(length), 8);
  for (std::size_t i = 0; i < length; ++i) bits.put(static_cast<std::uint8_t>(name[i]), 8);
  return bits;
}

EventBits setPrivateSquadEvent(int team, int squad, bool isPrivate) {
  EventBits bits;
  bits.put(23, kEventTypeBits)
      .put(static_cast<std::uint32_t>(squad) & 0xFF, 8)
      .put(static_cast<std::uint32_t>(team) & 0xFF, 8)
      .put(isPrivate ? 1u : 0u, 1);
  return bits;
}

std::vector<EventBits> chatEvents(ChatChannel channel, unsigned sender, std::string_view text) {
  if (text.empty() || text.size() > kMaxChatBytes ||
      text.find_first_of(std::string_view("\0\r\n", 3)) != std::string_view::npos) return {};
  // StringBlockEvent carries the text followed by a 12-byte StringManager header.
  std::string bytes(text);
  bytes.append(12, '\0');
  bytes[text.size()] = static_cast<char>(channel);
  bytes[text.size() + 4] = 2;  // player text
  bytes[text.size() + 8] = static_cast<char>(sender);
  bytes[text.size() + 9] = static_cast<char>(0xFF);  // no direct recipient
  std::vector<EventBits> events;
  events.emplace_back().put(15, kEventTypeBits).put(1, 1).put(static_cast<std::uint32_t>(bytes.size()), 8);
  for (std::size_t start = 0; start < bytes.size(); start += 63) {
    const auto chunk = std::string_view(bytes).substr(start, 63);
    auto& event = events.emplace_back();
    event.put(15, kEventTypeBits).put(0, 1).put(static_cast<std::uint32_t>(chunk.size()), 8);
    for (unsigned char byte : chunk) event.put(byte, 8);
  }
  return events;
}

EventBits radioCallEvent(const RadioCall& call) {
  EventBits bits;
  bits.put(20, kEventTypeBits).put(call.message, 8).put(call.sender, 8)
      .put(call.team, 2).put(call.extra, 8).put(call.channel, 5).put(call.type, 3);
  return bits;
}

EventBits spotEvent(unsigned spotter, unsigned target, unsigned team, Vec3f point, unsigned hudEvent) {
  EventBits bits;
  bits.put(29, kEventTypeBits).put(spotter, 16).put(target, 16)
      .put(std::bit_cast<std::uint32_t>(point.x), 32)
      .put(std::bit_cast<std::uint32_t>(point.z), 32).put(team, 8)
      .put(26, 32).put(hudEvent, 32);  // VTNoVehicleType, ECHud spotted
  return bits;
}

EventBits inviteSquadEvent(unsigned team, unsigned player, unsigned squad, bool invited) {
  EventBits bits;
  bits.put(25, kEventTypeBits).put(team, 8).put(player, 8).put(squad, 8).put(invited, 1);
  return bits;
}

EventBits squadOrderEvent(const SquadOrder& order) {
  EventBits bits;
  bits.put(24, kEventTypeBits).put(order.squad, 8).put(order.team, 8)
      .put(order.id, 8).put(order.target != 0, 1);
  if (order.target) {
    bits.put(order.target, 16);
  } else {
    for (auto component : {&Vec3f::x, &Vec3f::y, &Vec3f::z}) {
      for (const auto& point : order.points) bits.put(std::bit_cast<std::uint32_t>(point.*component), 32);
    }
  }
  bits.put(static_cast<unsigned>(order.route), 8).put(order.accepted, 1);
  return bits;
}

EventBits orderResponseEvent(const OrderResponse& response) {
  EventBits bits;
  bits.put(27, kEventTypeBits).put(response.squad, 8).put(response.team, 8)
      .put(response.accepted, 1).put(static_cast<unsigned>(response.route), 8).put(response.id, 8);
  return bits;
}

EventBits commanderOrderReplyEvent(unsigned squad, unsigned team, bool accepted) {
  EventBits bits;
  bits.put(50, kEventTypeBits).put(squad, 8).put(team, 8).put(accepted ? 2 : 3, 3);
  return bits;
}

EventBits commanderEvent(CommanderAction action, int team, int player) {
  EventBits bits;
  bits.put(19, kEventTypeBits).put(static_cast<unsigned>(action), 4)
      .put(team, 8).put(0, 1).put(player, 15);
  return bits;
}

EventBits commanderCameraEvent(Vec3f position) {
  EventBits bits;
  bits.put(36, kEventTypeBits).put(std::bit_cast<std::uint32_t>(position.x), 32)
      .put(std::bit_cast<std::uint32_t>(position.y), 32)
      .put(std::bit_cast<std::uint32_t>(position.z), 32);
  return bits;
}

EventBits commanderToolEvent(CommanderTool tool, int team, int squad, Vec3f target) {
  EventBits bits;
  const auto x = std::bit_cast<std::uint32_t>(target.x);
  const auto z = std::bit_cast<std::uint32_t>(target.z);
  switch (tool) {
    case CommanderTool::Move:
    case CommanderTool::Attack:
    case CommanderTool::Defend: {
      const unsigned order = tool == CommanderTool::Move ? 4 : tool == CommanderTool::Attack ? 5 : 6;
      return squadOrderEvent({static_cast<unsigned>(squad), static_cast<unsigned>(team), order,
                              0, {target, {}, {}}, OrderRoute::Commander, false});
    }
    case CommanderTool::UAV:
    case CommanderTool::Scan:
      bits.put(45, kEventTypeBits).put(x, 32).put(z, 32).put(team, 8)
          .put(tool == CommanderTool::Scan, 1);
      break;
    case CommanderTool::Artillery:
      bits.put(30, kEventTypeBits).put(1, 2).put(team, 32).put(x, 32).put(z, 32);
      break;
    case CommanderTool::Supplies:
      bits.put(37, kEventTypeBits).put(team, 32).put(x, 32).put(z, 32);
      break;
    case CommanderTool::Vehicle:
      bits.put(51, kEventTypeBits).put(x, 32).put(z, 32).put(team, 8).put(0, 1);
      break;
  }
  return bits;
}

std::vector<std::byte> writeDataPacket(std::uint8_t connectionId, Link& link,
                                       const PlayerActions* actions) {
  // The header (9 bytes, the last two the payload's length, filled in below).
  std::vector<std::byte> buffer(1472);
  BitWriter writer(buffer);
  const ExtendedHeader header = link.nextDataHeader();
  writer.writeBits(static_cast<std::uint32_t>(PacketKind::Data), 4);
  writer.writeBits(connectionId, 8);
  writer.writeBits(header.sequence & 0x3F, 6);
  writer.writeBits(header.ack & 0x3F, 6);
  writer.writeBits(header.ackBits, 32);
  writer.writeBits(0, 16);
  // The streams in `ClientConnection`'s order: actions, events, ghosts.
  if (actions != nullptr) {
    writer.writeBits(1, 1);
    WriteCursor cursor(writer);
    PlayerActions copy = *actions;
    serializePlayerActions(cursor, copy);
  } else {
    writer.writeBits(0, 1);
  }
  link.writeEvents(writer);
  writer.writeBits(0, 1);  // no ghosts
  const auto bytes = writer.byteSize();
  constexpr std::size_t kHeaderBytes = 9;
  const auto payload = static_cast<std::uint16_t>(bytes - kHeaderBytes);
  buffer[7] = static_cast<std::byte>(payload & 0xFF);
  buffer[8] = static_cast<std::byte>(payload >> 8);
  buffer.resize(bytes);
  return buffer;
}

}  // namespace obf2::net::bf2
