#pragma once
// The in-game chat — `HudChat` (OpenBF2 Menu/HudChat.cpp; `BF2.exe` list data 3
// of 0x7af170, the `ChatList` node of `HudElementsChat.con`).
//
// One list holds nine kinds of line, each in its own band of rows: a kind has a
// first row, a capacity, a spacing before it and a duration. A new line takes the
// next row of its band; a full band drops its oldest row and moves the rest up;
// a band whose time runs out drops its oldest row. Long text is wrapped on the
// width of the text column, at a space when one is within 20 characters of the
// cut. The setup is `BF2HudManager`'s (OpenBF2 BF2HudManagerInit.cpp):
//
//   kind  what                       capacity  spacing  duration
//   0     players' chat, kill lines  4         4        15
//   1     game messages (kill feed)  5         0        20
//   7     personal notices           3         12       15
//   8     requests                   2         9        500
//
// (2..6 have no rows; their durations are 15.) The rows, top to bottom: kind 1
// from row 0, kind 2 from 5, kind 0 from 9, kind 7 from 25, kind 8 from 37 — 39
// rows in all.
//
// The colours are per kind and per "variant": a line's variant is 2 when its
// team is the local player's, 1 otherwise (`wrapMessage`).
#include <array>
#include <functional>
#include <string>
#include <string_view>

#include "obf2/hud/hud.h"
#include "obf2/hud/list_data.h"

namespace obf2::hud {

class Chat {
 public:
  Chat();

  // The width of one glyph run in a list font (the `ChatList` node's fonts 0 and
  // 1), for wrapping. Without it nothing is wrapped.
  using Measure = std::function<float(int font, std::string_view text)>;
  void setMeasure(Measure measure) { measure_ = std::move(measure); }
  // A localisation key's text, for the chat's own prefixes.
  using Localize = std::function<std::string(std::string_view key)>;
  void setLocalize(Localize localize) { localize_ = std::move(localize); }
  // The local player's team, for the variant (0: none).
  void setLocalTeam(int team) { localTeam_ = team; }

  // Who a line is about, for `selectPlayerColors`: a commander's lines are his
  // colour, a squadmate's (or a squad leader's, to the commander) the squad's.
  struct Speaker {
    bool commander = false;
    bool squadColour = false;
  };

  // `addGameMessage`: the kill feed and other game lines (kind 1's band). Team 0
  // gives the plain kind-6 colour.
  void addGameMessage(const std::string& text, const Speaker* speaker, int team);
  // `addPlayerMessage`: a player's chat line (kind 0's band). The text may carry
  // the literal keys `HUD_CHAT_DEADPREFIX`, `HUD_TEXT_CHAT_TEAM`,
  // `HUD_TEXT_CHAT_SQUAD` or `HUD_TEXT_CHAT_COMMANDER` at its start, which become
  // their texts before "Name: "; a squad line takes the squad's colour, a
  // commander's his.
  struct Talker {
    std::string name;
    int team = 0;
    bool commander = false;
  };
  void addPlayerMessage(const std::string& text, const Talker& talker);
  // `addKillMessage` (kind 0's band).
  void addKillMessage(const std::string& text, const Speaker* speaker, int team);
  // `addNotice` and `addPersonalMessage` (kind 7's band).
  void addNotice(const std::string& text);
  // `addRequest` (kind 8's band), upper-cased.
  void addRequest(const std::string& text, int team);

  // `advanceTimers`: each band's time runs down; at zero its oldest row goes.
  void update(float dt);
  // `clearMessages`.
  void clear();

  const ListData& list() const { return data_; }
  // Grows with every change of the rows, for whoever rebuilds the list's node.
  int revision() const { return revision_; }
  // Every line that left the list, oldest first (`outputChatMessage`: the
  // console's history); for the log.
  std::function<void(const std::string&)> onHistory;

 private:
  static constexpr int kKinds = 9;
  void setColour(int kind, int variant, Color colour) { colours_[variant][kind] = colour; }
  void selectPlayerColours(int kind, const Speaker* speaker);
  void restoreColours(int kind);
  void rebuildRows();
  int nextRow(int kind);
  void dropRow(int kind);
  void append(const std::string& text, int kind, int team, int band);
  void wrap(int kind, const std::string& text, int team, int band, int row);
  // `measureText` on column 1: how many bytes of `text` fit, and the font the
  // last code left.
  std::size_t fit(std::string_view text, int row, int* font) const;

  ListData data_;
  Measure measure_;
  Localize localize_;
  int localTeam_ = 0;
  int revision_ = 0;
  std::array<int, kKinds> capacity_{};
  std::array<int, kKinds> spacing_{};
  std::array<float, kKinds> duration_{};
  std::array<int, kKinds> firstRow_{};
  std::array<int, kKinds> count_{};
  std::array<float, kKinds> remaining_{};
  std::array<std::array<Color, kKinds>, 4> colours_{};
};

}  // namespace obf2::hud
