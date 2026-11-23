#include "obf2/net/chat_string.h"

namespace obf2::net::bf2 {

namespace {

constexpr std::uint32_t kHeaderBytes = 12;

int int32At(const std::vector<std::uint8_t>& data, std::size_t at) {
  const std::uint32_t value = static_cast<std::uint32_t>(data[at]) |
                              (static_cast<std::uint32_t>(data[at + 1]) << 8) |
                              (static_cast<std::uint32_t>(data[at + 2]) << 16) |
                              (static_cast<std::uint32_t>(data[at + 3]) << 24);
  return static_cast<int>(value);
}

}  // namespace

bool ChatAssembler::begin(std::uint32_t size) {
  if (assembling_) return false;
  // The size is a byte there (`m_size`).
  size_ = size & 0xFFu;
  data_.clear();
  assembling_ = true;
  return true;
}

std::optional<ChatString> ChatAssembler::add(const std::vector<std::uint8_t>& chunk, bool* refused) {
  if (refused != nullptr) *refused = false;
  if (!assembling_ || chunk.size() + data_.size() > size_) {
    if (refused != nullptr) *refused = true;
    return std::nullopt;
  }
  data_.insert(data_.end(), chunk.begin(), chunk.end());
  if (data_.size() != size_) return std::nullopt;
  assembling_ = false;
  if (size_ < kHeaderBytes) return std::nullopt;  // the original would read past it; we do not
  const std::size_t textBytes = size_ - kHeaderBytes;
  ChatString out;
  out.text.assign(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(textBytes));
  out.channel = int32At(data_, textBytes);
  out.type = int32At(data_, textBytes + 4);
  out.sender = data_[textBytes + 8];
  out.target = data_[textBytes + 9];
  out.flags = data_[textBytes + 10];
  return out;
}

}  // namespace obf2::net::bf2
