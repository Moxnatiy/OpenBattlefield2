#include "obf2/net/bf2_world.h"

#include <algorithm>
#include <cmath>

namespace obf2::net::bf2 {

bool WorldView::isSoldier(std::uint16_t id) const {
  // Солдат — це об'єкт, який зайняв гравець. Каже це сам сервер подією
  // `EnterVehicleEvent` (тип 9), тож здогадів тут немає.
  return owners_.find(id) != owners_.end();
}

Vec3f WorldView::referenceFor(std::uint16_t id) const {
  // Місце солдата пакується відносно **вектора стиснення потоку**:
  // `BF2.exe`, 0x62bd60 читає вектор із поля `потік+0x54`, куди його
  // поклав стан керованого об'єкта.
  if (isSoldier(id)) return compressionReference_;
  const auto found = objects_.find(id);
  if (found != objects_.end()) return found->second.position;
  return compressionReference_;
}

bool WorldView::looksSane(const Vec3f& at, bool soldier) const {
  if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) return false;
  // Карти BF2 не більші за 2048 метрів у поперечнику (`GLSWorldSizeX`,
  // типове 2048 — див. spawnGroupWorldPos), тож усе поза цим — сміття.
  if (std::abs(at.x) > 1024.0f || std::abs(at.z) > 1024.0f) return false;

  // Перевірку по землі робимо **лише для солдата**: він на ній стоїть.
  // Техніка й майно бувають і на дахах, і на кранах — там перевищення в
  // десятки метрів звичайне, і відкидати їх було б помилкою.
  if (!soldier || !ground_) return true;
  const float above = at.y - ground_(at);
  return above > -5.0f && above < 5.0f;
}

void WorldView::feed(std::span<const std::byte> packet) {
  // 1. Події: хто грає, які об'єкти є, хто чим керує.
  for (const Event& event : readEvents(packet)) {
    if (event.player) {
      RemotePlayer& player = players_[event.player->id];
      player.name = event.player->name;
      player.team = static_cast<int>(event.player->team);
      // Себе впізнаємо за хвостом імені: сервер складає його як «тег
      // клану, пробіл, ім'я», і на сервері без рейтингу тег порожній.
      const std::string& name = event.player->name;
      if (ownPlayer_ < 0 && !ownName_.empty() && name.size() >= ownName_.size() &&
          name.compare(name.size() - ownName_.size(), ownName_.size(), ownName_) == 0) {
        ownPlayer_ = static_cast<int>(event.player->id);
        ownTeam_ = player.team;
      }
    }
    if (event.object && event.object->position) {
      RemoteObject& object = objects_[event.object->networkId];
      // Місце з події створення — це початкове; далі його уточнює потік.
      if (!object.fromGhostStream) object.position = *event.object->position;
    }
    if (event.enter) {
      owners_[event.enter->object] = event.enter->player;
      players_[event.enter->player].object = event.enter->object;
      objects_[event.enter->object].team =
          players_.count(event.enter->player) ? players_[event.enter->player].team : 0;
      if (ownPlayer_ >= 0 && event.enter->player == static_cast<std::uint32_t>(ownPlayer_)) {
        ownObject_ = event.enter->object;
      }
    }
    if (event.exitPlayer) {
      const auto found = players_.find(*event.exitPlayer);
      if (found != players_.end()) {
        owners_.erase(found->second.object);
        found->second.object = 0;
      }
      if (ownPlayer_ >= 0 && *event.exitPlayer == static_cast<std::uint32_t>(ownPlayer_)) {
        ownObject_ = 0;
      }
    }
  }

  // 2. Стан керованого об'єкта: опорна точка стиснення на весь пакет
  //    (`BF2.exe` / лінукс-сервер, GhostManager::readControlObjectState,
  //    0x445c30 — три числа перед мережевим номером).
  if (const auto state = readControlObjectState(packet)) {
    compressionReference_ = state->compressionReference;
  }

  // 3. Записи потоку привидів: де все рухається.
  const auto soldier = [this](std::uint16_t id) { return isSoldier(id); };
  const auto reference = [this](std::uint16_t id) { return referenceFor(id); };
  for (const GhostRecord& record : readGhostRecords(packet, reference, soldier)) {
    // Вид 3 — об'єкт зник (GhostManager::readData, 0x445820: гілка кличе
    // disableObject і removeActiveDescriptor).
    if (record.kind == 3) {
      objects_.erase(record.networkId);
      continue;
    }
    if (!record.position) continue;
    if (!looksSane(*record.position, isSoldier(record.networkId))) {
      ++rejected_;
      continue;
    }
    RemoteObject& object = objects_[record.networkId];
    if (object.updates == 0) object.firstSeen = *record.position;
    ++object.updates;
    const Vec3f moved = *record.position - object.firstSeen;
    object.travelled = std::max(
        object.travelled, std::sqrt(moved.x * moved.x + moved.y * moved.y + moved.z * moved.z));
    if (ground_) object.aboveGround += record.position->y - ground_(*record.position);
    object.position = *record.position;
    object.fromGhostStream = true;
    if (record.yaw) object.yaw = *record.yaw;
    const auto owner = owners_.find(record.networkId);
    if (owner != owners_.end() && players_.count(owner->second)) {
      object.team = players_[owner->second].team;
    }
    ++positionUpdates_;
  }
}

}  // namespace obf2::net::bf2
