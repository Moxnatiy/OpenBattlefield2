#pragma once
// Рукостискання й обмін станом між клієнтом і сервером.
//
// Послідовність узята з протоколу оригіналу (типи пакетів звірені з
// Refractor-2-BitStream-Emulator, який під'єднувався до живих серверів BF2):
//
//   клієнт -> сервер : ConnectionRequest   (ім'я гравця, версія протоколу)
//   сервер -> клієнт : ConnectionAccept    (id гравця, ім'я рівня, режим)
//                      або ConnectionDenied (код причини)
//   клієнт -> сервер : ConnectionAcknowledge
//   далі             : Data / PingRequest / PingResponse / Disconnect
//
// Побайтової сумісності з оригінальними серверами ми не обіцяємо (див.
// docs/TODO.md) — але формат і послідовність тримаємо ті самі, щоб ця
// сумісність лишалася досяжною.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/net/bitstream.h"
#include "obf2/net/connection.h"

namespace obf2::net {

// Версія нашого протоколу. Оригінальна гра сюди клала свою.
inline constexpr std::uint32_t kProtocolVersion = 1;

// Причина відмови у ConnectionDenied.
enum class DenyReason : std::uint32_t {
  ServerFull = 1,
  WrongVersion = 2,
  Banned = 3,
};

std::string_view denyReasonName(DenyReason reason);

struct ConnectionRequest {
  std::uint32_t protocolVersion = kProtocolVersion;
  std::string playerName;
};

struct ConnectionAccept {
  std::uint32_t playerId = 0;
  std::string levelName;
  std::string gameMode;
};

// Поява або оновлення об'єкта у світі. Позиція йде стисненим вектором —
// тим самим, що і в оригіналі.
struct ObjectUpdate {
  std::uint32_t objectId = 0;
  std::string templateName;  // лише при появі
  Vec3f position;
  Vec3f rotation;
  bool spawn = false;
};

// Довжини рядків у пакетах фіксовані: так робить і оригінал, бо це дозволяє
// не писати окремо довжину.
inline constexpr std::size_t kPlayerNameLength = 24;
inline constexpr std::size_t kLevelNameLength = 32;
inline constexpr std::size_t kGameModeLength = 16;
// 64, а не 32: у грі трапляються імена до 36 символів
// ("fence_corrugated_3x12m_broken_parts"), і на 32 вони мовчки обрізалися —
// об'єкт приходив, але геометрії до нього вже не знаходилося.
inline constexpr std::size_t kTemplateNameLength = 64;

// Точність позицій у пакетах оновлення, у світових одиницях.
inline constexpr float kPositionPrecision = 0.01f;

// --- запис ---
bool writeConnectionRequest(BitWriter& writer, const ConnectionRequest& request);
bool writeConnectionAccept(BitWriter& writer, const ConnectionAccept& accept);
bool writeConnectionDenied(BitWriter& writer, DenyReason reason);
bool writeConnectionAcknowledge(BitWriter& writer);
bool writeDisconnect(BitWriter& writer);
bool writeObjectUpdates(BitWriter& writer, const std::vector<ObjectUpdate>& updates,
                        const Vec3f& reference);

// --- читання ---
std::optional<ConnectionRequest> readConnectionRequest(BitReader& reader);
std::optional<ConnectionAccept> readConnectionAccept(BitReader& reader);
std::optional<DenyReason> readConnectionDenied(BitReader& reader);
std::optional<std::vector<ObjectUpdate>> readObjectUpdates(BitReader& reader,
                                                           const Vec3f& reference);

}  // namespace obf2::net
