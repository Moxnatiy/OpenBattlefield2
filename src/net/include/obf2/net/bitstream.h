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

}  // namespace obf2::net
