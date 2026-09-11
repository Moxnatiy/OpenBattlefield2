#pragma once
// The red hatch the original lays over the spawn screen's map.
//
// The ground a player may not walk on is hatched in red on the map, and the
// engine does it with one texture and one quad: `Ingame/Minimap/map_CombatArea32.dds`
// — 512x512 uncompressed, a single colour (139, 57, 39) carrying a diagonal
// hatch in its alpha channel, solid along its border — drawn over the map node's
// whole square at alpha 0.8 (`BF2.exe`, the map node's constructor at 0x780180
// loads it into +0x944 and +0x948; the dump of the original's spawn screen has
// the quad at 278.5,27.5 511.5x511.5 sampling the whole texture).
//
// A texture that ships with the game cannot know a level's combat area, so the
// engine writes into it, and a screenshot of the original says what it writes:
// the hatch stands everywhere except **inside** the combat area, where the map
// shows through untouched. The shape has the polygon's own outline, corners and
// all. So the write is the smallest one that produces it — the alpha is cleared
// inside the polygon and the shipped hatch is left alone outside.
//
// That the engine does it by clearing alpha rather than by some other means is
// not read out of the binary; what is measured is the picture on screen and the
// texture that produces it.
#include <string_view>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/texture/dds.h"

namespace obf2::hud {

// Where the game keeps the hatch, and the name we hand the finished overlay to
// the renderer under. The second is not a file: like `#flash`, it is a picture
// the application makes and the texture resolver answers for by name.
inline constexpr std::string_view kCombatAreaSource =
    "Menu/HUD/Texture/Ingame/Minimap/map_CombatArea32.dds";
inline constexpr std::string_view kCombatAreaTexture = "#combatarea";

// The square of world the overlay covers, in the same units as the polygon.
// It is the **uncut** crop square the map is built around — the one centred on
// the combat area whose side is the area's larger side plus the map's margin.
// Measured: on Strike at Karkand the overlay's left edge is world x -603, and
// the crop square runs -602.9..117.5 while the map picture beside it only shows
// -512 onwards, the part that exists in the picture.
struct WorldSquare {
  float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
};

// True when the point is inside the closed polygon. Ray casting along +x; a
// point exactly on an edge may land either way, which does not matter for a
// hatch drawn at a 512th of the map.
bool insidePolygon(const std::vector<Vec3f>& polygon, float x, float z);

// The shipped hatch with a hole cut in it. `hatch` must be the game's
// `map_CombatArea32.dds` — 8-bit BGRA; anything else is returned unchanged,
// because inventing the picture is worse than drawing the one that exists.
//
// The texture's u runs with x and its v runs **against** z, the same way the
// map picture's does: v = 0 at the square's far edge.
texture::Texture buildCombatAreaOverlay(const texture::Texture& hatch,
                                        const std::vector<Vec3f>& polygon,
                                        const WorldSquare& square);

}  // namespace obf2::hud
