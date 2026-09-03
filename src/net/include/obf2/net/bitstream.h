#pragma once
// Побітовий потік мережевого протоколу Refractor 2.
//
// Розкладка бітів: у кожному байті молодші біти йдуть **першими**, а значення
// довші за байт продовжуються в наступний. Тобто запис 3 біт `0b101` у
// порожній буфер дає байт `0x05`, а не `0xA0`.
//
// Джерела: [Refractor-2-BitStream-Emulator](https://github.com/matthias-hoste/Refractor-2-BitStream-Emulator)
// (робоча реалізація рукостискання) та символи лінукс-сервера BF2 1.5, де
// клас зветься `dice::hfe::io::BitStream` і має саме такий набір методів.
//
// Реалізація власна. Головна відмінність від оригіналу — **перевірка меж**:
// сюди приходять пакети з мережі, і вихід за буфер тут неприпустимий.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "obf2/core/math.h"

namespace obf2::net {

// Розмір службових полів у бітах — саме так їх читає й пише гра.
inline constexpr unsigned kBasicHeaderTypeBits = 4;
inline constexpr unsigned kBasicHeaderSubtypeBits = 8;
inline constexpr unsigned kExtendedHeaderTypeBits = 6;
inline constexpr unsigned kExtendedHeaderIdBits = 6;
inline constexpr unsigned kExtendedHeaderSequenceBits = 32;

// Тип пакета в основному заголовку.
enum class PacketType : std::uint32_t {
  ConnectionRequest = 1,
  ConnectionAccept = 2,
  ConnectionDenied = 3,
  ConnectionAcknowledge = 4,
  Disconnect = 5,
  PingRequest = 7,
  PingResponse = 8,
  Data = 15,
};

std::string_view packetTypeName(PacketType type);

struct BasicHeader {
  std::uint32_t type = 0;     // 4 біти, див. PacketType
  std::uint32_t subtype = 0;  // 8 біт
};

struct ExtendedHeader {
  std::uint32_t type = 0;      // 6 біт
  std::uint32_t dataId = 0;    // 6 біт
  std::uint32_t sequence = 0;  // 32 біти
};

// Стиснений вектор: замість трьох float-ів пишеться різниця до опорної
// точки з точністю, обраною за відстанню. Таблиці бітів узяті з даних
// лінукс-сервера (`BitStream::m_compressionVectorBitTable`), а сам алгоритм —
// з декомпіляції `writeCompressedVector`.
//
// Рівень (2 біти) обирається за довжиною різниці:
//
//   < 2^11  -> рівень 3, 12 біт на компоненту
//   < 2^15  -> рівень 2, 16 біт
//   < 2^19  -> рівень 1, 20 біт
//   інакше  -> рівень 0: три сирі float-и, і то вже АБСОЛЮТНА позиція
//
// У рівнях 1-3 компонента пишеться знаком і величиною: 1 біт знаку плюс
// (біти - 1) біт модуля.
inline constexpr std::uint32_t kCompressionVectorBitTable[4] = {32, 20, 16, 12};
inline constexpr std::uint32_t kHighCompressionVectorBitTable[4] = {32, 12, 10, 8};
inline constexpr std::uint32_t kCompressionVectorBitTable2[8] = {28, 24, 20, 16, 12, 10, 8, 0};

// Скільки біт іде на рівень стиснення.
inline constexpr unsigned kCompressionLevelBits = 2;

class BitReader {
 public:
  explicit BitReader(std::span<const std::byte> data) : data_(data) {}

  // nullopt — вихід за межі буфера або запит більше ніж 32 біти.
  std::optional<std::uint32_t> readBits(unsigned bits);
  std::optional<bool> readBool();
  std::optional<std::uint8_t> readByte();
  // Рядок фіксованої довжини; нульові байти обрізаються.
  std::optional<std::string> readString(std::size_t length);
  bool readBytes(std::span<std::byte> destination);

  // precision — той самий крок квантування, що й при записі.
  std::optional<Vec3f> readCompressedVector(const Vec3f& reference, float precision,
                                            const std::uint32_t (&table)[4] =
                                                kCompressionVectorBitTable);

