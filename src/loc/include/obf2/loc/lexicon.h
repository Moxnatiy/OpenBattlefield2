#pragma once
// Локалізація BF2: файли `.utxt` у `Localization/<мова>/`.
//
// Формат простий і теж не потребував реверсу:
//
//   UTF-16LE з BOM, рядки через CRLF
//   КЛЮЧ<пробіли до 30 позицій>\x1B\x1B значення \x1B\x1B
//
// Два символи ESC (0x1B) обрамляють значення з обох боків. Ключі — ті самі,
// що трапляються у `.con` і `.tweak`: `HUD_INGAME_QUIT`,
// `WEAPON_NAME_ammobag` тощо.
//
// Значення можуть містити `§` (0xA7) — керуючі послідовності кольору гри.
// Ми їх поки лишаємо як є.
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace obf2::loc {

class Lexicon {
 public:
  // Додає вміст одного .utxt. Файлів на мову кілька (основний, патчі,
  // додатки), і пізніші перекривають раніші — так само, як у грі.
  bool addUtxt(std::span<const std::byte> bytes, std::string* error = nullptr);

  // nullopt, якщо ключа немає: підставляти сам ключ чи ні — вирішує той,
  // хто малює, бо в грі відсутній рядок видно саме як ключ.
  std::optional<std::string_view> find(std::string_view key) const;

  // Зручний варіант: повертає сам ключ, якщо перекладу нема.
  std::string_view text(std::string_view key) const;

  std::size_t size() const { return entries_.size(); }

 private:
  std::unordered_map<std::string, std::string> entries_;
};

// UTF-16LE -> UTF-8. Сурогатні пари складаються в один символ.
std::string utf16ToUtf8(std::span<const std::byte> bytes);

}  // namespace obf2::loc
