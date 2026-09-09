#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace obf2::con {

// Splits ONE line of .con/.tweak into tokens.
//
// The rules, taken from the game's corpus (mods/bf2, 4155 files):
//  * the separators are space and tab, and how many there are does not matter;
//  * lines end in CRLF, so '\r' is trimmed;
//  * "a quoted string" is one token, and the quotes are not part of the value;
//  * every other character ('\', '/', '.', '-' included) is an ordinary token character.
//
// Quotes in BF2 have no escape sequences: a backslash inside quotes is part of a
// path ("Ingame\Kits\Icons\kit.tga"), not an escape.
std::vector<std::string> tokenizeLine(std::string_view line);

// Splitting a command's first token into components on the dots:
// "ObjectTemplate.fire.addFireRate" -> {"ObjectTemplate","fire","addFireRate"}
std::vector<std::string> splitCommandPath(std::string_view token);

}  // namespace obf2::con
