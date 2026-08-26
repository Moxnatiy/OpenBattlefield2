#include "obf2/server/game_server.h"

#include <algorithm>
#include <cmath>

namespace obf2::server {
namespace {

// Розмір буфера під один пакет. Оновлення світу ріжуться на порції, щоб
// сюди вміщатися.
constexpr std::size_t kPacketBytes = 1200;

// Скільки об'єктів іде в одному пакеті появи.
//
// Одна поява коштує ~77 байтів: 64 на ім'я шаблону, решта на id, стиснену
// позицію й кути. У 1200 байтів (типовий безпечний розмір UDP-пакета)
// вміщається близько п'ятнадцяти, тож беремо дванадцять із запасом.
constexpr std::size_t kSpawnsPerPacket = 12;

}  // namespace

void GameServer::loadWorld(const level::Level& level, std::size_t limit) {
  objects_.clear();
  for (const level::StaticObject& object : level.objects) {
    if (limit != 0 && objects_.size() >= limit) break;
    WorldObject world;
    world.id = nextObjectId_++;
    world.templateName = object.templateName;
    world.position = object.position;
    world.rotation = object.rotation;
    objects_.push_back(std::move(world));
  }
  log_.push_back("світ завантажено: " + std::to_string(objects_.size()) + " об'єктів з рівня " +
                 level.name);
}

void GameServer::accept(std::unique_ptr<net::Connection> connection) {
  Player player;
  player.id = nextPlayerId_++;
  player.connection = std::move(connection);
  players_.push_back(std::move(player));
}

bool GameServer::sendTo(Player& player, std::span<const std::byte> data) {
  if (player.connection == nullptr || !player.connection->send(data)) return false;
  ++packetsSent_;
  return true;
}

void GameServer::handlePacket(Player& player, const net::Packet& packet) {
  net::BitReader reader(packet);
  const auto header = reader.readBasicHeader();
  if (!header) return;

  const auto type = static_cast<net::PacketType>(header->type);
  std::vector<std::byte> buffer(kPacketBytes);

  switch (type) {
    case net::PacketType::ConnectionRequest: {
      const auto request = net::readConnectionRequest(reader);
      if (!request) return;

      net::BitWriter writer(buffer);
      // Перевірки — до будь-якої зміни стану: спершу вирішуємо, чи пускаємо.
      if (request->protocolVersion != net::kProtocolVersion) {
        net::writeConnectionDenied(writer, net::DenyReason::WrongVersion);
        log_.push_back("відмова: версія " + std::to_string(request->protocolVersion));
      } else if (static_cast<int>(players_.size()) > settings_.maxPlayers) {
        net::writeConnectionDenied(writer, net::DenyReason::ServerFull);
        log_.push_back("відмова: сервер заповнений");
      } else {
        player.name = request->playerName;
        net::ConnectionAccept accept;
        accept.playerId = player.id;
        accept.levelName = settings_.levelName;
        accept.gameMode = settings_.gameMode;
        net::writeConnectionAccept(writer, accept);
        log_.push_back("під'єднався \"" + player.name + "\" (id " + std::to_string(player.id) + ")");
      }
      sendTo(player, std::span(buffer).first(writer.byteSize()));
      return;
    }

    case net::PacketType::ConnectionAcknowledge:
      player.acknowledged = true;
      log_.push_back("підтвердження від \"" + player.name + "\"");
      return;

    case net::PacketType::Data: {
      // Від клієнта тип Data означає ввід (підтип 1 у заголовку).
      if (header->subtype != 1) return;
      const auto input = net::readPlayerInput(reader);
      if (!input) return;

      // Порядковий номер 16-бітний і перевертається; враховуємо це, інакше
      // після 65535 такту гравець застряг би назавжди.
      const std::uint32_t previous = player.lastSequence;
      const std::uint32_t delta = (input->sequence - previous) & 0xFFFFu;
      if (previous != 0 && (delta == 0 || delta > 0x8000u)) return;  // старий пакет

      player.input = *input;
      player.lastSequence = input->sequence;
      return;
    }

    case net::PacketType::PingRequest: {
      net::BitWriter writer(buffer);
      writer.writeBasicHeader(
          net::BasicHeader{static_cast<std::uint32_t>(net::PacketType::PingResponse), 0});
      sendTo(player, std::span(buffer).first(writer.byteSize()));
      return;
    }

    case net::PacketType::Disconnect:
      if (player.connection != nullptr) player.connection->close();
      log_.push_back("від'єднався \"" + player.name + "\"");
      return;

    default:
      return;
  }
}

void GameServer::sendWorld(Player& player) {
  // Опорна точка стиснення — початок координат: рівень центрований на ньому,
  // тому різниці лишаються в межах карти.
  const Vec3f reference{0.0f, 0.0f, 0.0f};

  for (std::size_t start = 0; start < objects_.size(); start += kSpawnsPerPacket) {
    const std::size_t end = std::min(start + kSpawnsPerPacket, objects_.size());

    std::vector<net::ObjectUpdate> updates;
    updates.reserve(end - start);
    for (std::size_t i = start; i < end; ++i) {
      net::ObjectUpdate update;
      update.objectId = objects_[i].id;
      update.templateName = objects_[i].templateName;
      update.position = objects_[i].position;
      update.rotation = objects_[i].rotation;
      update.spawn = true;
      updates.push_back(std::move(update));
    }

    std::vector<std::byte> buffer(kPacketBytes);
    net::BitWriter writer(buffer);
    if (!net::writeObjectUpdates(writer, updates, reference)) break;
    if (!sendTo(player, std::span(buffer).first(writer.byteSize()))) break;
  }

  player.worldSent = true;
  log_.push_back("світ надіслано гравцеві \"" + player.name + "\"");
}

WorldObject* GameServer::findObject(std::uint32_t id) {
  for (WorldObject& object : objects_) {
    if (object.id == id) return &object;
  }
  return nullptr;
}

std::uint32_t GameServer::spawnSoldier(Player& player) {
  WorldObject soldier;
  soldier.id = nextObjectId_++;
  soldier.templateName = settings_.soldierTemplate;
  soldier.position = settings_.spawnPosition;
  soldier.dynamic = true;
  soldier.ownerPlayerId = player.id;
  objects_.push_back(std::move(soldier));

  log_.push_back("з'явився солдат гравця \"" + player.name + "\" (об'єкт " +
                 std::to_string(objects_.back().id) + ")");
  return objects_.back().id;
}

float GameServer::groundHeightAt(const Vec3f& position) const {
  if (terrain_ == nullptr || terrain_->heights.empty()) return 0.0f;

  // Білінійна вибірка з карти висот: без неї солдат стрибав би сходинками
  // по вузлах сітки.
  const float half = terrain_->halfExtent();
  const float gx = position.x / terrain_->primary.scale.x + half;
  const float gz = position.z / terrain_->primary.scale.z + half;

  const int x0 = static_cast<int>(std::floor(gx));
  const int z0 = static_cast<int>(std::floor(gz));
  const float tx = gx - static_cast<float>(x0);
  const float tz = gz - static_cast<float>(z0);

  const float h00 = terrain_->heightAt(x0, z0);
  const float h10 = terrain_->heightAt(x0 + 1, z0);
  const float h01 = terrain_->heightAt(x0, z0 + 1);
  const float h11 = terrain_->heightAt(x0 + 1, z0 + 1);

  const float top = h00 + (h10 - h00) * tx;
  const float bottom = h01 + (h11 - h01) * tx;
  return top + (bottom - top) * tz;
}

void GameServer::simulate(float step) {
  ++tickCount_;

  for (Player& player : players_) {
    if (!player.acknowledged || player.soldierId == 0) continue;
    WorldObject* soldier = findObject(player.soldierId);
    if (soldier == nullptr) continue;

    // Рух у площині: осі вводу повертаються на кут огляду, тому "вперед"
    // означає туди, куди гравець дивиться.
    constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
    const float yaw = player.input.yaw * kToRadians;
    const float sin = std::sin(yaw);
    const float cos = std::cos(yaw);

    const float speed = player.input.sprint ? settings_.sprintSpeed : settings_.walkSpeed;
    Vec3f wish{player.input.moveRight * cos - player.input.moveForward * sin, 0.0f,
               -player.input.moveRight * sin - player.input.moveForward * cos};

    // Нормуємо, щоб рух по діагоналі не був швидшим за рух прямо.
    const float magnitude = length(wish);
    if (magnitude > 1.0f) wish = wish * (1.0f / magnitude);

    BodyState body;
    body.position = soldier->position;
    body.velocity = soldier->velocity;
    body.onGround = soldier->onGround;

    stepSoldier(body, wish, speed, player.input.jump, settings_.physics,
                groundHeightAt(soldier->position), step);

    // Зіткнення з геометрією рівня: сервер вирішує, куди гравець дійшов
    // насправді. Швидкість гасимо в напрямку виштовхування, інакше гравець
    // «тремтів» би, впираючись у стіну.
    if (collision_ != nullptr) {
      const Vec3f before = body.position;
      // Перевіряємо на висоті грудей, а не біля ніг: інакше сфера чіплялася б
      // за землю й гравець не міг би рухатися взагалі.
      Vec3f probe = body.position;
      probe.y += settings_.soldierRadius + 0.5f;
      if (collision_->resolveSphere(probe, settings_.soldierRadius) > 0) {
        body.position.x = probe.x;
        body.position.z = probe.z;
        const Vec3f pushed = body.position - before;
        if (length(pushed) > 1e-4f) {
          const Vec3f direction = normalize(pushed);
          const float into = dot(body.velocity, direction);
          if (into < 0.0f) body.velocity = body.velocity - direction * into;
        }
      }
    }

    soldier->position = body.position;
    soldier->velocity = body.velocity;
    soldier->onGround = body.onGround;
    soldier->rotation = Vec3f{player.input.yaw, player.input.pitch, 0.0f};
  }
}

void GameServer::broadcastDynamic() {
  std::vector<net::ObjectUpdate> updates;
  for (const WorldObject& object : objects_) {
    if (!object.dynamic) continue;
    net::ObjectUpdate update;
    update.objectId = object.id;
    update.position = object.position;
    update.rotation = object.rotation;
    update.spawn = false;
    updates.push_back(std::move(update));
  }
  if (updates.empty()) return;

  std::vector<std::byte> buffer(kPacketBytes);
  net::BitWriter writer(buffer);
  if (!net::writeObjectUpdates(writer, updates, Vec3f{0.0f, 0.0f, 0.0f})) return;

  for (Player& player : players_) {
    if (!player.worldSent) continue;
    sendTo(player, std::span(buffer).first(writer.byteSize()));
  }
}

void GameServer::tick(float deltaSeconds) {
  for (Player& player : players_) {
    if (player.connection == nullptr) continue;

    while (auto packet = player.connection->receive()) {
      ++packetsReceived_;
      handlePacket(player, *packet);
    }

    // Стан світу йде лише після підтвердження — інакше клієнт отримав би
    // об'єкти ще до того, як дізнався свій id і рівень.
    if (player.acknowledged && !player.worldSent) {
      if (player.soldierId == 0) player.soldierId = spawnSoldier(player);
      sendWorld(player);
    }
  }

  // Фіксований крок: скільки б не тривав кадр, симуляція йде рівними
  // тактами. Інакше на повільній машині гравець рухався б інакше.
  const float step = tickInterval();
  accumulator_ += deltaSeconds;

  // Обмеження на випадок довгої паузи: краще відстати, ніж намотати
  // сотні тактів за один кадр.
  constexpr int kMaxStepsPerFrame = 8;
  int steps = 0;
  while (accumulator_ >= step && steps < kMaxStepsPerFrame) {
    simulate(step);
    accumulator_ -= step;
    ++steps;
  }
  if (steps > 0) broadcastDynamic();

  // Прибираємо тих, хто відвалився.
  players_.erase(std::remove_if(players_.begin(), players_.end(),
                                [](const Player& player) {
                                  return player.connection == nullptr ||
                                         !player.connection->connected();
                                }),
                 players_.end());
}

}  // namespace obf2::server
