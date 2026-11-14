#pragma once
// The connection's delivery: which of our data packets the server received, and
// the game events riding them until it has.
//
// Taken from OpenBF2's reconstruction of the engine's two halves of it:
//
//   * `SWConnection` (IO/Network/ProtocolLayer/SWConnection.cpp): every data
//     packet takes the next 6-bit number (`addPacketToSendBuffer`, the counter
//     starts at 1); its header names the last number received from the other
//     side and a 32-bit mask of the ones before it (`writeExtendedHeader` — the
//     field OpenBF2 calls `delayScaleFactor`). A received data packet with the
//     same number as the last, or more than 31 behind, is dropped
//     (`handleDataPacket`); otherwise the mask shifts by the gap and takes a 1.
//     A ping response carries the last number sent, not a new one. The other
//     side's acknowledgement turns into one delivered-or-lost answer per data
//     packet we sent, oldest first (`confirmPacket`, `getPacketDeliveryStatus`);
//   * `GameEventManager` (Game/Common/GameEventManager.cpp): events wait in a
//     queue; each data packet carries at most one block — every queued event, or
//     the oldest lost block whole with the resend bit (`transmit`). A block is a
//     5-bit number; at most 15 stand unconfirmed (`m_dword88`). A delivered block
//     is retired in order, a lost one goes back for resending
//     (`handlePacketStatus`). The receiving side executes blocks strictly in
//     their order (`processReceivedPacket`), so one lost and never resent stops
//     every event after it.
//
// We had none of it: every event went in a packet of its own, once, and our
// header told the server we had received everything. A lost or dropped event
// packet then held back every event after it.
#include <array>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <deque>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {

// One game event's bits as its `serialize` writes them, the 7-bit type first.
class EventBits {
 public:
  EventBits& put(std::uint32_t value, unsigned bits) {
    fields_.emplace_back(value, bits);
    return *this;
  }
  void write(BitWriter& writer) const {
    for (const auto& [value, bits] : fields_) writer.writeBits(value, bits);
  }
  std::size_t bitCount() const {
    std::size_t total = 0;
    for (const auto& field : fields_) total += field.second;
    return total;
  }

 private:
  std::vector<std::pair<std::uint32_t, unsigned>> fields_;
};

class Link {
 public:
  // `GameEventManager`'s window of unconfirmed blocks (`m_dword88`, 15 in its
  // constructor) and the send window of `SWConnection::send` (30 ahead of the
  // acknowledged).
  static constexpr int kEventWindow = 15;
  static constexpr unsigned kSendWindow = 30;

  // A data packet from the other side: its numbers go into our acknowledgement,
  // its acknowledgement into our deliveries. False: a duplicate or a stale one,
  // which the engine drops whole (`handleDataPacket`, answer 11).
  bool acceptData(const ExtendedHeader& header);
  // A ping request: its acknowledgement counts; `haveSent` makes its number
  // count as received too (`pingConfirmation`).
  void acceptPing(const ExtendedHeader& header, bool haveSent);

  // The header of the next data packet; it takes the next number.
  ExtendedHeader nextDataHeader();
  // The header of a ping response: the last number sent, ours and theirs.
  ExtendedHeader pingHeader() const;
  // Whether the send window has room for another data packet.
  bool canSend() const;

  // The server's event blocks, executed in their order (`GameEventManager::
  // processReceivedPacket`): a block waits until every block before it has come,
  // then it and the ones waiting behind it are handed back, oldest first. A block
  // already handed back, or already waiting, is dropped — the original would
  // take a repeat for an error, but a repeat does happen when the server's answer
  // about our acknowledgement is lost [CHOSEN, not recovered]: a block up to 16
  // behind the next expected is taken for such a repeat.
  std::vector<std::vector<Event>> receiveBlock(unsigned index, std::vector<Event> events);
  int blocksHeldBack() const { return static_cast<int>(pending_.size()); }
  int blocksRepeated() const { return blocksRepeated_; }
  int blocksOutOfOrder() const { return blocksOutOfOrder_; }

