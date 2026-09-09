#include "obf2/mesh/material.h"

#include <cctype>

namespace obf2::mesh {
namespace {

bool startsWithNoCase(std::string_view text, std::string_view token) {
  if (text.size() < token.size()) return false;
  for (std::size_t i = 0; i < token.size(); ++i) {
    const auto a = static_cast<unsigned char>(text[i]);
    const auto b = static_cast<unsigned char>(token[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

enum class Channel { Base, Detail, Dirt, Crack, NormalBase, NormalDetail, NormalCrack, Parallax };

struct Token {
  std::string_view text;
  Channel channel;
};

// Longest first: `NDetail` has to win over `Detail`, and `parallaxdetail`
// over both — otherwise `BaseDetailNDetailparallaxdetail` would be read as
// four channels where the game has three plus a flag.
constexpr Token kTokens[] = {
    {"parallaxdetail", Channel::Parallax}, {"NDetail", Channel::NormalDetail},
    {"NCrack", Channel::NormalCrack},      {"Detail", Channel::Detail},
    {"NBase", Channel::NormalBase},        {"Crack", Channel::Crack},
    {"Base", Channel::Base},               {"Dirt", Channel::Dirt},
};

}  // namespace

MaterialLayout materialLayout(std::string_view technique) {
  MaterialLayout out;
  std::size_t at = 0;
  while (at < technique.size()) {
    const std::string_view rest = technique.substr(at);
    const Token* found = nullptr;
    for (const Token& token : kTokens) {
      if (startsWithNoCase(rest, token.text)) {
        found = &token;
        break;
      }
    }
    if (found == nullptr) break;
    at += found->text.size();

    // `parallaxdetail` is a flag on the detail channel, not a channel of its
    // own: it takes no texture slot (`RaShaderSTM.fx:272`, the parallax branch
    // samples DetailMapSampler through the normal map).
    if (found->channel == Channel::Parallax) {
      out.parallaxDetail = true;
      continue;
    }

    const int slot = out.count++;
    switch (found->channel) {
      case Channel::Base: out.base = slot; break;
      case Channel::Detail: out.detail = slot; break;
      case Channel::Dirt: out.dirt = slot; break;
      case Channel::Crack: out.crack = slot; break;
      case Channel::NormalBase: out.normalBase = slot; break;
      case Channel::NormalDetail: out.normalDetail = slot; break;
      case Channel::NormalCrack: out.normalCrack = slot; break;
      case Channel::Parallax: break;  // handled above
    }
  }
  return out;
}

}  // namespace obf2::mesh
