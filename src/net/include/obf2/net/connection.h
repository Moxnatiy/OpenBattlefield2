#pragma once
// Канал між клієнтом і сервером.
//
// У Battlefield 2 **одиночна гра — це теж клієнт і сервер**: рушій піднімає
// локальний сервер і під'єднується до нього. Тому канал абстрактний: для
// одиночної гри це петля в пам'яті, для мережі — UDP. Логіка гри однакова,
// і це головна причина, чому серверну частину не можна відкладати.
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace obf2::net {

using Packet = std::vector<std::byte>;

class Connection {
 public:
  virtual ~Connection() = default;

  virtual bool send(std::span<const std::byte> data) = 0;
  // nullopt — нічого не прийшло; це не помилка.
  virtual std::optional<Packet> receive() = 0;
  virtual bool connected() const = 0;
  virtual void close() = 0;

  virtual std::string_view describe() const = 0;
};

// Петля в пам'яті: два кінці, з'єднані чергами. Пакети не серіалізуються
// двічі й не проходять через сокет, але формат той самий, що й у мережі —
// інакше одиночна гра перевіряла б не той код, який працює в мультиплеєрі.
class LoopbackConnection : public Connection {
 public:
  bool send(std::span<const std::byte> data) override;
  std::optional<Packet> receive() override;
  bool connected() const override { return peer_ != nullptr; }
  void close() override;
  std::string_view describe() const override { return "loopback"; }

  std::size_t pending() const { return incoming_.size(); }
  long long sentPackets() const { return sent_; }
  long long receivedPackets() const { return received_; }

  // Створює пару зв'язаних кінців: [клієнт, сервер].
  static std::pair<std::unique_ptr<LoopbackConnection>, std::unique_ptr<LoopbackConnection>>
  createPair();

 private:
  LoopbackConnection* peer_ = nullptr;
  std::deque<Packet> incoming_;
  long long sent_ = 0;
  long long received_ = 0;
};

}  // namespace obf2::net
