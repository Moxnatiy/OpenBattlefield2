#pragma once
// The game client — the other half of the same pair.
//
// The client sends input at the rate the server runs its simulation, and smooths
// other players' positions between packets. Client-side prediction of its own
// movement does not exist yet — see docs/TODO.md.
//
// The client invents nothing itself: it asks to connect, gets its id and the
// level's name, acknowledges — and from then on only receives the world's state.
// In a single-player game the other side of the loop is a local server, on a
// network a remote one; to the client there is no difference.
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "obf2/net/connection.h"
#include "obf2/net/session.h"

namespace obf2::server {

enum class ClientState {
  Disconnected,
  Requesting,   // a ConnectionRequest was sent, waiting for the reply
  Accepted,     // got the id and the level, sent the acknowledgement
  InWorld,      // receiving objects
  Denied,
};

std::string_view clientStateName(ClientState state);

struct RemoteObject {
  std::uint32_t id = 0;
  std::string templateName;
  Vec3f position;
  Vec3f rotation;

  // The previous state — for smoothing. The server sends 30 times a second
  // while we draw more often, so the position is interpolated between packets.
  Vec3f previousPosition;
  bool moved = false;
};

class GameClient {
 public:
  GameClient(std::unique_ptr<net::Connection> connection, std::string playerName)
      : connection_(std::move(connection)), playerName_(std::move(playerName)) {}

  // Sends a ConnectionRequest.
  bool connect();
  void tick(float deltaSeconds);
  void disconnect();

  // Input from the keyboard and mouse. The client adds the sequence number itself.
  void setInput(const net::PlayerInput& input) { input_ = input; }
  const net::PlayerInput& input() const { return input_; }

  // An object's position, smoothed between the last two states from the server.
  Vec3f interpolatedPosition(std::uint32_t objectId) const;

  std::uint32_t inputSequence() const { return inputSequence_; }
  long long inputsSent() const { return inputsSent_; }

  ClientState state() const { return state_; }
  std::uint32_t playerId() const { return playerId_; }
  const std::string& levelName() const { return levelName_; }
  const std::string& gameMode() const { return gameMode_; }
  std::optional<net::DenyReason> denyReason() const { return denyReason_; }

  const std::unordered_map<std::uint32_t, RemoteObject>& objects() const { return objects_; }
  const std::vector<std::string>& log() const { return log_; }

 private:
  void handlePacket(const net::Packet& packet);
  void sendInput();

  std::unique_ptr<net::Connection> connection_;
  std::string playerName_;
  ClientState state_ = ClientState::Disconnected;
  std::uint32_t playerId_ = 0;
  std::string levelName_;
  std::string gameMode_;
  std::optional<net::DenyReason> denyReason_;
  std::unordered_map<std::uint32_t, RemoteObject> objects_;
  std::vector<std::string> log_;

  net::PlayerInput input_;
  std::uint32_t inputSequence_ = 0;
  long long inputsSent_ = 0;
  float sendAccumulator_ = 0.0f;
  float interpolation_ = 0.0f;  // 0..1 between the previous and the current state
};

}  // namespace obf2::server
