#pragma once
// UDP-канал до справжнього сервера.
//
// Досі мережа працювала лише петлею в пам'яті. Тут — звичайний сокет,
// щоб можна було говорити з оригінальним сервером BF2.
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace obf2::net {

class UdpSocket {
 public:
  ~UdpSocket();

  // Відкриває сокет і запам'ятовує, куди слати. Хост — адреса або ім'я.
  static std::unique_ptr<UdpSocket> connect(const std::string& host, std::uint16_t port,
                                            std::string* error = nullptr);

  bool send(std::span<const std::byte> data);

  // Чекає пакет не довше за timeoutMs. nullopt — нічого не прийшло.
  std::optional<std::vector<std::byte>> receive(int timeoutMs);

  const std::string& describe() const { return description_; }

 private:
  int handle_ = -1;
  std::string description_;
};

}  // namespace obf2::net
