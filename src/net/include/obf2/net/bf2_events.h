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
#include <functional>
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
  // Номер групи, яким її називає сервер. Саме його чекає
  // `NESelectSpawnGroup` — перевірено знятим трафіком оригінального
  // клієнта: він шле `NESelectSpawnGroup = 2` на другий прапор, а не
  // 516 (мережевий номер) і не 402 (номер точки з рівня).
  std::uint8_t id = 0;         // 8 біт
  std::uint32_t team = 0;      // 4 біти
  bool flag1 = false;
  bool flag2 = false;
  bool flag3 = false;
  std::uint8_t worldX = 0;     // спаковане місце, вісь 1
  std::uint8_t worldZ = 0;     // спаковане місце, вісь 2
  // Мережевий номер групи — ним її знає система об'єктів. Для вибору
  // місця появи він **не** потрібен: оригінал шле не його.
  std::uint16_t networkId = 0;  // 16 біт
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

// Номер групи появи, найближчої до заданого місця.
//
// Це те, чим гравець тисне DONE: у нас на екрані стоїть кружечок біля
// прапора, а серверу треба назвати **його** номер групи. Зіставляємо за
// місцем, бо іншої спільної ознаки немає: номери контрольних точок із
// даних рівня (401..404 на Dalian) і номери груп сервера (515..518) не
// пов'язані ніяк.
//
// Точного збігу не буде й не має бути: місце групи — це середнє її точок
// появи, тож від прапора воно за десятки метрів. `distance` (якщо
// потрібна) віддає відстань у метрах — за нею видно, чи зіставлення
// взагалі осмислене.
//
// Нуль означає «не знайшли»: саме його сервер розуміє як «місце не
// обране» (`Player::getSpawnGroup() > 0`).
std::uint8_t nearestSpawnGroup(const std::vector<CreateSpawnGroup>& groups, float worldX,
                               float worldZ, float worldSize, float* distance = nullptr);

// Одна подія з пакета: номер типу і те з неї, що ми вже розбираємо.
// Хто чим керує (`EnterVehicleEvent`, тип 9) і хто вийшов
// (`ExitVehicleEvent`, тип 10).
//
// Солдат у BF2 — теж керований об'єкт, і гравець «займає» його так само,
// як техніку. Саме цією подією клієнт і дізнається, який об'єкт його:
// у знятому трафіку сервер відповідає на появу одним пакетом —
// `CreateObjectEvent` (номер 1794), одразу за ним
// `EnterVehicleEvent: гравець 1 -> об'єкт 1794`, далі набір і
// `NEPlayerSpawned = 1`. Свій номер гравця ми знаємо з
// `CreatePlayerEvent`, тож зіставлення однозначне.
struct EnterVehicle {
  std::uint32_t player = 0;
  std::uint16_t object = 0;
  bool flag = false;
};

// Мережева подія, яку сервер підняв у нас (`PostRemoteEvent`, тип 11).
// Ті, що несуть значення, везуть його чотирма байтами.
struct RemoteEvent {
  std::uint32_t category = 0;
  std::uint32_t number = 0;
  std::optional<std::int32_t> value;
};

struct Event {
  std::uint32_t type = 0;
  std::optional<CreateObject> object;
  std::optional<CreatePlayer> player;
  std::optional<DataBlockPiece> block;
  std::optional<CreateSpawnGroup> spawnGroup;
  // `PostRemoteEvent` (тип 11): сервер шле їх нам так само, як ми йому.
  // Найважливіша для нас — `NEPlayerSpawned`.
  std::optional<RemoteEvent> remote;
  std::optional<EnterVehicle> enter;
  // `ExitVehicleEvent` (тип 10): номер гравця, який вийшов.
  std::optional<std::uint32_t> exitPlayer;
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

// Стан керованого об'єкта — те, що сервер шле нам про **нашого** солдата.
//
// Він лежить у потоці привидів одразу після заголовка, коли в тому
// стоїть прапорець `controlObjectState`, і читає його окрема функція
// `GhostManager::readControlObjectState` (0x445c30). Початок її розкладки:
//
//   12 біт                 число на початку (призначення не з'ясоване)
//   1 біт знак + 31 біт    лічильник; при знаку значення заперечується
//   32, 32, 32             три числа — опорна точка стиснення
//   16 біт                 мережевий номер керованого об'єкта
//   1 біт ...              далі йде решта стану, яку ми ще не розбирали
//
// Дві речі тут варто назвати точно, бо вони видно прямо в коді функції.
//
// **Трійка чисел — це ТІЛЬКИ опорна точка стиснення, а не наше місце.**
// Перед нею стоїть `BitStream::resetCompressionVector`, а одразу після —
// `setCompressionVector` із нею ж (0x445cf7 і 0x445d4b). І більше з нею
// функція не робить **нічого**: серед усіх її викликів немає жодного,
// який клав би цю трійку в об'єкт. Місце солдата їде далі — у стані
// самого об'єкта (`setNetUpdate` через дескриптор, віртуальний виклик
// `*0x68(%rax)`), і пакується як різниця до цієї точки.
//
// Ми деякий час вважали трійку своїм місцем: вона ж точно збіглася з
// `gameLogic.setBeforeSpawnCamera`. Збіглася тому, що до появи керований
// об'єкт — саме та камера. Після появи точка виявилася округленою і
// стояла на метр вище землі, скільки б ми не йшли, — і солдата смикало
// вгору щоразу, як приходив пакет. Правдоподібне число виявилося гіршим
// за жодне.
//
// **16 біт після неї — мережевий номер нашого об'єкта.** Рушій одразу
// віддає його в `NetworkManager::getObject`, а результат — у `getSoldier`
// (0x445dc3 і 0x445e01). Це пряма відповідь на питання, хто наш солдат:
// здогадуватися за відстанню до прапора більше не треба.
struct ControlObjectState {
  std::uint32_t first = 0;
  std::int32_t counter = 0;
  // Назва навмисно не «position»: цим полем не можна рухати солдата.
  Vec3f compressionReference;
  std::uint16_t networkId = 0;
};

std::optional<ControlObjectState> readControlObjectState(std::span<const std::byte> packet);

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
  bool baseline = false;         // прапорець перед довжиною
  std::uint32_t stateMask = 0;   // 19 біт: які поля їдуть у вмісті
  // Місце, якщо в масці стоїть kObjectStatePosition.
  std::optional<Vec3f> position;
};

