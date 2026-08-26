#pragma once
// Світ зіткнень сервера.
//
// Трикутники всіх статичних об'єктів переводяться у світові координати один
// раз при завантаженні й розкладаються по рівномірній сітці. Перевіряти
// 2.5 млн трикутників щокадру неможливо, а комірка розміром із кілька
// метрів лишає на перевірку одиниці.
//
// Зіткнення саме серверні: у BF2 світом володіє сервер, і саме він вирішує,
// куди гравець дійшов насправді.
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/mesh/collision.h"

namespace obf2::server {

struct CollisionTriangle {
  Vec3f a, b, c;
  Vec3f normal;
};

class CollisionWorld {
 public:
  // Розмір комірки сітки. 8 метрів — компроміс: дрібніша сітка це більше
  // пам'яті під індекс, більша — більше зайвих перевірок.
  explicit CollisionWorld(float cellSize = 8.0f) : cellSize_(cellSize) {}

  // Додає шар зіткнень, перетворений у світові координати.
  void addLayer(const mesh::CollisionLayer& layer, const Mat4& transform);

  // Виштовхує сферу з геометрії. Повертає, скільки разів довелося
  // виштовхувати — нуль означає, що шлях вільний.
  int resolveSphere(Vec3f& position, float radius) const;

  std::size_t triangleCount() const { return triangles_.size(); }
  std::size_t cellCount() const { return cells_.size(); }

 private:
  // Ключ комірки: три цілі координати, згорнуті в одне число.
  static std::uint64_t cellKey(int x, int y, int z);
  void forEachNearby(const Vec3f& position, float radius,
                     const std::function<void(const CollisionTriangle&)>& visit) const;

  float cellSize_;
  std::vector<CollisionTriangle> triangles_;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;
};

}  // namespace obf2::server
