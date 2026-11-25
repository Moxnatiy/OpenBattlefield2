#include "obf2/hud/text_case.h"

namespace obf2::hud {

std::string upperCase(std::string text) {
  // `BF2.exe` 0x4311b0: the five hand-mapped bytes, then `toupper` in the "C"
  // locale, which changes a–z only.
  for (char& c : text) {
    const auto byte = static_cast<unsigned char>(c);
    switch (byte) {
      case 0xe5: c = static_cast<char>(0xc5); break;
      case 0xe4: c = static_cast<char>(0xc4); break;
      case 0xf6: c = static_cast<char>(0xd6); break;
      case 0xfc: c = static_cast<char>(0xdc); break;
      case 0xe9: c = static_cast<char>(0xc9); break;
      default:
        if (byte >= 'a' && byte <= 'z') c = static_cast<char>(byte - 'a' + 'A');
    }
  }
  return text;
}

}  // namespace obf2::hud
