#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/mesh/skeleton.h"

using namespace obf2;

namespace {

// Збирач `.ske` — тест не залежить від наявності гри й заодно фіксує
// розкладку формату в коді.
class SkeletonBuilder {
 public:
  explicit SkeletonBuilder(std::uint32_t boneCount) {
    u32(2);  // версія
    u32(boneCount);
  }

  void addBone(const std::string& name, std::int16_t parent, float x, float y, float z) {
    // Довжина рахує завершальний нуль.
    u16(static_cast<std::uint16_t>(name.size() + 1));
    for (const char c : name) byte(static_cast<std::uint8_t>(c));
    byte(0);
    u16(static_cast<std::uint16_t>(parent));
    real(0.0f); real(0.0f); real(0.0f); real(1.0f);  // одиничний кватерніон
    real(x); real(y); real(z);
  }

  const std::vector<std::byte>& bytes() const { return bytes_; }

 private:
  void byte(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
  void u16(std::uint16_t value) { append(&value, sizeof(value)); }
  void u32(std::uint32_t value) { append(&value, sizeof(value)); }
  void real(float value) { append(&value, sizeof(value)); }
  void append(const void* data, std::size_t size) {
    const auto* start = static_cast<const std::byte*>(data);
    bytes_.insert(bytes_.end(), start, start + size);
  }

  std::vector<std::byte> bytes_;
};

}  // namespace

static void testHierarchyIsRead() {
  SkeletonBuilder builder(3);
  builder.addBone("root", -1, 0.0f, 1.0f, 0.0f);
  builder.addBone("hip", 0, 0.0f, -0.1f, 0.0f);
  builder.addBone("knee", 1, 0.0f, -0.4f, 0.0f);

  std::string error;
  const auto skeleton = mesh::loadSkeleton(builder.bytes(), &error);
  CHECK(skeleton.has_value());
  if (!skeleton) return;

  CHECK_EQ(skeleton->version, 2u);
  CHECK_EQ(skeleton->bones.size(), std::size_t(3));
  CHECK_EQ(skeleton->bones[0].name, std::string("root"));
  CHECK_EQ(skeleton->bones[0].parent, -1);
  CHECK_EQ(skeleton->bones[2].parent, 1);
  CHECK_EQ(skeleton->find("knee"), 2);
  CHECK_EQ(skeleton->find("немає"), -1);
  // Зсув локальний, відносно батька.
  CHECK(std::abs(skeleton->bones[2].position.y + 0.4f) < 0.001f);
}

static void testSizeMatchesLayout() {
  // Розкладка перевірена на даних гри саме так: порахований розмір має
  // збігатися з файлом байт у байт.
  SkeletonBuilder builder(2);
  builder.addBone("root", -1, 0.0f, 0.0f, 0.0f);
  builder.addBone("joint1", 0, 0.0f, 0.0f, 0.0f);
  // 8 заголовок + (2 + 5 + 2 + 28) + (2 + 7 + 2 + 28)
  CHECK_EQ(builder.bytes().size(), std::size_t(8 + 37 + 39));
}

static void testBadParentIsRejected() {
  // Батько має бути вже прочитаною кісткою — інакше файл пошкоджений.
  SkeletonBuilder builder(2);
  builder.addBone("root", -1, 0.0f, 0.0f, 0.0f);
  builder.addBone("broken", 5, 0.0f, 0.0f, 0.0f);

  std::string error;
  CHECK(!mesh::loadSkeleton(builder.bytes(), &error).has_value());
  CHECK(!error.empty());
}

static void testTruncatedFileDoesNotCrash() {
  SkeletonBuilder builder(3);
  builder.addBone("root", -1, 0.0f, 1.0f, 0.0f);
  builder.addBone("hip", 0, 0.0f, -0.1f, 0.0f);
  builder.addBone("knee", 1, 0.0f, -0.4f, 0.0f);

  const std::vector<std::byte>& full = builder.bytes();
  for (std::size_t size = 0; size < full.size(); ++size) {
    std::vector<std::byte> truncated(full.begin(), full.begin() + static_cast<long>(size));
    std::string error;
    const auto skeleton = mesh::loadSkeleton(truncated, &error);
    if (skeleton.has_value()) continue;  // дуже короткий обрізок може бути «порожнім» скелетом
    CHECK(!error.empty());
  }
}

TEST_MAIN({
  testHierarchyIsRead();
  testSizeMatchesLayout();
  testBadParentIsRejected();
  testTruncatedFileDoesNotCrash();
})
