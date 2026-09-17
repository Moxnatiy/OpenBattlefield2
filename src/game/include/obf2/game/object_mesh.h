#pragma once
// A template's name -> the geometry to draw it with.
//
// In BF2 an object's mesh is not named by a path: `ObjectTemplate.geometry`
// gives a name, and the file lies beside the `.con` in a `meshes` subdirectory.
// A vehicle is one `.bundledmesh` whose parts the tree places by
// `geometryPart` (scene.h), while a control point has no mesh of its own and is
// assembled from its children's. The engine does this in
// `Code/BF2/Scene/Object/ObjectUtils.cpp`.
#include <optional>
#include <string>

#include "obf2/game/object_template.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::game {

// Which geom of a `.bundledmesh` is the outside view.
//
// A vehicle's file holds several: the cockpit view, the outside view and the
// wreckage. There is no explicit marker, but there is a reliable sign — **the
// cockpit view has exactly one lod**, because the player is always close to it,
// while the outside view has three or four.
std::size_t pickGeometry(const mesh::Mesh& mesh, std::size_t lodIndex);

// The file beside the template's `.con`: `<dir>/meshes/<name>.<ext>` with the
// three mesh extensions tried in turn. Empty when nothing is there.
std::string resolveGeometryPath(FileSystem& files, const std::string& templateFile,
                                const std::string& geometryName);

// One mesh file, unpacked into drawable geometry. `geometryOverride` below zero
// means "choose by `pickGeometry`".
std::optional<mesh::RenderMesh> loadMesh(FileSystem& files, const std::string& path,
                                         int geometryOverride, int lodIndex, bool verbose);

// The whole tree of a template as one mesh, with the parts in their places.
// When the root has no geometry of its own the children's meshes are merged —
// that is how a control point is built, its flag arriving through
// `ObjectTemplate.addTemplate flagpole`.
std::optional<mesh::RenderMesh> buildObjectMesh(FileSystem& files, const Registry& registry,
                                                const std::string& templateName,
                                                int geometryOverride, int lodIndex, bool verbose);

// At which step an object is lost on its way to the screen. Walks the same path
// as `buildObjectMesh` but says where exactly it stopped: without that, "the
// object is not visible" explains nothing.
enum class DrawStage {
  Drawn,            // it arrived: the geometry is assembled
  NoTemplate,       // the template is not in the registry
  NoTree,           // the child tree did not assemble
  NoGeometryName,   // neither the root nor the children have geometry
  GeometryInChild,  // there is geometry, but in a child — we take only the root's
  NoGeometryFile,   // there is a name but the file was not found
  NoMesh,           // there is a file but the mesh did not parse
};

std::string_view drawStageName(DrawStage stage);

DrawStage checkDrawable(FileSystem& files, const Registry& registry,
                        const std::string& templateName, int geometryOverride, int lodIndex);

}  // namespace obf2::game
