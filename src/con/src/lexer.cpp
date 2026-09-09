#include "obf2/con/lexer.h"

namespace obf2::con {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

}  // namespace

std::vector<std::string> tokenizeLine(std::string_view line) {
  std::vector<std::string> tokens;
  std::size_t i = 0;
  const std::size_t n = line.size();

  while (i < n) {
    while (i < n && isSpace(line[i])) ++i;
    if (i >= n) break;

    std::string token;
    if (line[i] == '"') {
      ++i;
      while (i < n && line[i] != '"') token.push_back(line[i++]);
      if (i < n) ++i;  // the closing quote; an unterminated string is accepted leniently
    } else {
      while (i < n && !isSpace(line[i])) token.push_back(line[i++]);
    }
    tokens.push_back(std::move(token));
  }
  return tokens;
}

std::vector<std::string> splitCommandPath(std::string_view token) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= token.size(); ++i) {
    if (i != token.size() && token[i] != '.') continue;
    if (i > start) parts.emplace_back(token.substr(start, i - start));
    start = i + 1;
  }
  return parts;
}

}  // namespace obf2::con
