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
#include <array>
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

// Каркас потоку подій усередині пакета даних.
//
// `GameEventManager::processReceivedPacket` читає: 1 біт «є події»,
// 8 бітів кількість, 5 бітів і ще 1 біт службових. Далі `readGameEvent`
// бере тип події у N бітах, де N — найменше, при якому (1<<N)-1 вміщує
// розмір реєстру подій; на живому сервері це 7.
//
// Перед потоками в заголовку пакета даних є ще 16 бітів — довжина
// корисної частини в байтах. Разом заголовок займає рівно 72 біти (9 байтів).
inline constexpr unsigned kStreamFramingBits = 16;
inline constexpr unsigned kEventTypeBits = 7;

// Тип події, якою передаються блоки даних (`DataBlockEvent::getType`).
inline constexpr std::uint32_t kDataBlockEvent = 4;

// Тип блока з відомостями про клієнта (`GameServer::handleDataBlock`).
inline constexpr std::uint32_t kClientInfoBlock = 1;

// Блок, який рушій читає в `ClientInfo::setFromDataBlock`:
//   u16 довжина + ім'я, u32 хеш імені, 1 біт знак + 31 біт номер профілю,
//   u16 довжина + тег клану, u16 довжина + рядок автентифікації, 1 біт.
//
// На сервері без рейтингу (`sv.ranked 0`) хеш і рядок автентифікації не
// перевіряються, а підсумкове ім'я гравця рушій складає як «тег + пробіл +
// ім'я» (`GameServer::handleClientInfo`).
struct ClientInfo {
  std::string name;
  std::uint32_t nameHash = 0;
  std::int32_t profileId = 0;
  std::string clanTag;
  std::string auth;
  bool flag = false;
};

// Подія-виклик: сервер шле її одразу після під'єднання
// (`GameServer::onNewConnection`), і клієнт має відповісти.
struct ChallengeEvent {
  std::string challenge;
  std::string modDirectory;
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

  // Для пакетів даних: скільки подій усередині й перша розібрана.
  int eventCount = 0;
  std::optional<ChallengeEvent> challenge;
};

// Збирає запит на під'єднання (тип 1).
std::vector<std::byte> writeConnectRequest(const ConnectRequest& request);

// Відповідь на виклик (подія типу 2). Сервер без автентифікатора
// (`sv.internet 0`) перевіряє лише мережеву версію — решту блоку не читає.
//
// Мережева версія — та сама, що й у запиті на під'єднання: у рушії це
// `BuildNrUtil::getNetVersionNumber()`, і вона повертає рівно kGameVersion.
std::vector<std::byte> writeChallengeResponse(std::uint8_t connectionId,
                                              const ExtendedHeader& header, std::uint8_t batch);

// Подія з блоком даних: спершу заголовок (тип блока й повний розмір),
// далі шматки не більші за 255 байтів. Коли зібрано весь блок, рушій
// віддає його в `GameServer::handleDataBlock`.
std::vector<std::byte> writeDataBlockHeader(std::uint8_t connectionId,
                                            const ExtendedHeader& header, std::uint8_t batch,
                                            std::uint32_t blockType, std::uint32_t size);
std::vector<std::byte> writeDataBlockChunk(std::uint8_t connectionId, const ExtendedHeader& header,
                                           std::uint8_t batch, std::span<const std::byte> chunk);

// Подія «підніми в себе оцю подію» (`PostRemoteEvent`, тип 11):
//   4 біти категорія, 32 біти номер події, 32 біти затримка (float),
//   8 бітів довжина даних, далі байти.
//
// Категорію 6 сервер віддає в `GameServer::handleNetworkEvent`
// (`GameServer::handleEvent` порівнює саме з шісткою; двійка там — HUD).
// Номер 2 у таблиці переходів веде в `clientLoadComplete`: поки клієнт її
// не надішле, стан з'єднання лишається нижчим за поріг `isClientReady`,
// і сервер не шле ні об'єктів світу, ні потоку привидів.
inline constexpr std::uint32_t kPostRemoteEvent = 11;
inline constexpr std::uint32_t kNetworkCategory = 6;
inline constexpr std::uint32_t kNetDataBlockReady = 1;
inline constexpr std::uint32_t kNetLoadComplete = 2;
inline constexpr std::uint32_t kNetStartSimulation = 3;
inline constexpr std::uint32_t kNetDatabaseComplete = 4;
inline constexpr std::uint32_t kNetSelectSpawnGroup = 6;
inline constexpr std::uint32_t kNetSelectTeam = 7;
inline constexpr std::uint32_t kNetSelectKit = 8;

// `value` передається у корисних даних 32-бітним числом — так його
// читають ті події, що несуть вибір (команда, набір, місце появи).
std::vector<std::byte> writePostRemoteEvent(std::uint8_t connectionId,
                                            const ExtendedHeader& header, std::uint8_t batch,
                                            std::uint32_t category, std::uint32_t event,
                                            std::optional<std::int32_t> value = std::nullopt);

// Перевірка вмісту (`ContentCheckEvent`, тип 46): три хеші по 128 бітів.
// Сервер звіряє їх у `GameServer::onContentCheckEvent` і лише після
// збігу виставляє клієнтові `contentValid`. Без цього
// `clientSendDatabaseComplete` ставить його в чергу на від'єднання, і
// стан з'єднання не доростає до потрібного для потоку привидів.
//
// Порядок хешів:
//   1. те, що сервер рахує сам при старті (`runMiscChecksum`);
//   2. рядок із `mods/<мод>/std_archive.md5`;
//   3. рядок із `mods/<мод>/levels/<рівень>/archive.md5`.
//
// Номер рядка в обох файлах дає `MapInfo::getChallengeOrdinal()`.
inline constexpr std::uint32_t kContentCheckEvent = 46;

// Перевірку сервер розглядає лише коли стан з'єднання вже більший за
// одиницю, тобто після NELoadComplete. Інакше він її мовчки ігнорує.
std::vector<std::byte> writeContentCheckEvent(std::uint8_t connectionId,
                                              const ExtendedHeader& header, std::uint8_t batch,
                                              const std::array<std::byte, 16>& misc,
                                              const std::array<std::byte, 16>& archives,
                                              const std::array<std::byte, 16>& level);

// Читає файл відбитків: «номер md5» для архівів, «назва номер md5» для
// рівня. nullopt — рядка з таким номером немає.
std::optional<std::array<std::byte, 16>> readFingerprint(std::string_view text, int ordinal);

// Хеш імені, який рейтинговий сервер звіряє з переданим: h = 0x1505, далі
// для кожного символу в нижньому регістрі h = h * 0x21 ^ c.
std::uint32_t clientInfoNameHash(const std::string& name);

std::vector<std::byte> buildClientInfo(const ClientInfo& info);

// Відповідь на пінг. `time` — те саме число, що надіслав сервер: за
// різницею він рахує затримку.
std::vector<std::byte> writePingResponse(std::uint8_t connectionId, const ExtendedHeader& header,
                                         std::uint32_t time);

// Короткий пакет із самого заголовка: підтвердження, від'єднання, пінг.
std::vector<std::byte> writeShortPacket(PacketKind kind, std::uint8_t connectionId);

// Розбирає те, що прийшло від сервера. nullopt — пакет коротший за заголовок.
std::optional<Incoming> readPacket(std::span<const std::byte> data);

}  // namespace obf2::net::bf2
