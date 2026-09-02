#pragma once
// Ігрові події BF2: таблиця розмірів і розбір того, що ми вміємо.
//
// Таблиця в `bf2_events.inc` згенерована з бінаря сервера
// (`tools/linuxded/gen_events.py`): номер типу береться з `getType()`,
// розміри полів — із `deSerialize`. Перезняти можна тією ж командою.
//
// Головне, заради чого вона потрібна, — вміти **пропустити** будь-яку
// подію рівно на стільки бітів, скільки вона займає. Пакет везе їх одну
// за одною, і якщо на незнайомій спіткнутися, далі йде сміття: тип
// події не буває більшим за 69, а зі зсуву вилазять 80 чи 106.
//
// Частину подій пропустити за таблицею не можна: у них поля лежать за
// умовою, і довжина залежить від вмісту. Такі позначені окремо, і розбір
// для них написано руками.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {

// Назва події за номером; порожньо, якщо номера немає в реєстрі.
std::string_view eventName(std::uint32_t type);

// Чи залежить довжина події від вмісту.
bool eventIsBranchy(std::uint32_t type);

// Об'єкт світу з `CreateObjectEvent` (тип 6).
//
// Розкладка з `bitfields.py --blocks CreateObjectEvent::deSerialize`:
// прапорець після перших трьох полів розводить дві **взаємно виключні**
// гілки, а полярність видно в коді (`cmpl $0x1; jne`).
struct CreateObject {
  // Номер шаблона — це порядок його створення, а не хеш назви:
  // `ObjectTemplateManager::createTemplate` робить `setId(лічильник)` і
  // одразу `лічильник++`. Отже щоб зіставити номер із назвою, треба
  // читати ті самі файли в тому самому порядку, що й гра. Перевірено:
  // між запусками номери збігаються повністю (27 з 27).
  std::uint32_t templateId = 0;
  std::uint16_t networkId = 0;
  std::uint32_t field2 = 0;
  std::optional<std::uint8_t> field8;
  std::optional<Vec3f> position;
  // Кути Ейлера ZXY у градусах — той самий вигляд, що й у `.con`
  // (`getRotationZXY` поруч у `clientSendDatabase`, і три числа там
  // додатково міняють знак).
  std::optional<Vec3f> rotation;
};

// Гравець із `CreatePlayerEvent` (тип 5). Ім'я сервер шле в 32 байтах.
struct CreatePlayer {
  std::uint32_t team = 0;
  std::uint32_t squad = 0;
  std::uint32_t id = 0;
  std::string name;
};

// Шматок блока даних із `DataBlockEvent` (тип 4). Блок їде двома видами
// подій: спершу заголовок із типом і повним розміром, далі шматки.
struct DataBlockPiece {
  bool header = false;
  std::uint32_t blockType = 0;  // лише в заголовку
  std::uint32_t size = 0;       // лише в заголовку
  std::vector<std::byte> chunk;
};

// Відомості про рівень — блок типу 5, який сервер шле одразу після
// реєстрації (`GameServer::sendClientMapInfo` віддає `MapInfo`).
struct MapInfo {
  std::string levelName;
  std::string gameMode;
  int size = 0;
  // Перше число блока. Схоже на «номер виклику» — той самий рядок у
  // файлах відбитків, яким сервер перевіряє вміст. Перевіряється на
  // живому сервері, а не взяте на віру.
  std::uint32_t first = 0;
};

inline constexpr std::uint32_t kMapInfoBlock = 5;

std::optional<MapInfo> parseMapInfo(std::span<const std::byte> block);

// Блок типу 2 — це **справжній** `MapInfo`, той, що складає
// `MapInfo::updateNetBuffer` (0x4168b0). Блок 5 поруч простіший і несе
// лише назву рівня, режим і розмір.
//
// Порядок полів узято прямо з `updateNetBuffer`; числа там пишуться не
// цілим словом, а знаком і 31 бітом:
//
//   u16 довжина + рядок   режим гри
//   u16 довжина + рядок   шлях до рівнів
//   u16 довжина + рядок   назва рівня
//   1 біт знак + 31 біт   скільки місць
//   1 біт                 чи є командир
//   1 біт знак + 31 біт   **номер виклику**
//
// Номер виклику потрібен для перевірки вмісту: сервер обирає його
// випадково при завантаженні рівня (`GameServer::loadPath`,
// `rand() % 10`) і звіряє наші хеші саме з тим рядком файлів відбитків.
inline constexpr std::uint32_t kMapInfoNetBuffer = 2;

struct ServerMapInfo {
  std::string gameMode;
  std::string levelPath;
  std::string levelName;
  int maxPlayers = 0;
  bool commanderEnabled = false;
  int challengeOrdinal = -1;
};

std::optional<ServerMapInfo> parseMapInfoNetBuffer(std::span<const std::byte> block);