// Вміст запису читає мережевий клас об'єкта. Для всього, що має місце в
// світі, це `SimpleObjectNetworkable::setNetUpdate` (лінукс-сервер,
// 0x5d78a0), і починається він завжди однаково:
//
//   19 біт   маска стану — які поля є в цьому оновленні
//   якщо в масці стоїть біт 1:
//       стиснений вектор — місце об'єкта
//
// Маска читається за 0x5d7a17 (`readBits(..., 0x13)`), біт 1
// перевіряється першим (`testb $0x2` за 0x5d7a35), і в його гілці стоїть
// `BitStream::readCompressedVector` (0x5d845b). Точність — стала 0.0005
// із `.rodata` за 0xb47194, а не наш підбір.
//
// Які взагалі поля має цей клас, каже `getGhostStateMask` (0x5d6990):
// вона повертає 0x5849b. Решту полів ми ще не розбирали — і не мусимо:
// довжина запису дає пропустити хвіст, як це робить сам рушій.
inline constexpr unsigned kObjectStateMaskBits = 19;
inline constexpr std::uint32_t kObjectStatePosition = 0x2;
inline constexpr float kObjectPositionPrecision = 0.0005f;

// **Солдат читається інакше, і це не дрібниця.** Мережевий клас у нього
// свій — `SoldierNetworkable` (0x5dc640), і в нього:
//
//   * маска не 19 біт, а **21** (`readBits(..., 0x15)` за 0x5dc719).
//     Ширина маски — це кількість бітів у `getGhostStateMask`, а вона в
//     кожного класу своя: у простого об'єкта 0x5849b, у солдата
//     0x1950ff (0x5dabe0);
//   * місце вмикає **біт 7**, а не біт 1 (`testb %dl, %dl; js` за
//     0x5dc941 — перевірка знакового біта молодшого байта маски);
//   * опорна точка — не потік і не попереднє місце, а
//     `dice::hfe::nullVec`, тобто **нуль** (0x5dd367). Тож місце солдата
//     приходить по суті абсолютним;
//   * точність — 0.01 (стала за 0xb6d934), а не 0.0005.
//
// Через це чужі солдати в нас і не рухалися: ми читали їх розкладкою
// простого об'єкта. Хто з об'єктів солдат, ми знаємо напевно — сервер
// сам каже це подіями `CreatePlayerEvent` і `EnterVehicleEvent`.
inline constexpr unsigned kSoldierStateMaskBits = 21;
inline constexpr std::uint32_t kSoldierStatePosition = 0x80;
inline constexpr float kSoldierPositionPrecision = 0.01f;
// `dice::hfe::nullVec` — саме він стоїть опорою в розкладці солдата.
inline constexpr Vec3f kNullVec{};

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
// referenceFor — опорна точка для стисненого вектора **цього** об'єкта.
// Вимірювання показало, що різниця дрібна (метри), а не через півкарти:
// отже база — це попереднє відоме місце самого об'єкта, а не одна точка
// на весь потік. Хто кличе, той і пам'ятає останнє місце; поки об'єкт
// невідомий, за базу править місце з `CreateObjectEvent`.
//
// isSoldier — чи цей номер належить солдатові гравця: у солдата інша
// розкладка (див. вище), і без цього він читається як сміття.
std::vector<GhostRecord> readGhostRecords(
    std::span<const std::byte> packet,
    const std::function<Vec3f(std::uint16_t)>& referenceFor = {},
    const std::function<bool(std::uint16_t)>& isSoldier = {});

// Проходить пакет даних і повертає всі події з нього.
// Порожньо — це не пакет даних або він обірвався на першій же події.
std::vector<Event> readEvents(std::span<const std::byte> packet);

}  // namespace obf2::net::bf2
