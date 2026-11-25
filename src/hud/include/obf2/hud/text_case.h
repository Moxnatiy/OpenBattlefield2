#pragma once

#include <string>

namespace obf2::hud {

// The HUD's upper case (`BF2.exe` 0x4311b0, in place; 0x431410 is the copy
// that calls it). Five Latin-1 letters are mapped by hand — å Å (0xe5 → 0xc5),
// ä Ä (0xe4 → 0xc4), ö Ö (0xf6 → 0xd6), ü Ü (0xfc → 0xdc), é É (0xe9 → 0xc9) —
// and every other byte goes through the C runtime's `toupper` (the "C" locale:
// ASCII a–z only). Callers of 0x431410: the squad row 0x78fc30 (the squad's
// name), 0x7a3310, 0x6c4950, 0x699b20, 0x79edf0, 0x6c4c20 — only the first is
// ported.
std::string upperCase(std::string text);

}  // namespace obf2::hud
