#include "obf2/hud/chat.h"

#include <algorithm>
#include <cctype>

namespace obf2::hud {

namespace {

// OpenBF2 Menu/HudColors.cpp and BF2HudManagerInit.cpp, the PE's initialisers.
constexpr std::array<Color, 4> kDefaultChat = {{{0.765f, 0.729f, 0.663f, 1.0f},
                                                {1.0f, 0.3921568691730499f, 0.3921568691730499f, 1.0f},
                                                {0.0117647061124444f, 0.7215686440467834f, 0.9490196108818054f, 1.0f},
                                                {1.0f, 0.0f, 1.0f, 1.0f}}};
constexpr std::array<Color, 4> kSquadChat = {{{1.0f, 1.0f, 1.0f, 1.0f},
                                              {0.4470588266849518f, 0.8901960849761963f, 0.3450980484485626f, 1.0f},
                                              {0.4470588266849518f, 0.8901960849761963f, 0.3450980484485626f, 1.0f},
                                              {0.4470588266849518f, 0.8901960849761963f, 0.3450980484485626f, 1.0f}}};
constexpr std::array<Color, 4> kCategoryThree = {
    {{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}};
constexpr Color kCommanderChat{0.9686274528503418f, 0.8274509906768799f, 0.2549019753932953f, 1.0f};
constexpr Color k97dd60{0.8156862854957581f, 0.8156862854957581f, 0.8156862854957581f, 1.0f};
constexpr Color k97dd70{0.4470588266849518f, 0.8901960849761963f, 0.3450980484485626f, 1.0f};
constexpr Color k97dd80{0.0117647061124444f, 0.7215686440467834f, 0.9490196108818054f, 1.0f};
constexpr Color k97dd90{0.9686274528503418f, 0.8274509906768799f, 0.2549019753932953f, 1.0f};
constexpr Color kNoticeYellow{0.95703125f, 0.7109375f, 0.046875f, 1.0f};

// The font codes of a list text (`§0`..`§3`, and `§C`/`§c` taking six), as
// `BfListBoxData::measureText` walks them.
constexpr unsigned char kCode = 0xa7;
const std::string kFont1 = std::string(1, static_cast<char>(kCode)) + "1";
const std::string kFont0 = std::string(1, static_cast<char>(kCode)) + "0";

// The length of the UTF-8 sequence starting with `lead`.
std::size_t sequenceLength(unsigned char lead) {
  if (lead < 0x80) return 1;
  if ((lead >> 5) == 0x6) return 2;
  if ((lead >> 4) == 0xe) return 3;
  if ((lead >> 3) == 0x1e) return 4;
  return 1;
}

}  // namespace

Chat::Chat() {
  // `HudChat::init("ingameChat", 16, 500)`: column 0 from the list's own
  // constructor, then 16 and 500 (the second running to the next).
  data_.addColumn(16.0f, false);
  data_.addColumn(500.0f, true);
  data_.reserveScrollbar = false;  // `m_field021 = false`
  duration_.fill(5.0f);
  capacity_[0] = 4;
  capacity_[1] = 5;
  capacity_[7] = 3;
  capacity_[8] = 2;
  spacing_[0] = 4;
  spacing_[7] = 12;
  spacing_[8] = 9;
  rebuildRows();
  duration_[6] = 15.0f;
  duration_[0] = 15.0f;
  duration_[1] = 20.0f;
  duration_[2] = 15.0f;
  duration_[3] = 15.0f;
  duration_[4] = 15.0f;
  duration_[5] = 15.0f;
  duration_[7] = 15.0f;
  duration_[8] = 500.0f;
  for (int variant = 0; variant < 3; ++variant) {
    setColour(6, variant, k97dd70);
    setColour(3, variant, kCategoryThree[static_cast<std::size_t>(variant)]);
    setColour(0, variant, kDefaultChat[static_cast<std::size_t>(variant)]);
    setColour(4, variant, kNoticeYellow);
    setColour(5, variant, kNoticeYellow);
    setColour(7, variant, k97dd60);
  }
  setColour(8, 0, k97dd60);
  setColour(8, 1, k97dd70);
  setColour(8, 2, k97dd90);
  setColour(8, 3, k97dd80);
}

void Chat::rebuildRows() {
  firstRow_[1] = spacing_[1];
  firstRow_[2] = spacing_[1] + spacing_[2] + capacity_[1];
  firstRow_[0] = firstRow_[2] + capacity_[2] + spacing_[0];
  firstRow_[7] = firstRow_[0] + spacing_[7] + capacity_[0];
  firstRow_[8] = firstRow_[7] + spacing_[8] + capacity_[7];
  count_.fill(0);
  remaining_.fill(0.0f);
  int total = 0;
  for (int i = 0; i < kKinds; ++i) total += capacity_[static_cast<std::size_t>(i)] + spacing_[static_cast<std::size_t>(i)];
  data_.clear();
  for (int row = 0; row < total; ++row) {
    data_.addText(0, "", 0.0f, 0.0f, Color{}, 0);
    data_.addText(1, "", 0.0f, 0.0f, Color{}, 0);
  }
  data_.visibleRows = total;
}

void Chat::selectPlayerColours(int kind, const Speaker* speaker) {
  if (speaker != nullptr && speaker->commander) {
    for (int variant = 0; variant < 3; ++variant) setColour(kind, variant, kCommanderChat);
    return;
  }
  if (speaker != nullptr && speaker->squadColour) {
    for (int variant = 0; variant < 3; ++variant) {
      setColour(kind, variant, kSquadChat[static_cast<std::size_t>(variant)]);
    }
    return;
  }
  restoreColours(kind);
}

void Chat::restoreColours(int kind) {
  for (int variant = 0; variant < 3; ++variant) {
    setColour(kind, variant, kDefaultChat[static_cast<std::size_t>(variant)]);
  }
}

void Chat::addGameMessage(const std::string& text, const Speaker* speaker, int team) {
  remaining_[1] = duration_[1];
  const std::string decorated = kFont1 + text + kFont0;
  if (team == 0) {
    append(decorated, 6, 0, 1);
  } else {
    selectPlayerColours(0, speaker);
    append(decorated, 0, team, 1);
    restoreColours(0);
  }
}

void Chat::addPlayerMessage(const std::string& text, const Talker& talker) {
  // `HudChat::addPlayerMessage` (OpenBF2 HudChat.cpp). The ignore list
  // (`isPlayerIgnored`) is not ours: every player is heard.
  remaining_[0] = duration_[0];
  const auto local = [this](const std::string& key) {
    return key.empty() || !localize_ ? std::string{} : localize_(key);
  };
  const std::string prefix = talker.name + ": ";
  std::string dead = "HUD_CHAT_DEADPREFIX";
  const std::string squad = "HUD_TEXT_CHAT_SQUAD", team = "HUD_TEXT_CHAT_TEAM",
                    commander = "HUD_TEXT_CHAT_COMMANDER";
  std::string stripped;
  if (text.find(dead) != std::string::npos) {
    stripped = text.substr(dead.size());
  } else {
    dead.clear();
    stripped = text;
  }
  std::string formatted;
  if (talker.commander) {
    for (int variant = 0; variant < 3; ++variant) setColour(0, variant, kCommanderChat);
    if (text.find(commander) != std::string::npos) {
      stripped = stripped.substr(std::min(commander.size(), stripped.size()));
      formatted = local(dead) + local(commander) + prefix + stripped;
    } else if (text.find(team) != std::string::npos) {
      stripped = stripped.substr(std::min(team.size(), stripped.size()));
      formatted = local(dead) + local(team) + prefix + stripped;
    } else {
      formatted = prefix + text;  // the original ignores the stripped dead prefix here
    }
  } else if (text.find(squad) != std::string::npos) {
    for (int variant = 0; variant < 3; ++variant) {
      setColour(0, variant, kSquadChat[static_cast<std::size_t>(variant)]);
    }
    stripped = stripped.substr(std::min(squad.size(), stripped.size()));
    formatted = local(dead) + local(squad) + prefix + stripped;
  } else {
    restoreColours(0);
    if (text.find(team) != std::string::npos) {
      stripped = stripped.substr(std::min(team.size(), stripped.size()));
      formatted = local(dead) + local(team) + prefix + stripped;
    } else {
      formatted = local(dead) + prefix + stripped;
    }
  }
  if (!formatted.empty()) append(formatted, 0, talker.team, 0);
  restoreColours(0);
}

void Chat::addKillMessage(const std::string& text, const Speaker* speaker, int team) {
  remaining_[0] = duration_[0];
  selectPlayerColours(0, speaker);
  if (!text.empty()) append(text, 0, team, 0);
  restoreColours(0);
}

void Chat::addNotice(const std::string& text) {
  remaining_[7] = duration_[7];
  append(text, 7, 0, 7);
}

void Chat::addRequest(const std::string& text, int team) {
  remaining_[8] = duration_[8];
  std::string upper = text;
  for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  append(upper.empty() ? text : upper, 8, team, 8);
}

int Chat::nextRow(int band) {
  const int saved = count_[static_cast<std::size_t>(band)];
  const int first = firstRow_[static_cast<std::size_t>(band)];
  if (saved >= capacity_[static_cast<std::size_t>(band)]) {
    dropRow(band);
    return first + saved - 1;
  }
  return first + saved;
}

void Chat::dropRow(int band) {
  ++revision_;
  const int first = firstRow_[static_cast<std::size_t>(band)];
  int& count = count_[static_cast<std::size_t>(band)];
  if (onHistory && !data_.text(1, first).empty()) onHistory(data_.text(1, first));
  if (count == 0) return;
  if (count == 1) {
    data_.clearRow(first);
  } else {
    for (int row = first; row < first + count - 1; ++row) {
      if (onHistory && row != first && !data_.text(1, row).empty()) onHistory(data_.text(1, row));
      data_.moveRow(row + 1, row);
    }
  }
  --count;
}

void Chat::append(const std::string& text, int kind, int team, int band) {
  // `appendMessage`: kinds 0 and 3 need a side; the empty column-0 cell first.
  if (text.empty()) return;
  ++revision_;
  const int row = nextRow(band);
  switch (kind) {
    case 0:
    case 3:
      if (team != 1 && team != 2) return;
      data_.setText(0, row, "", Color{});
      wrap(kind, text, team, band, row);
      return;
    case 6:
      data_.setText(0, row, "", Color{});
      wrap(6, text, team, band, row);
      return;
    case 7:
    case 8:
      data_.setText(0, row, "", Color{});
      wrap(kind, kFont1 + text + kFont0, team, band, row);
      return;
    default:
      return;
  }
}

std::size_t Chat::fit(std::string_view text, int row, int* font) const {
  // `BfListBoxData::measureText` on column 1: glyph by glyph until the run is
  // wider than the column — the glyph that crosses it is counted, as there —
  // or 250 characters. The column runs to the next one's start (16 to 500).
  (void)row;
  const auto& columns = data_.columns();
  const float available = columns.size() > 2 ? columns[2].x - columns[1].x : 484.0f;
  float width = 0.0f;
  std::size_t index = 0;
  int mode = *font;
  while (index < text.size()) {
    const auto c = static_cast<unsigned char>(text[index]);
    const auto next = index + 1 < text.size() ? static_cast<unsigned char>(text[index + 1]) : 0;
    if (c == kCode && next >= '0' && next <= '3') {
      mode = next - '0';
      index += 2;
    } else if (c == kCode && (next == 'C' || next == 'c')) {
      index += 6;
    } else {
      const std::size_t length = sequenceLength(c);
      width += measure_(mode, text.substr(index, length));
      index += length;
    }
    if (index >= 250) break;
    if (available < width) break;
  }
  *font = mode;
  return index > text.size() ? text.size() : index;
}

void Chat::wrap(int kind, const std::string& text, int team, int band, int row) {
  int variant = team;
  if (localTeam_ != 0 && kind != 8) variant = (team == localTeam_) + 1;
  if (variant < 0 || variant > 3) variant = 0;
  const Color colour = colours_[static_cast<std::size_t>(variant)][static_cast<std::size_t>(kind)];
  int& count = count_[static_cast<std::size_t>(band)];
  int mode = 0;
  if (!measure_ || fit(text, row, &mode) >= text.size()) {
    data_.setText(1, row, text, colour, 0);
    ++count;
    return;
  }
  std::size_t offset = 0;
  int nextMode = 0, previousMode = 0;
  bool firstLine = true;
  do {
    if (offset > 0) {
      row = nextRow(band);
      data_.setText(0, row, "", Color{});
    }
    std::size_t consumed = fit(std::string_view(text).substr(offset), row, &nextMode);
    const std::size_t remaining = text.size() - offset;
    bool skipSpace = false;
    if (consumed >= remaining) {
      consumed = remaining;
    } else {
      const std::size_t space = text.substr(offset, consumed + 1).rfind(' ', consumed);
      if (space != std::string::npos && space > 0 && consumed - space < 20) {
        consumed = space;
        skipSpace = true;
      }
    }
    if (consumed == 0) consumed = 1;  // the original has no guard here; ours does not loop forever
    data_.setText(1, row, text.substr(offset, consumed), colour, firstLine ? 0 : previousMode);
    offset += consumed;
    firstLine = false;
    previousMode = nextMode;
    if (skipSpace) ++offset;
    ++count;
  } while (offset < text.size());
}

void Chat::update(float dt) {
  for (int i = 0; i < kKinds; ++i) {
    if (count_[static_cast<std::size_t>(i)] <= 0) continue;
    remaining_[static_cast<std::size_t>(i)] -= dt;
    if (remaining_[static_cast<std::size_t>(i)] <= 0.0f) {
      dropRow(i);
      remaining_[static_cast<std::size_t>(i)] = 0.0f;
    }
  }
}

void Chat::clear() {
  for (int row = 0; row < data_.rows(); ++row) {
    if (onHistory && !data_.text(1, row).empty()) onHistory(data_.text(1, row));
    data_.clearRow(row);
  }
  count_.fill(0);
  remaining_.fill(0.0f);
  ++revision_;
}

}  // namespace obf2::hud
