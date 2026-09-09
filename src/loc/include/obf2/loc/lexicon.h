#pragma once
// BF2 localisation: `.utxt` files in `Localization/<language>/`.
//
// The format is simple and needed no reversing either:
//
//   UTF-16LE with a BOM, lines separated by CRLF
//   KEY<spaces up to 30 columns>\x1B\x1B value \x1B\x1B
//
// Two ESC characters (0x1B) frame the value on both sides. The keys are the same
// ones that occur in `.con` and `.tweak`: `HUD_INGAME_QUIT`,
// `WEAPON_NAME_ammobag` and so on.
//
// Values may contain `§` (0xA7) — the game's colour control sequences.
// We leave them as they are for now.
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace obf2::loc {

class Lexicon {
 public:
  // Adds the contents of one .utxt. There are several files per language (the
  // main one, patches, add-ons), and later ones override earlier — as in the game.
  bool addUtxt(std::span<const std::byte> bytes, std::string* error = nullptr);

  // nullopt when the key is absent: whether to substitute the key itself is up to
  // whoever draws it, because in the game a missing string shows as the key.
  std::optional<std::string_view> find(std::string_view key) const;

  // A convenience variant: returns the key itself when there is no translation.
  std::string_view text(std::string_view key) const;

  std::size_t size() const { return entries_.size(); }

 private:
  std::unordered_map<std::string, std::string> entries_;
};

// UTF-16LE -> UTF-8. Surrogate pairs are combined into one character.
std::string utf16ToUtf8(std::span<const std::byte> bytes);

}  // namespace obf2::loc
