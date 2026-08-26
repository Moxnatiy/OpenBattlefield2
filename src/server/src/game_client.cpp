#include "obf2/server/game_client.h"

namespace obf2::server {
namespace {

constexpr std::size_t kPacketBytes = 1200;

}  // namespace

std::string_view clientStateName(ClientState state) {
  switch (state) {
    case ClientState::Disconnected: return "Disconnected";
    case ClientState::Requesting: return "Requesting";
    case ClientState::Accepted: return "Accepted";
    case ClientState::InWorld: return "InWorld";
    case ClientState::Denied: return "Denied";
  }
  return "?";
}

bool GameClient::connect() {
  if (connection_ == nullptr) return false;

  std::vector<std::byte> buffer(kPacketBytes);
  net::BitWriter writer(buffer);

  net::ConnectionRequest request;
  request.playerName = playerName_;
  if (!net::writeConnectionRequest(writer, request)) return false;
  if (!connection_->send(std::span(buffer).first(writer.byteSize()))) return false;

  state_ = ClientState::Requesting;
  log_.push_back("надіслано запит під'єднання від \"" + playerName_ + "\"");
  return true;
}

void GameClient::handlePacket(const net::Packet& packet) {
  net::BitReader reader(packet);
  const auto header = reader.readBasicHeader();
  if (!header) return;

  switch (static_cast<net::PacketType>(header->type)) {
    case net::PacketType::ConnectionAccept: {
      const auto accept = net::readConnectionAccept(reader);
      if (!accept) return;
      playerId_ = accept->playerId;
      levelName_ = accept->levelName;
      gameMode_ = accept->gameMode;
      state_ = ClientState::Accepted;
      log_.push_back("прийнято: id " + std::to_string(playerId_) + ", рівень " + levelName_ +
                     ", режим " + gameMode_);

      // Підтверджуємо одразу — сервер чекає саме на це, щоб почати
      // надсилати світ.
      std::vector<std::byte> buffer(kPacketBytes);
      net::BitWriter writer(buffer);
      if (net::writeConnectionAcknowledge(writer)) {
        connection_->send(std::span(buffer).first(writer.byteSize()));
      }
      return;
    }

    case net::PacketType::ConnectionDenied: {
      denyReason_ = net::readConnectionDenied(reader);
      state_ = ClientState::Denied;
      log_.push_back("відмовлено: " + std::string(denyReason_
                                                      ? net::denyReasonName(*denyReason_)
                                                      : std::string_view("невідома причина")));
      return;
    }

    case net::PacketType::Data: {
      const auto updates = net::readObjectUpdates(reader, Vec3f{0.0f, 0.0f, 0.0f});
      if (!updates) return;
      for (const net::ObjectUpdate& update : *updates) {
        RemoteObject& object = objects_[update.objectId];
        object.id = update.objectId;
        // Ім'я шаблону приходить лише при появі; далі його не перезаписуємо.
        if (update.spawn) object.templateName = update.templateName;
        object.position = update.position;
        object.rotation = update.rotation;
      }
      if (state_ == ClientState::Accepted) {
        state_ = ClientState::InWorld;
        log_.push_back("отримано перший пакет світу");
      }
      return;
    }

    case net::PacketType::Disconnect:
      state_ = ClientState::Disconnected;
      return;

    default:
      return;
  }
}

void GameClient::tick(float) {
  if (connection_ == nullptr) return;
  while (auto packet = connection_->receive()) handlePacket(*packet);
}

void GameClient::disconnect() {
  if (connection_ == nullptr) return;

  std::vector<std::byte> buffer(kPacketBytes);
  net::BitWriter writer(buffer);
  if (net::writeDisconnect(writer)) {
    connection_->send(std::span(buffer).first(writer.byteSize()));
  }
  connection_->close();
  state_ = ClientState::Disconnected;
}

}  // namespace obf2::server
