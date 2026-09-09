#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/mesh/skeleton.h"

using namespace obf2;

namespace {

// A `.ske` builder — the test does not depend on the game being present and also
// fixes the format's layout in code.
class SkeletonBuilder {
 public:
  explicit SkeletonBuilder(std::uint32_t boneCount) {
    u32(2);  // version
    u32(boneCount);
  }

  void addBone(const std::string& name, std::int16_t parent, float x, float y, float z) {
    // The length counts the terminating zero.
    u16(static_cast<std::uint16_t>(name.size() + 1));
    for (const char c : name) byte(static_cast<std::uint8_t>(c));
    byte(0);
    u16(static_cast<std::uint16_t>(parent));
    real(0.0f); real(0.0f); real(0.0f); real(1.0f);  // the identity quaternion
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
  CHECK_EQ(skeleton->find("nothing"), -1);
  // The translation is local, relative to the parent.
  CHECK(std::abs(skeleton->bones[2].position.y + 0.4f) < 0.001f);
}

static void testSizeMatchesLayout() {
  // The layout was verified against the game's data exactly like this: the computed
  // size has to match the file byte for byte.
  SkeletonBuilder builder(2);
  builder.addBone("root", -1, 0.0f, 0.0f, 0.0f);
  builder.addBone("joint1", 0, 0.0f, 0.0f, 0.0f);
  // 8 header + (2 + 5 + 2 + 28) + (2 + 7 + 2 + 28)
  CHECK_EQ(builder.bytes().size(), std::size_t(8 + 37 + 39));
}

static void testBadParentIsRejected() {
  // The parent has to be an already read bone — otherwise the file is corrupt.
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
    if (skeleton.has_value()) continue;  // a very short truncation may be an "empty" skeleton
    CHECK(!error.empty());
  }
}

TEST_MAIN({
  testHierarchyIsRead();
  testSizeMatchesLayout();
  testBadParentIsRejected();
  testTruncatedFileDoesNotCrash();
})