  // Queue an event for the next data packet.
  void queue(EventBits event) { sendQueue_.push_back(std::move(event)); }
  // Whether the next data packet would carry a block.
  bool hasEventsToSend() const;
  // The event section of the data packet whose header `nextDataHeader` just gave:
  // the presence bit, then count 8, block 5, resend 1 and the events. Records
  // the packet for its delivery answer — call it once for every data packet,
  // events or not.
  void writeEvents(BitWriter& writer);

  // Each block's fate as it is settled — confirmed or lost — with the packet
  // number it rode and how long the answer took; for the log.
  std::function<void(int block, unsigned sequence, bool delivered, double milliseconds,
                     std::size_t events)>
      onBlockSettled;

  // What happened, for the log.
  int blocksSent() const { return blocksSent_; }
  int blocksResent() const { return blocksResent_; }
  int packetsLost() const { return packetsLost_; }
  int unconfirmedBlocks() const { return ((nextSendBlock_ & 31) - lastConfirmedBlock_) & 31; }
  std::size_t queuedEvents() const { return sendQueue_.size(); }

 private:
  struct Block {
    int index = 0;
    std::vector<EventBits> events;
    std::chrono::steady_clock::time_point sentAt{};
    unsigned sequence = 0;
  };
  struct Sent {
    bool hasBlock = false;
    Block block;
  };

  void confirm(unsigned ack, std::uint32_t mask);  // `confirmPacket`
  void delivered(bool ok);                          // one answer, oldest packet first
  bool advanceConfirmed();                          // `advanceLastConfirmedBlockIndex`

  // `SWConnection`.
  unsigned nextSequence_ = 1;       // +0x48
  int lastSentSequence_ = -1;       // +0x54, the ping response's number
  unsigned remoteCounter_ = 0;      // +0x4C, the last number received
  std::uint32_t receiveMask_ = 0;   // +0xD8
  unsigned ackHorizon_ = 0;         // +0x50, the last number the other side acknowledged
  std::deque<Sent> inFlight_;       // one per data packet sent, oldest first

  // `GameEventManager`.
  std::deque<EventBits> sendQueue_;
  std::deque<Block> resend_;
  int nextSendBlock_ = 0;          // +0x7c
  int lastConfirmedBlock_ = 31;    // +0x84, "just before block zero"
  bool confirmed_[32] = {};        // +0xa4, a delivered block awaiting its turn

  // The receiving side.
  unsigned nextReceive_ = 0;                                   // +0x80
  std::map<unsigned, std::vector<Event>> pending_;            // the parked blocks
  int blocksRepeated_ = 0;
  int blocksOutOfOrder_ = 0;

  int blocksSent_ = 0;
  int blocksResent_ = 0;
  int packetsLost_ = 0;
};

// The events the client sends, each as its `serialize` writes it (the layouts
// are the ones `bf2_protocol.h` documents beside the old one-event writers).
EventBits postRemoteEvent(std::uint32_t category, std::uint32_t number,
                          std::optional<std::int32_t> value = std::nullopt);
EventBits connectionTypeEvent(std::uint32_t type);
EventBits contentCheckEvent(const std::array<std::byte, 16>& misc,
                            const std::array<std::byte, 16>& archives,
                            const std::array<std::byte, 16>& level);
EventBits challengeResponseEvent();
EventBits dataBlockHeaderEvent(std::uint32_t blockType, std::uint32_t size);
EventBits dataBlockChunkEvent(std::span<const std::byte> chunk);
EventBits joinSquadEvent(int team, int player, int squad);
EventBits leaveSquadEvent(int team, int player, int squad, bool clearInvite);
EventBits changeSquadNameEvent(int team, int squad, int player, std::string_view name);
EventBits setPrivateSquadEvent(int team, int squad, bool isPrivate);

// One data packet as `ClientConnection::transmitMsgs` lays it out: the header,
// the action stream (or its absence bit), the event section from the link, the
// ghost stream's absence bit. It takes the link's next number.
std::vector<std::byte> writeDataPacket(std::uint8_t connectionId, Link& link,
                                       const PlayerActions* actions);

}  // namespace obf2::net::bf2