// Група появи з `CreateSpawnGroupEvent` (тип 57). Саме її номер чекає
// подія `NESelectSpawnGroup`, коли гравець тисне DONE.
//
// Розкладка знята з `CreateSpawnGroupEvent::deSerialize` (0x424520 у
// Linux-сервері) і звірена з конструктором, чия сигнатура збереглася в
// символах:
//
//   CreateSpawnGroupEvent(unsigned char, unsigned short, int,
//                         bool, bool, bool, unsigned char, unsigned char)
//
// Дротовий порядок інший, ніж у конструктора, — він видно з послідовності
// читань і зсувів, куди вони лягають:
//
//   8 біт  -> +0x10  u8   перший аргумент
//   4 біти -> +0x14  int  третій, з `group->[0x38]()`
//   1 біт  -> +0x18  bool з `group->[0x58](0)`
//   1 біт  -> +0x19  bool поле 0x9a групи
//   1 біт  -> +0x1a  bool поле 0xa0 групи
//   8 біт  -> +0x1b  u8   \ разом це `SpawnGroup::getUnsignedWorldPosition`
//   8 біт  -> +0x1c  u8   /  — місце групи, спаковане у два байти
//   16 біт -> +0x1e  u16  другий аргумент, поле 0x10 групи
//
// Хто зі створює, видно теж: `SpawnManager::createSpawnGroupOnClients`
// (0x4b96e0).
struct CreateSpawnGroup {
  std::uint8_t first = 0;      // 8 біт
  std::uint32_t small = 0;     // 4 біти
  bool flag1 = false;
  bool flag2 = false;
  bool flag3 = false;
  std::uint8_t worldX = 0;     // спаковане місце, вісь 1
  std::uint8_t worldZ = 0;     // спаковане місце, вісь 2
  std::uint16_t id = 0;        // 16 біт
};

// Розпакування місця групи. Пакує його `SpawnGroup::getUnsignedWorldPosition`
// (0x4b94b0), і арифметика там така:
//
//   half = GLSWorldSizeX / 2                 (стала за замовчуванням 1024,
//                                             рівень ставить свою)
//   pos >  half  -> 255
//   pos < -half  -> 0
//   інакше  байт = (int)((pos + half) / (2*half) * 255)
//
// Множник 255 лежить сталою за 0xb355bc. Отже назад:
inline float spawnGroupWorldPos(std::uint8_t packed, float worldSize) {
  return static_cast<float>(packed) / 255.0f * worldSize - worldSize * 0.5f;
}

// Одна подія з пакета: номер типу і те з неї, що ми вже розбираємо.
struct Event {
  std::uint32_t type = 0;
  std::optional<CreateObject> object;
  std::optional<CreatePlayer> player;
  std::optional<DataBlockPiece> block;
  std::optional<CreateSpawnGroup> spawnGroup;
};

// Складає блоки з шматків, які приходять подіями.
class DataBlockAssembler {
 public:
  // Повертає зібраний блок, коли його дочитано до кінця.
  std::optional<std::pair<std::uint32_t, std::vector<std::byte>>> feed(const DataBlockPiece& piece);

 private:
  std::uint32_t type_ = 0;
  std::uint32_t expected_ = 0;
  std::vector<std::byte> data_;
};

// Пересуває читача рівно на довжину події вказаного типу.
// false — тип невідомий або пакет обірвався.
bool skipEvent(BitReader& reader, std::uint32_t type);

// Розбирає одну подію: що вміємо — заповнює, решту просто пропускає.
std::optional<Event> readEvent(BitReader& reader);

// Читає файл зі спійманими пакетами (`tools/linuxded/capture.py --out`):
// для кожного пакета u32 довжина, далі байти.
std::vector<std::vector<std::byte>> loadCapture(const std::string& path);

// Діагностика: чи дочитали ми пакет до прапорця потоку привидів і що в
// ньому. nullopt — розбір подій обірвався раніше (значить, ламаємось ми);
// false — сервер сам каже, що привидів у пакеті немає.
std::optional<bool> ghostFlag(std::span<const std::byte> packet);

// Заголовок потоку привидів — те, що йде після подій у пакеті даних.
// Час рахується тактами по 1/30 секунди.
struct GhostHeader {
  std::uint32_t time = 0;
  std::uint8_t records = 0;
  bool controlObjectState = false;
};

// Один запис потоку привидів (`GhostManager::readData`):
//   2 біти вид, 16 бітів мережевий номер;
//   вид 1 — оновлення стану: 1 біт, 11 бітів довжини вмісту, сам вміст;
//   вид 0 — більше нічого; вид 3 — об'єкт зникає;
//   вид 2 рушій вважає помилкою потоку.
//
// Довжина в записі дає змогу пропустити його, не розбираючи вмісту, —
// саме так і робить рушій, коли не знає об'єкта.
struct GhostRecord {
  std::uint32_t kind = 0;
  std::uint16_t networkId = 0;
  std::uint32_t payloadBits = 0;
};

// Ширина поля довжини. У рушії вона обчислюється на льоту, а на дроті
// це рівно одинадцять бітів: із нею весь спійманий зразок розбирається
// до останнього байта, з будь-якою іншою — жоден пакет.
inline constexpr unsigned kGhostLengthBits = 11;

// Читає заголовок потоку привидів із пакета даних, пройшовши події.
// nullopt — привидів у пакеті немає або якусь подію ще не вміємо
// пропустити на потрібну довжину.
std::optional<GhostHeader> readGhostHeader(std::span<const std::byte> packet);

// Записи потоку привидів. Порожньо, якщо привидів немає або в пакеті
// стоїть прапорець стану керованого об'єкта — той стан іде перед
// записами і поки не розібраний.
std::vector<GhostRecord> readGhostRecords(std::span<const std::byte> packet);

// Проходить пакет даних і повертає всі події з нього.
// Порожньо — це не пакет даних або він обірвався на першій же події.
std::vector<Event> readEvents(std::span<const std::byte> packet);

}  // namespace obf2::net::bf2
