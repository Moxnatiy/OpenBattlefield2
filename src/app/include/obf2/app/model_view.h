#pragma once
// One mesh or one object on screen, posed by its clips — the model viewer.
//
// It is not a mode of the game: the engine has no such screen. It is how a mesh,
// a skeleton and an animation are looked at while they are being read, and the
// scene it builds is the same one a level fills, so the camera and the renderer
// do not know the difference.
#include <optional>
#include <string>
#include <vector>

#include "obf2/app/scene.h"
#include "obf2/game/object_template.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

struct ModelRequest {
  // Exactly one of the two: a path in the VFS, or a template's name.
  std::string meshPath;
  std::string objectName;
  int geometryIndex = -1;
  int lodIndex = 0;
  // `--anim`: several clips are allowed, and the engine blends them per bone —
  // the weapon's move the upper body, the movement's the legs, and each touches
  // only its own bones.
  std::vector<std::string> animationPaths;
  // `--skeleton`; the soldier's third-person setup by default.
  std::string skeletonPath;
  int frame = 0;  // which frame of the clips to show
};

// Puts the mesh into the scene and sets the scene's centre and radius from its
// bounds. False when there was nothing to show; the reason is on stderr.
bool addModelToScene(FileSystem& files, const game::Registry& registry,
                     const ModelRequest& request, Scene& scene);

}  // namespace obf2::app
