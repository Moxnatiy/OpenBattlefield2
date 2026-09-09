#pragma once
// Simple bodies that are not in the game's files.
//
// Needed where we know **where** an object is but cannot yet take its real
// geometry. Such a placeholder is more honest than an empty space: it shows
// the object is there, and shows this is not what it really looks like.
#include <string>

#include "obf2/mesh/bf2_mesh.h"

namespace obf2::mesh {

// A box centred in the middle of its base (zero at foot level), of the given
// size and with one material carrying the given "texture". A name of the form
// `#RRGGBB` is drawn as a solid colour — the same as the water's colour.
RenderMesh buildBox(const Vec3& size, const std::string& map);

}  // namespace obf2::mesh
