#pragma once
// Протокол оригінального сервера BF2.
//
// Розкладку взято з рушія (Linux-сервер, `dice::hfe::io::NetServer`):
// `_update` розбирає заголовок і роздає пакети за типом, а
// `handleConnectRequest`, `sendConnectAccept` і `sendConnectDenied`
// задають тіла.
//
// Заголовок кожного пакета — рівно 12 бітів:
//   4 біти  тип
//   8 бітів номер з'єднання (у клієнта до під'єднання це 0)
//
// Біти пакуються молодшими вперед — так само, як у нашому BitStream.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace obf2::net::bf2 {

// Типи пакетів із диспетчера `NetServer::_update`.
enum class PacketKind : std::uint8_t {
  ConnectRequest = 1,
  ConnectAccept = 2,
  ConnectDenied = 3,
  ConnectAcceptAck = 4,
  Disconnect = 5,
  PingRequest = 7,
  PingResponse = 8,
  ServerInfoRequest = 9,
  Data = 15,
};

// Стала, яку рушій звіряє першою (`checkVersion`: перше число має бути
// рівно 0x1002, інакше відмова).
inline constexpr std::uint32_t kProtocolMagic = 0x1002;

// Версія гри. Байти 15 0C 51 00 читаються як 1.5 і збірка 0x0C51 = 3153,
// тобто рівно `bf2-linuxded-1.5.3153.0`. Знайдено двійковим пошуком по
// живому серверу: він відповідає «застарий» (0x17) або «занадто новий»
// (0x18), і за 32 кроки дає точне число.
inline constexpr std::uint32_t kGameVersion = 0x150C5100;

// Причини відмови з `handleConnectRequest`.
enum class DenyReason : std::uint32_t {
  ServerFull = 0x02,
  VersionMismatch = 0x09,
  WrongPassword = 0x11,
  Banned = 0x16,
  ClientTooOld = 0x17,
  ClientTooNew = 0x18,
  NoFreeSlots = 0x1E,
  PunkBusterRequired = 0x1F,
  WrongMod = 0x24,
};

std::string_view denyReasonName(DenyReason reason);

struct ConnectRequest {
  std::uint32_t magic = kProtocolMagic;
  std::uint32_t version = kGameVersion;
  bool punkBuster = true;
  std::uint32_t reconnectToken = 0;
  std::string password;
  std::string modDirectory = "mods/bf2";
};

struct ConnectAccept {
  std::uint8_t connectionId = 0;
  std::uint32_t serverTime = 0;
  bool punkBuster = false;
};

struct ConnectDenied {
  DenyReason reason = DenyReason::VersionMismatch;
  std::string modDirectory;  // сервер додає її лише до відмови WrongMod
};

// Розширений заголовок надійного рівня. Стоїть одразу після базового в
// пакетах пінгу й даних (`writeExtendedHeader`):
//   6 бітів  номер пакета (по колу до 64)
//   6 бітів  номер останнього прийнятого
//   32 біти  бітова маска, які з попередніх дійшли
struct ExtendedHeader {
  std::uint8_t sequence = 0;
  std::uint8_t ack = 0;
  std::uint32_t ackBits = 0;
};

// Пакет, розібраний із мережі.
struct Incoming {
  PacketKind kind = PacketKind::Data;
  std::uint8_t connectionId = 0;
  std::optional<ConnectAccept> accept;
  std::optional<ConnectDenied> denied;

  // Є в пінгах і даних.
  std::optional<ExtendedHeader> extended;
  // Час сервера з пінг-запиту: його треба повернути незміненим.
  std::optional<std::uint32_t> pingTime;
};

// Збирає запит на під'єднання (тип 1).
std::vector<std::byte> writeConnectRequest(const ConnectRequest& request);

// Відповідь на пінг. `time` — те саме число, що надіслав сервер: за
// різницею він рахує затримку.
std::vector<std::byte> writePingResponse(std::uint8_t connectionId, const ExtendedHeader& header,
                                         std::uint32_t time);

// Короткий пакет із самого заголовка: підтвердження, від'єднання, пінг.
std::vector<std::byte> writeShortPacket(PacketKind kind, std::uint8_t connectionId);

// Розбирає те, що прийшло від сервера. nullopt — пакет коротший за заголовок.
std::optional<Incoming> readPacket(std::span<const std::byte> data);

}  // namespace obf2::net::bf2
