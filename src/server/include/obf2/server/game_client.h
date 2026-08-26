#pragma once
// Ігровий клієнт — інша половина тієї самої пари.
//
// Клієнт шле ввід із тією ж частотою, з якою сервер крутить симуляцію,
// і згладжує чужі позиції між пакетами. Передбачення власного руху
// (client-side prediction) поки немає — див. docs/TODO.md.
//
// Клієнт нічого не вигадує сам: він просить під'єднання, дістає свій id і
// назву рівня, підтверджує — і далі лише отримує стан світу. В одиночній грі
// по той бік петлі стоїть локальний сервер, у мережі — віддалений; для
// клієнта різниці немає.
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
  Requesting,   // надіслали ConnectionRequest, чекаємо відповіді
  Accepted,     // дістали id і рівень, надіслали підтвердження
  InWorld,      // отримуємо об'єкти
  Denied,
};

std::string_view clientStateName(ClientState state);

struct RemoteObject {
  std::uint32_t id = 0;
  std::string templateName;
  Vec3f position;
  Vec3f rotation;

  // Попередній стан — для згладжування. Сервер шле 30 разів на секунду,
  // а малюємо ми частіше, тому між пакетами позицію інтерполюємо.
  Vec3f previousPosition;
  bool moved = false;
};

class GameClient {
 public:
  GameClient(std::unique_ptr<net::Connection> connection, std::string playerName)
      : connection_(std::move(connection)), playerName_(std::move(playerName)) {}

  // Надсилає ConnectionRequest.
  bool connect();
  void tick(float deltaSeconds);
  void disconnect();

  // Ввід із клавіатури й миші. Клієнт сам додає порядковий номер.
  void setInput(const net::PlayerInput& input) { input_ = input; }
  const net::PlayerInput& input() const { return input_; }

  // Позиція об'єкта, згладжена між двома останніми станами від сервера.
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
  float interpolation_ = 0.0f;  // 0..1 між попереднім і поточним станом
};

}  // namespace obf2::server
