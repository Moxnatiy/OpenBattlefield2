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
// Підтвердження від сервера: гравець з'явився. У знятому трафіку
// оригіналу вона приходить за 100 мс після `NESelectSpawnGroup`, і це
// найпряміша ознака, що поява вдалася.
inline constexpr std::uint32_t kNetPlayerSpawned = 9;

// Один набір дій гравця — те, що клієнт шле серверу тридцять разів на
// секунду. Розкладка з `PlayerActionManager::processReceivedPacket`
// (0x44d670) і звірена зі знятим трафіком оригіналу:
//
//   1 біт                 чи є дії
//   4 біти                скільки наборів у пакеті (оригінал шле три)
//   9 бітів               номер (у дампі змінюється; призначення не
//                         з'ясоване)
//   1 біт знак + 31 біт   лічильник вводу, росте на одиницю за пакет
//   далі на кожен набір:
//      6 разів: 1 біт знака + 15 бітів значення
//      32 біти  маска кнопок
//      9 бітів  (у дампі завжди нуль)
//      1 біт    прапорець (у дампі одиниця)
//
// Що означають осі, видно з дампу: коли гравець біг уперед, третя ось
// стояла на 99, а решта на нулі; п'ята й шоста весь час дрібно
// смикалися — це миша. Маска кнопок у ті самі секунди дорівнювала 32,
// коли гравець тримав спринт, і нулю, коли відпускав.
struct PlayerAction {
  std::int16_t axes[6] = {0, 0, 0, 0, 0, 0};
  std::uint32_t buttons = 0;
  bool flag = true;
};

// Осі й кнопки названі не з дампу, а з таблиці, якою рушій реєструє
// сталі керування (BF2.exe, 0x6904c0 і далі: рядок імені, потім його
// номер у EDX). Порядок звідти:
//
//   0  c_PIYaw          4  c_PIMouseLookX     8  c_PIFire
//   1  c_PIPitch        5  c_PIMouseLookY     9  c_PIAction
//   2  c_PIRoll         6  c_PICameraX       10  c_PIUse
//   3  c_PIThrottle     7  c_PICameraY       13  c_PISprint
//
// У пакет ідуть перші шість — рівно осі 0..5.
//
// Для піхоти розкладку задає `Settings/Controls.con`:
//
//   ControlMap.addKeysToAxisMapping c_PIYaw      IDKey_D IDKey_A
//   ControlMap.addKeysToAxisMapping c_PIThrottle IDKey_W IDKey_S
//   ControlMap.addKeyToTriggerMapping c_PIAction IDKey_Space
//   ControlMap.addKeyToTriggerMapping c_PISprint IDKey_LeftShift
//
// Тобто вбік солдат ходить **віссю рискання**: окремої осі для кроку
// вбік у рушія немає. Доки ми цього не знали, вбік було не піти взагалі.
inline constexpr int kAxisYaw = 0;       // D/A — крок вбік у піхоти
inline constexpr int kAxisPitch = 1;
inline constexpr int kAxisRoll = 2;
inline constexpr int kAxisThrottle = 3;  // W/S — хід уперед-назад
inline constexpr int kAxisMouseX = 4;    // c_PIMouseLookX
inline constexpr int kAxisMouseY = 5;    // c_PIMouseLookY
// Стара назва третьої осі — лишаємо, щоб не переписувати виклики.
inline constexpr int kAxisForward = kAxisThrottle;
// Повний хід у дампі — рівно 99.
inline constexpr std::int16_t kAxisFull = 99;

// На дроті вісь їде цілим числом, а в рушії вона float. Множник — **100**:
// `PlayerAction::set` кладе осі як int16 (`BF2.exe`, 0x5bc890), а зворотне
// перетворення поруч (0x5bc5f0) робить рівно `(float)value * 0.01`.
//
// Звідси й «повний хід = 99»: вісь газу дійшла до 0.99.
inline constexpr float kAxisWireScale = 100.0f;

// Маска кнопок: біт = номер сталої мінус номер `c_PIFire`. Перевірка
// сходиться з трафіком: спринт це 13 - 8 = 5, а саме біт 5 (значення 32)
// стояв у дампі, поки гравець тримав Shift.
inline constexpr std::uint32_t kButtonFire = 1u << 0;    // c_PIFire   (8)
inline constexpr std::uint32_t kButtonAction = 1u << 1;  // c_PIAction (9) — стрибок
inline constexpr std::uint32_t kButtonUse = 1u << 2;     // c_PIUse    (10)
inline constexpr std::uint32_t kButtonSprint = 1u << 5;  // c_PISprint (13)

// Увесь потік дій із пакета.
struct PlayerActions {
  std::uint32_t number = 0;  // 9 бітів на початку
  std::int32_t tick = 0;     // лічильник вводу
  std::vector<PlayerAction> actions;
};

// Розкладка потоку дій — **один опис на обидва напрями**.
//
// Те саме тіло і читає, і пише: різницю робить курсор (`ReadCursor` чи
// `WriteCursor` із `bitstream.h`). Доки опис один, складач і розбирач не
// можуть розійтися — а поки їх було двоє, кожне поле доводилося
// вписувати двічі.
//
// Поля — з `PlayerActionManager::processReceivedPacket` (0x44d670).
template <typename Cursor>
bool serializePlayerActions(Cursor& cursor, PlayerActions& stream) {
  std::uint32_t count = static_cast<std::uint32_t>(stream.actions.size());
  if (!cursor.bits(count, 4)) return false;
  if (!cursor.bits(stream.number, 9)) return false;
  // На читанні це створює потрібну кількість наборів, на записі не
  // змінює нічого: там count уже дорівнює розміру.
  if (count > 15) return false;
  stream.actions.resize(count);

  if (count > 0 && !cursor.signedBits(stream.tick, 31)) return false;

  for (PlayerAction& action : stream.actions) {
    for (std::int16_t& axis : action.axes) {
      std::int32_t value = axis;
      if (!cursor.signedBits(value, 15)) return false;
      axis = static_cast<std::int16_t>(value);
    }
    if (!cursor.bits(action.buttons, 32)) return false;
    std::uint32_t spare = 0;  // у знятому трафіку завжди нуль
    if (!cursor.bits(spare, 9)) return false;
    if (!cursor.flag(action.flag)) return false;
  }
  return true;
}

// Пакет із самими діями: подій у ньому немає.
std::vector<std::byte> writePlayerActions(std::uint8_t connectionId, const ExtendedHeader& header,
                                          const PlayerActions& stream);

// Прочитати потік дій із пакета даних. nullopt — це не пакет даних або
// дій у ньому немає.
//
// Тим самим розбирачем ми читаємо і власні пакети, і зняті з
// оригінального клієнта.
std::optional<PlayerActions> readPlayerActions(std::span<const std::byte> packet);

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
