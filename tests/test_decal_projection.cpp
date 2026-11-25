#include "check.h"
#include <cstdlib>
#include <filesystem>
#include "obf2/app/decal_projection.h"
#include "obf2/mesh/primitives.h"
#include "obf2/vfs/filesystem.h"

using namespace obf2;
int main() {
  // Independent retained Retail geometry: the rear fence plane .114613056
  // emits at .104613056 after separation, m4-effects.md/receiver captures.
  auto rear = mesh::buildScreenQuad({});
  for (auto& vertex : rear.vertices) vertex.position.z = .114613056f;
  // Reverse the front-facing quad for a ray approaching the back of the panel.
  for (std::size_t i = 0; i < rear.indices.size(); i += 3)
    std::swap(rear.indices[i], rear.indices[i+2]);
  const auto result = app::projectStaticDecal(rear, Mat4::identity(), {0,0,-1}, {0,0,2},
                                             .027f,0,0,0,.5f,.5f);
  CHECK(result && !result->vertices.empty());
  if (result) for (const auto& vertex : result->vertices)
    CHECK(std::abs(vertex.position.z - .104613056f) < .000001f);
  // A boundary-crossing polygon retains interpolated UVs and cannot spill
  // beyond the receiver edge. Exact edge points remain inside.
  const auto clipped = app::clipDecalPolygon({{{-.5f,-.5f,0},0,1},{{.5f,-.5f,0},1,1},
      {{.5f,.5f,0},1,0},{{-.5f,.5f,0},0,0}}, {{{0,-1,0},{1,-1,0},{0,1,0}}});
  CHECK(clipped && clipped->size() >= 3);
  if (clipped) for (const auto& vertex : *clipped) {
    CHECK(vertex.position.x >= 0);
    CHECK(std::abs(vertex.u - (vertex.position.x + .5f)) < .000001f);
  }

  const char* data = std::getenv("OBF2_GAME_DATA");
  if (data) {
    FileSystem files;
    for (const char* list : {"ServerArchives.con", "ClientArchives.con"})
      mountArchivesFromCon(files,data,std::filesystem::path(data)/list);
    files.mountDirectory(data);
    const auto bytes = files.read("objects/staticobjects/common/destroyableobjects/"
      "fence_corrugated_3x12m_broken/meshes/fence_corrugated_3x12m_broken.staticmesh");
    CHECK(bytes.has_value()); if (!bytes) return 1;
    const auto original = mesh::load(*bytes,mesh::Kind::Static);
    CHECK(original.has_value()); if (!original) return 1;
    const auto fence = mesh::extract(*original,0,0);
    CHECK(fence.has_value()); if (!fence) return 1;
    const auto mark = app::projectStaticDecal(*fence,Mat4::identity(),{.018f,.0665f,1},
                                            {0,0,-2},.027f,0,0,0,.5f,.5f);
    CHECK(mark && !mark->vertices.empty()); if (!mark || mark->vertices.empty()) return 1;
    for (const auto& vertex : mark->vertices) {
      // Independent Retail entry and original render-plane measurement.
      CHECK(std::abs(vertex.position.z - .178134689f) < .000002f);
    }
    std::printf("Original fence projected vertices=%zu front_z=%.9f\n",
                 mark->vertices.size(),mark->vertices.front().position.z);
  }
  return obf2test::g_failures ? 1 : 0;
}
