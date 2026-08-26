#include "obf2/net/session.h"

namespace obf2::net {
namespace {

// Скільки об'єктів може бути в одному пакеті оновлення. Обмеження потрібне
// не для економії, а для безпеки: без нього зіпсований лічильник змусив би
// нас виділяти скільки завгодно пам'яті.
constexpr unsigned kUpdateCountBits = 12;
constexpr std::uint32_t kMaxUpdatesPerPacket = (1u << kUpdateCountBits) - 1u;

constexpr unsigned kObjectIdBits = 16;
constexpr unsigned kPlayerIdBits = 8;
constexpr unsigned kDenyReasonBits = 8;

bool writeHeader(BitWriter& writer, PacketType type) {
  return writer.writeBasicHeader(BasicHeader{static_cast<std::uint32_t>(type), 0});
}

}  // namespace

std::string_view denyReasonName(DenyReason reason) {
  switch (reason) {
    case DenyReason::ServerFull: return "сервер заповнений";
    case DenyReason::WrongVersion: return "невідповідна версія";
    case DenyReason::Banned: return "заблоковано";
  }
  return "невідома причина";
}

// --- запис ---------------------------------------------------------------

bool writeConnectionRequest(BitWriter& writer, const ConnectionRequest& request) {
  return writeHeader(writer, PacketType::ConnectionRequest) &&
         writer.writeBits(request.protocolVersion, 32) &&
         writer.writeString(request.playerName, kPlayerNameLength);
}

bool writeConnectionAccept(BitWriter& writer, const ConnectionAccept& accept) {
  return writeHeader(writer, PacketType::ConnectionAccept) &&
         writer.writeBits(accept.playerId, kPlayerIdBits) &&
         writer.writeString(accept.levelName, kLevelNameLength) &&
         writer.writeString(accept.gameMode, kGameModeLength);
}

bool writeConnectionDenied(BitWriter& writer, DenyReason reason) {
  return writeHeader(writer, PacketType::ConnectionDenied) &&
         writer.writeBits(static_cast<std::uint32_t>(reason), kDenyReasonBits);
}

bool writeConnectionAcknowledge(BitWriter& writer) {
  return writeHeader(writer, PacketType::ConnectionAcknowledge);
}

bool writeDisconnect(BitWriter& writer) { return writeHeader(writer, PacketType::Disconnect); }

bool writeObjectUpdates(BitWriter& writer, const std::vector<ObjectUpdate>& updates,
                        const Vec3f& reference) {
  if (updates.size() > kMaxUpdatesPerPacket) return false;
  if (!writeHeader(writer, PacketType::Data)) return false;
  if (!writer.writeBits(static_cast<std::uint32_t>(updates.size()), kUpdateCountBits)) return false;

  for (const ObjectUpdate& update : updates) {
    if (!writer.writeBits(update.objectId, kObjectIdBits)) return false;
    // Ім'я шаблону їде лише при появі: далі об'єкт відомий за id.
    if (!writer.writeBool(update.spawn)) return false;
    if (update.spawn && !writer.writeString(update.templateName, kTemplateNameLength)) return false;

    if (!writer.writeCompressedVector(update.position, reference, kPositionPrecision)) return false;
    // Кути в градусах: діапазон невеликий, тому вистачає 16 біт на вісь.
    for (const float angle : {update.rotation.x, update.rotation.y, update.rotation.z}) {
      const auto quantized = static_cast<std::uint32_t>((angle + 360.0f) * 32.0f) & 0xFFFFu;
      if (!writer.writeBits(quantized, 16)) return false;
    }
  }
  return true;
}

// --- читання -------------------------------------------------------------

std::optional<ConnectionRequest> readConnectionRequest(BitReader& reader) {
  ConnectionRequest request;
  const auto version = reader.readBits(32);
  if (!version) return std::nullopt;
  request.protocolVersion = *version;

  const auto name = reader.readString(kPlayerNameLength);
  if (!name) return std::nullopt;
  request.playerName = *name;
  return request;
}

std::optional<ConnectionAccept> readConnectionAccept(BitReader& reader) {
  ConnectionAccept accept;
  const auto id = reader.readBits(kPlayerIdBits);
  if (!id) return std::nullopt;
  accept.playerId = *id;

  const auto level = reader.readString(kLevelNameLength);
  const auto mode = reader.readString(kGameModeLength);
  if (!level || !mode) return std::nullopt;
  accept.levelName = *level;
  accept.gameMode = *mode;
  return accept;
}

std::optional<DenyReason> readConnectionDenied(BitReader& reader) {
  const auto reason = reader.readBits(kDenyReasonBits);
  if (!reason) return std::nullopt;
  return static_cast<DenyReason>(*reason);
}

std::optional<std::vector<ObjectUpdate>> readObjectUpdates(BitReader& reader,
                                                           const Vec3f& reference) {
  const auto count = reader.readBits(kUpdateCountBits);
  if (!count) return std::nullopt;

  std::vector<ObjectUpdate> updates;
  updates.reserve(*count);

  for (std::uint32_t i = 0; i < *count; ++i) {
    ObjectUpdate update;
    const auto id = reader.readBits(kObjectIdBits);
    const auto spawn = reader.readBool();
    if (!id || !spawn) return std::nullopt;
    update.objectId = *id;
    update.spawn = *spawn;

    if (update.spawn) {
      const auto name = reader.readString(kTemplateNameLength);
      if (!name) return std::nullopt;
      update.templateName = *name;
    }

    const auto position = reader.readCompressedVector(reference, kPositionPrecision);
    if (!position) return std::nullopt;
    update.position = *position;

    float* angles[3] = {&update.rotation.x, &update.rotation.y, &update.rotation.z};
    for (float* angle : angles) {
      const auto quantized = reader.readBits(16);
      if (!quantized) return std::nullopt;
      *angle = static_cast<float>(*quantized) / 32.0f - 360.0f;
    }
    updates.push_back(std::move(update));
  }
  return updates;
}

}  // namespace obf2::net
