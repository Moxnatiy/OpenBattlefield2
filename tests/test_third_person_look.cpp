// A third-person soldier's spine and head follow his look
// (`Soldier::updateThirdPersonAnimations`, Linux 0x555020). docs/functions/soldier-look-3p.md.
#include <cmath>

#include "check.h"
#include "obf2/app/third_person_look.h"
#include "obf2/mesh/skinning.h"

namespace {

using namespace obf2;
using namespace obf2::app;

bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }

// The forward axis (+z) of a post's 3x3: column 2 in our layout.
float forwardX(const mesh::Mat4& m) { return m.m[8]; }
float forwardY(const mesh::Mat4& m) { return m.m[9]; }
float downDegrees(const mesh::Mat4& m) { return std::asin(-forwardY(m)) * 180.0f / 3.14159265f; }

// Standing, 40 down and 30 right: a quarter and a third to each spine bone.
void testSpread() {
  const auto posts = soldierLookPosts(40.0f, 30.0f, 0);
  CHECK(posts.size() == 5);
  CHECK(posts[0].bone == kLookSpine2 && posts[1].bone == kLookSpine3 && posts[2].bone == kLookTorso);
  CHECK(posts[3].bone == kLookHead && posts[4].bone == kLookMesh15);
  CHECK(forwardY(posts[0].rotation) < 0.0f);  // down
  CHECK(forwardX(posts[0].rotation) > 0.0f);  // right
  CHECK(near(downDegrees(posts[3].rotation), 10.0f));
  CHECK(near(forwardX(posts[3].rotation), 0.0f));  // the head's yaw is the idle look
  CHECK(near(downDegrees(posts[4].rotation), -10.0f));  // the gear turns back
}

// Standing to +-70, prone to -30..5, swimming to 0.
void testClamps() {
  CHECK(near(downDegrees(soldierLookPosts(90.0f, 0.0f, 0)[3].rotation), 17.5f));
  CHECK(near(downDegrees(soldierLookPosts(-90.0f, 0.0f, 1)[3].rotation), -17.5f));
  CHECK(near(downDegrees(soldierLookPosts(40.0f, 0.0f, 2)[3].rotation), 1.25f));
  CHECK(near(downDegrees(soldierLookPosts(-40.0f, 0.0f, 2)[3].rotation), -7.5f));
  CHECK(near(downDegrees(soldierLookPosts(40.0f, 0.0f, 3)[3].rotation), 0.0f));
}

// A post turns its bone and the children on it; the bone's position stays.
void testPose() {
  mesh::Skeleton skeleton;
  skeleton.bones.resize(2);
  skeleton.bones[0].position = {0.0f, 1.0f, 0.0f};
  skeleton.bones[1].parent = 0;
  skeleton.bones[1].position = {0.0f, 0.0f, 1.0f};
  const auto posts = soldierLookPosts(40.0f, 0.0f, 0);
  const auto world = mesh::poseSkeleton(skeleton, {}, {mesh::BonePost{0, posts[3].rotation}});
  CHECK(near(world[0].m[13], 1.0f));
  CHECK(world[1].m[13] < 1.0f);  // one ahead of it, the child dips
  const auto still = mesh::poseSkeleton(skeleton, {});
  CHECK(near(still[1].m[13], 1.0f));
}

}  // namespace

TEST_MAIN({
  testSpread();
  testClamps();
  testPose();
})
