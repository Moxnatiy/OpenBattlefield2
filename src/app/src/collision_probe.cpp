#include "obf2/app/collision_probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace obf2::app {

void probeCollisionNear(const Vec3f& at, const level::Level& level,
                        const game::Registry& registry, const server::CollisionWorld& world) {
  for (const auto& object : level.objects) {
    const float dx = object.position.x - at.x;
    const float dz = object.position.z - at.z;
    if (dx * dx + dz * dz > 15.0f * 15.0f) continue;
    const auto* root = registry.find(object.templateName);
    std::printf("  near: %s at %.2f %.2f %.2f rot %.1f %.1f %.1f, collisionMesh '%s'\n",
                object.templateName.c_str(), object.position.x, object.position.y,
                object.position.z, object.rotation.x, object.rotation.y, object.rotation.z,
                root ? std::string(root->text("collisionMesh")).c_str() : "(no template)");
    if (root == nullptr) continue;
    for (const auto& child : root->children) {
      const auto* childTemplate = registry.find(child.name);
      std::printf("    child %s at %.2f %.2f %.2f, collisionMesh '%s'\n", child.name.c_str(),
                  child.position.x, child.position.y, child.position.z,
                  childTemplate ? std::string(childTemplate->text("collisionMesh")).c_str()
                                : "(no template)");
    }
  }
  std::vector<server::MeshContact> contacts;
  world.sphereContacts(at, Vec3f{}, 1.0f, contacts);
  std::printf("  near: %zu contacts of a 1 m sphere at the point\n", contacts.size());
}

}  // namespace obf2::app
