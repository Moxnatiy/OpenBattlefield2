#include "obf2/loc/lexicon.h"

#include <cstring>

namespace obf2::loc {
namespace {

constexpr char kSeparator = '\x1B';

void appendUtf8(std::string& out, std::uint32_t codepoint) {
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::string_view trimRight(std::string_view text) {
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}

std::string_view trimLeft(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  return text;
}

}  // namespace

std::string utf16ToUtf8(std::span<const std::byte> bytes) {
  std::string out;
  out.reserve(bytes.size() / 2);

  std::size_t index = 0;
  // The BOM, if present, is skipped.
  if (bytes.size() >= 2 && static_cast<std::uint8_t>(bytes[0]) == 0xFF &&
      static_cast<std::uint8_t>(bytes[1]) == 0xFE) {
    index = 2;
  }

  auto unitAt = [&](std::size_t at) -> std::uint32_t {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + 1])) << 8);
  };

  while (index + 1 < bytes.size()) {
    std::uint32_t unit = unitAt(index);
    index += 2;

    // A surrogate pair: a high surrogate plus a low one give one character.
    if (unit >= 0xD800 && unit <= 0xDBFF && index + 1 < bytes.size()) {
      const std::uint32_t low = unitAt(index);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        index += 2;
        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
      }
    }
    appendUtf8(out, unit);
  }
  return out;
}

bool Lexicon::addUtxt(std::span<const std::byte> bytes, std::string* error) {
  if (bytes.size() < 2) {
    if (error) *error = "file too small for a .utxt";
    return false;
  }

  const std::string text = utf16ToUtf8(bytes);
  std::size_t added = 0;
  std::size_t position = 0;

  while (position < text.size()) {
    const std::size_t lineEnd = text.find('\n', position);
    std::string_view line(text.data() + position,
                          (lineEnd == std::string::npos ? text.size() : lineEnd) - position);
    position = (lineEnd == std::string::npos) ? text.size() : lineEnd + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) continue;

    // Key and value are separated by two ESCs; two more frame the value on the right.
    const std::size_t separator = line.find(kSeparator);
    if (separator == std::string_view::npos) continue;

    const std::string_view key = trimRight(line.substr(0, separator));
    if (key.empty()) continue;

    std::string_view value = line.substr(separator);
    while (!value.empty() && value.front() == kSeparator) value.remove_prefix(1);
    while (!value.empty() && value.back() == kSeparator) value.remove_suffix(1);
    value = trimRight(trimLeft(value));

    entries_[std::string(key)] = std::string(value);
    ++added;
  }

  if (added == 0) {
    if (error) *error = "not a single line was parsed";
    return false;
  }
  return true;
}

std::optional<std::string_view> Lexicon::find(std::string_view key) const {
  const auto found = entries_.find(std::string(key));
  if (found == entries_.end()) return std::nullopt;
  return std::string_view{found->second};
}

std::string_view Lexicon::text(std::string_view key) const {
  const auto found = find(key);
  return found ? *found : key;
}

}  // namespace obf2::loc
