// The in-game chat (obf2/hud/chat.h): the bands, the drop of the oldest line,
// the timers, and wrapping on the text column.
#include <string>

#include "check.h"
#include "obf2/hud/chat.h"

using obf2::hud::Chat;

namespace {

// The rows: kind 1 from 0 (5), kind 0 from 9 (4), kind 7 from 25 (3), kind 8
// from 37 (2) — 39 in all (`rebuildRows`).
void testBands() {
  Chat chat;
  CHECK_EQ(chat.list().rows(), 39);
  CHECK_EQ(chat.list().visibleRows, 39);
  chat.setLocalTeam(1);
  chat.addGameMessage("first", nullptr, 1);
  CHECK(chat.list().text(1, 0).find("first") != std::string::npos);
  chat.addNotice("note");
  CHECK(chat.list().text(1, 25).find("note") != std::string::npos);
  chat.addKillMessage("kill", nullptr, 2);
  CHECK_EQ(chat.list().text(1, 9), std::string("kill"));
  // A kind-0 line without a side is not shown (`appendMessage`).
  chat.addKillMessage("nobody", nullptr, 0);
  CHECK(chat.list().text(1, 10).empty());
}

// A full band drops its oldest line and moves the rest up.
void testFullBandDropsOldest() {
  Chat chat;
  for (int i = 0; i < 6; ++i) chat.addGameMessage("line " + std::to_string(i), nullptr, 1);
  CHECK(chat.list().text(1, 0).find("line 1") != std::string::npos);
  CHECK(chat.list().text(1, 4).find("line 5") != std::string::npos);
  // A timed-out band does not leave a copy of its last line behind.
  chat.update(21.0f);
  CHECK(chat.list().text(1, 3).find("line 5") != std::string::npos);
  CHECK(chat.list().text(1, 4).empty());
}

// The band's time: 20 seconds for game lines; then one line a frame goes.
void testTimers() {
  Chat chat;
  chat.addGameMessage("a", nullptr, 1);
  chat.addGameMessage("b", nullptr, 1);
  chat.update(19.0f);
  CHECK(!chat.list().text(1, 0).empty());
  chat.update(1.5f);
  CHECK(chat.list().text(1, 0).find('b') != std::string::npos);
  chat.update(0.1f);
  CHECK(chat.list().text(1, 0).empty());
}

// Wrapping: ten pixels a character on a 484-pixel column — the text breaks at a
// space and goes on in the next row of the band.
void testWrap() {
  Chat chat;
  chat.setMeasure([](int, std::string_view text) { return 10.0f * static_cast<float>(text.size()); });
  std::string words;
  for (int i = 0; i < 12; ++i) words += "word" + std::to_string(i) + " ";
  chat.addKillMessage(words, nullptr, 1);
  CHECK(!chat.list().text(1, 9).empty());
  CHECK(!chat.list().text(1, 10).empty());
  CHECK(chat.list().text(1, 9).size() <= 49);
}

}  // namespace

TEST_MAIN({
  testBands();
  testFullBandDropsOldest();
  testTimers();
  testWrap();
})
