// Chat strings (obf2/net/chat_string.h): an announcement, chunks, the trailing
// 12-byte header.
#include <string>
#include <vector>

#include "check.h"
#include "obf2/net/chat_string.h"

using obf2::net::bf2::ChatAssembler;

namespace {

std::vector<std::uint8_t> message(const std::string& text, int channel, int type, int sender) {
  std::vector<std::uint8_t> out(text.begin(), text.end());
  const auto put32 = [&](int value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
  };
  put32(channel);
  put32(type);
  out.push_back(static_cast<std::uint8_t>(sender));
  out.push_back(0);  // target
  out.push_back(0);  // flags
  out.push_back(0);  // padding
  return out;
}

void testAssembled() {
  ChatAssembler chat;
  const auto bytes = message("hello there", 0, 2, 7);
  CHECK(chat.begin(static_cast<std::uint32_t>(bytes.size())));
  CHECK(!chat.begin(10));  // a second announcement is refused
  const std::vector<std::uint8_t> first(bytes.begin(), bytes.begin() + 5);
  const std::vector<std::uint8_t> rest(bytes.begin() + 5, bytes.end());
  CHECK(!chat.add(first).has_value());
  const auto done = chat.add(rest);
  CHECK(done.has_value());
  CHECK(done->text == "hello there");
  CHECK_EQ(done->type, 2);
  CHECK_EQ(done->sender, 7);
  CHECK(!chat.assembling());
}

void testOverrunRefused() {
  ChatAssembler chat;
  CHECK(chat.begin(13));
  bool refused = false;
  CHECK(!chat.add(std::vector<std::uint8_t>(20, 'x'), &refused).has_value());
  CHECK(refused);
}

}  // namespace

TEST_MAIN({
  testAssembled();
  testOverrunRefused();
})
