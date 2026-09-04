#pragma once
// Стан світу, зібраний із пакетів сервера.
//
// Клієнт дізнається про світ трьома різними шляхами, і всі троє сходяться
// тут:
//
//   * **події** — хто грає (`CreatePlayerEvent`), які об'єкти створено
//     (`CreateObjectEvent`), хто чим керує (`EnterVehicleEvent`);
//   * **стан керованого об'єкта** — опорна точка стиснення на пакет;
//   * **записи потоку привидів** — де зараз рухомі об'єкти.
//
// Тримати це в циклі кадрів немає сенсу: тут немає ні вікна, ні часу, ні
// вводу — самі лише пакети. Тому воно живе окремо й перевіряється тестом
// на знятому трафіку (`tests/data/bf2-spawned.bin`), а не «на око в грі».
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>

#include "obf2/core/math.h"
#include "obf2/net/bf2_events.h"

namespace obf2::net::bf2 {

// Об'єкт, який сервер створив уже в грі: чужий солдат або техніка.
struct RemoteObject {
  Vec3f position;
  std::optional<float> yaw;  // градуси, якщо приходило рискання
  // Команда власника. Нуль — це не солдат гравця (техніка, майно рівня).
  int team = 0;
  bool fromGhostStream = false;  // місце вже уточнене потоком, не лише подією

  // Слід — це мірило розбору, а не прикраса. Солдат стоїть на землі, тож
  // стала різниця з рельєфом означає зсув початку об'єкта, а різниця, що
  // росте, — помилку розбору. Для нерухомого гравця `travelled` має
  // лишатися близьким до нуля.
  Vec3f firstSeen;
  int updates = 0;
  float travelled = 0.0f;    // найбільший зсув від першого місця
  float aboveGround = 0.0f;  // сума перевищень над рельєфом
};

struct RemotePlayer {
  std::string name;
  int team = 0;
  std::uint16_t object = 0;  // об'єкт, який гравець зайняв
};

class WorldView {
 public:
  // Один пакет даних від сервера. Порядок усередині важливий: спершу
  // події (з них ми дізнаємося, хто солдат), потім стан керованого
  // об'єкта (опорна точка), і аж тоді записи привидів.
  void feed(std::span<const std::byte> packet);

  // Ім'я, за яким упізнаємо себе. Сервер складає його як «тег клану,
  // пробіл, ім'я», тож порівнюємо хвостом.
  void setOwnName(std::string name) { ownName_ = std::move(name); }
  void setPlayerSpawned(bool spawned) { playerSpawned_ = spawned; }

  int ownPlayer() const { return ownPlayer_; }
  int ownTeam() const { return ownTeam_; }
  std::uint16_t ownObject() const { return ownObject_; }

  const std::map<std::uint32_t, RemotePlayer>& players() const { return players_; }
  const std::map<std::uint16_t, RemoteObject>& objects() const { return objects_; }
  const Vec3f& compressionReference() const { return compressionReference_; }

  // Скільки місць прийшло з потоку і скільки ми відкинули як нечитані.
  int positionUpdates() const { return positionUpdates_; }
  int rejected() const { return rejected_; }

  // Перевірка розбору: солдат стоїть на землі, тож стала різниця з
  // рельєфом — це зсув початку об'єкта, а розбіжність, що росте, —
  // помилка розбору. Хто кличе, той і знає рельєф.
  void setGroundProbe(std::function<float(const Vec3f&)> probe) { ground_ = std::move(probe); }

 private:
  bool isSoldier(std::uint16_t id) const;
  Vec3f referenceFor(std::uint16_t id) const;
  bool looksSane(const Vec3f& at, bool soldier) const;

  std::string ownName_;
  int ownPlayer_ = -1;
  int ownTeam_ = 0;
  std::uint16_t ownObject_ = 0;
  bool playerSpawned_ = false;

  std::map<std::uint32_t, RemotePlayer> players_;
  std::map<std::uint16_t, RemoteObject> objects_;
  std::map<std::uint16_t, std::uint32_t> owners_;  // об'єкт -> гравець
  Vec3f compressionReference_;
  std::function<float(const Vec3f&)> ground_;
  int positionUpdates_ = 0;
  int rejected_ = 0;
};

}  // namespace obf2::net::bf2
