#include "obf2/server/game_client.h"

namespace obf2::server {
namespace {

// A soldier cannot travel further than this in one packet: over 1/30 s even at a
// run it is less than a metre. So this is a teleport, not movement.
constexpr float kTeleportDistance = 5.0f;

}  // namespace
namespace {

constexpr std::size_t kPacketBytes = 1200;

// The input is sent at the rate the server runs its simulation.
constexpr float kInputInterval = 1.0f / 30.0f;

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
  log_.push_back("connection request sent from \"" + playerName_ + "\"");
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
      log_.push_back("accepted: id " + std::to_string(playerId_) + ", level " + levelName_ +
                     ", mode " + gameMode_);

      // We acknowledge at once — that is exactly what the server waits for before
      // it starts sending the world.
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
      log_.push_back("denied: " + std::string(denyReason_
                                                      ? net::denyReasonName(*denyReason_)
                                                      : std::string_view("unknown reason")));
      return;
    }

    case net::PacketType::Data: {
      const auto updates = net::readObjectUpdates(reader, Vec3f{0.0f, 0.0f, 0.0f});
      if (!updates) return;
      for (const net::ObjectUpdate& update : *updates) {
        RemoteObject& object = objects_[update.objectId];
        object.id = update.objectId;
        // The template's name arrives only on spawn; we do not overwrite it later.
        if (update.spawn) object.templateName = update.templateName;
        // The previous state is kept for smoothing. But a jump across half the
        // world is not movement, it is appearing somewhere new: it must not be
        // smoothed, otherwise the camera flies through the level and it looks as
        // though there is no collision at all.
        const Vec3f jump = update.position - object.position;
        const bool teleported = length(jump) > kTeleportDistance;
        if (!update.spawn && !teleported) {
          object.previousPosition = object.position;
          object.moved = true;
        } else {
          object.previousPosition = update.position;
          object.moved = false;
        }
        object.position = update.position;
        object.rotation = update.rotation;
      }
      interpolation_ = 0.0f;
      if (state_ == ClientState::Accepted) {
        state_ = ClientState::InWorld;
        log_.push_back("the first world packet received");
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

void GameClient::sendInput() {
  if (connection_ == nullptr || state_ != ClientState::InWorld) return;

  std::vector<std::byte> buffer(kPacketBytes);
  net::BitWriter writer(buffer);

  net::PlayerInput input = input_;
  // Numbering starts at one: zero on the server means "nothing has arrived yet".
  input.sequence = ++inputSequence_;
  if (inputSequence_ > 0xFFFF) inputSequence_ = 1;

  if (!net::writePlayerInput(writer, input)) return;
  if (connection_->send(std::span(buffer).first(writer.byteSize()))) ++inputsSent_;
}

Vec3f GameClient::interpolatedPosition(std::uint32_t objectId) const {
  const auto found = objects_.find(objectId);
  if (found == objects_.end()) return Vec3f{};

  const RemoteObject& object = found->second;
  if (!object.moved) return object.position;

  const float t = interpolation_ < 0.0f ? 0.0f : (interpolation_ > 1.0f ? 1.0f : interpolation_);
  return object.previousPosition + (object.position - object.previousPosition) * t;
}

void GameClient::tick(float deltaSeconds) {
  if (connection_ == nullptr) return;
  while (auto packet = connection_->receive()) handlePacket(*packet);

  // We approach the last state received over exactly the time between packets.
  interpolation_ += deltaSeconds / kInputInterval;

  sendAccumulator_ += deltaSeconds;
  while (sendAccumulator_ >= kInputInterval) {
    sendInput();
    sendAccumulator_ -= kInputInterval;
  }
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
