#include "obf2/app/model_view.h"

#include <algorithm>
#include <cstdio>

#include "obf2/game/object_mesh.h"
#include "obf2/mesh/skinning.h"

namespace obf2::app {

bool addModelToScene(FileSystem& files, const game::Registry& registry,
                     const ModelRequest& request, Scene& scene) {
  std::optional<mesh::RenderMesh> single;
  if (!request.objectName.empty()) {
    single = game::buildObjectMesh(files, registry, request.objectName, request.geometryIndex,
                                   request.lodIndex, true);
    if (!single) {
      std::fprintf(stderr, "could not assemble the object %s\n", request.objectName.c_str());
      return false;
    }

    // Skeletal animation: we put the mesh into a pose from the clips.
    if (!request.animationPaths.empty() && !single->skin.empty()) {
      const std::string skeletonPath =
          request.skeletonPath.empty()
              ? std::string("objects/soldiers/Common/Animations/3p_setup.ske")
              : request.skeletonPath;

      const auto skeletonBytes = files.read(skeletonPath);
      std::string error;
      const auto skeleton =
          skeletonBytes ? mesh::loadSkeleton(*skeletonBytes, &error) : std::nullopt;
      if (!skeleton) {
        std::fprintf(stderr, "skeleton: %s\n", error.c_str());
      } else {
        std::vector<mesh::BoneAnimation> clips;
        for (const std::string& path : request.animationPaths) {
          const auto bytes = files.read(path);
          if (!bytes) {
            std::fprintf(stderr, "no animation %s\n", path.c_str());
            continue;
          }
          auto clip = mesh::loadBoneAnimation(*bytes, &error);
          if (!clip) {
            std::fprintf(stderr, "animation %s: %s\n", path.c_str(), error.c_str());
            continue;
          }
          clips.push_back(std::move(*clip));
        }

        std::vector<mesh::PoseStage> stages;
        const auto wanted = static_cast<std::uint32_t>(request.frame < 0 ? 0 : request.frame);
        for (const auto& clip : clips) {
          // The frame is taken cyclically: the clips are of different lengths (the
          // legs 16 frames, the weapon 36), while we show one moment.
          const std::uint32_t frame = clip.frameCount == 0 ? 0 : wanted % clip.frameCount;
          stages.push_back(mesh::PoseStage{&clip, static_cast<float>(frame), 1.0f});
          std::printf("  clip: %zu tracks, %u frames -> frame %u\n", clip.boneIds.size(),
                      clip.frameCount, frame);
        }

        if (!stages.empty()) {
          const auto pose = mesh::poseSkeleton(*skeleton, stages);
          mesh::RenderMesh posed = *single;
          mesh::skinMesh(*single, pose, posed);
          single = std::move(posed);
        }
      }
    }
  } else {
    single = game::loadMesh(files, request.meshPath, request.geometryIndex, request.lodIndex, true);
    if (!single) return false;
  }

  const Vec3f boundsMin{single->bounds.min.x, single->bounds.min.y, single->bounds.min.z};
  const Vec3f boundsMax{single->bounds.max.x, single->bounds.max.y, single->bounds.max.z};
  scene.center = (boundsMin + boundsMax) * 0.5f;
  scene.radius = std::max(0.001f, length(boundsMax - boundsMin) * 0.5f);
  scene.add(std::move(*single), Mat4::identity());
  return true;
}

}  // namespace obf2::app