  std::optional<BasicHeader> readBasicHeader();
  std::optional<ExtendedHeader> readExtendedHeader();

  bool skipBits(std::size_t bits);
  std::size_t bitPosition() const { return position_; }
  std::size_t bitsRemaining() const { return data_.size() * 8 - position_; }
  bool ok() const { return ok_; }

 private:
  std::span<const std::byte> data_;
  std::size_t position_ = 0;
  bool ok_ = true;
};

class BitWriter {
 public:
  explicit BitWriter(std::span<std::byte> buffer) : buffer_(buffer) {}

  // false — не влізло; потік після цього лишається у стані помилки.
  bool writeBits(std::uint32_t value, unsigned bits);
  bool writeBool(bool value);
  bool writeByte(std::uint8_t value);
  // Рядок доповнюється нулями до length байтів або обрізається.
  bool writeString(std::string_view text, std::size_t length);
  bool writeBytes(std::span<const std::byte> bytes);

  bool writeCompressedVector(const Vec3f& value, const Vec3f& reference, float precision,
                             const std::uint32_t (&table)[4] = kCompressionVectorBitTable);

  bool writeBasicHeader(const BasicHeader& header);
  bool writeExtendedHeader(const ExtendedHeader& header);

  std::size_t bitPosition() const { return position_; }
  // Скільки цілих байтів займає записане (з добиванням до межі байта).
  std::size_t byteSize() const { return (position_ + 7) / 8; }
  bool ok() const { return ok_; }

 private:
  std::span<std::byte> buffer_;
  std::size_t position_ = 0;
  bool ok_ = true;
};

// --- один опис розкладки на обидва напрями --------------------------
//
// Складач і розбирач протоколу неминуче описують ту саму розкладку, і
// поки їх двоє, вони розходяться: досить забути поле в одному з них.
// Тому пишемо розкладку **один раз** — функцією, яка бере курсор і
// проходить по полях, — а напрям вибирає сам курсор.
//
// Виглядає це так:
//
//   template <typename Cursor>
//   bool serialize(Cursor& cursor, PlayerActions& actions) {
//     if (!cursor.bits(actions.number, 9)) return false;
//     ...
//   }
//
// і те саме тіло працює як `readPlayerActions`, і як `writePlayerActions`.
// Так само чинить і сам рушій: у кожної його події є пара
// `serialize`/`deSerialize`, і поля в них ідуть однаково.
class ReadCursor {
 public:
  explicit ReadCursor(BitReader& reader) : reader_(reader) {}

  static constexpr bool reading = true;

  bool bits(std::uint32_t& value, unsigned width) {
    const auto got = reader_.readBits(width);
    if (!got) return false;
    value = *got;
    return true;
  }
  bool flag(bool& value) {
    std::uint32_t raw = 0;
    if (!bits(raw, 1)) return false;
    value = raw != 0;
    return true;
  }
  // Число зі знаком: біт знака, далі значення. Саме так рушій пише всі
  // цілі зі знаком — і в блоці MapInfo, і в потоці дій гравця.
  bool signedBits(std::int32_t& value, unsigned width) {
    std::uint32_t sign = 0;
    std::uint32_t magnitude = 0;
    if (!bits(sign, 1) || !bits(magnitude, width)) return false;
    value = static_cast<std::int32_t>(magnitude);
    if (sign) value = -value;
    return true;
  }

 private:
  BitReader& reader_;
};

class WriteCursor {
 public:
  explicit WriteCursor(BitWriter& writer) : writer_(writer) {}

  static constexpr bool reading = false;

  bool bits(std::uint32_t& value, unsigned width) { return writer_.writeBits(value, width); }
  bool flag(bool& value) { return writer_.writeBits(value ? 1 : 0, 1); }
  bool signedBits(std::int32_t& value, unsigned width) {
    const bool negative = value < 0;
    const auto magnitude = static_cast<std::uint32_t>(negative ? -value : value);
    return writer_.writeBits(negative ? 1 : 0, 1) && writer_.writeBits(magnitude, width);
  }

 private:
  BitWriter& writer_;
};

}  // namespace obf2::net
