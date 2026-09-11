// The red hatch over the ground outside the combat area.
//
// What is checked is the hole: the shipped texture keeps its hatch everywhere
// the player may not walk and loses its alpha where the player may. The shape
// is the level's own polygon, corners and all — that is what the original's
// spawn screen shows (docs/functions/hud-map.md).
#include "check.h"
#include "obf2/hud/combat_area.h"

using namespace obf2;

namespace {

// A 4x4 BGRA texture with the hatch's colour and full alpha everywhere.
texture::Texture solidHatch(std::uint32_t size) {
  texture::Texture out;
  out.format = texture::Format::Bgra8;
  out.width = size;
  out.height = size;
  out.mips.push_back(texture::MipLevel{size, size, 0, std::size_t(size) * size * 4});
  out.data.assign(std::size_t(size) * size * 4, std::byte{0});
  for (std::size_t i = 0; i < std::size_t(size) * size; ++i) {
    out.data[i * 4 + 0] = std::byte{39};   // blue
    out.data[i * 4 + 1] = std::byte{57};   // green
    out.data[i * 4 + 2] = std::byte{139};  // red
    out.data[i * 4 + 3] = std::byte{255};  // alpha
  }
  return out;
}

std::uint8_t alphaAt(const texture::Texture& texture, std::uint32_t column, std::uint32_t row) {
  const std::size_t at = (std::size_t(row) * texture.width + column) * 4 + 3;
  return static_cast<std::uint8_t>(texture.data[at]);
}

// A square from (-10,-10) to (10,10) in the world.
std::vector<Vec3f> squarePolygon() {
  return {Vec3f{-10.0f, 0.0f, -10.0f}, Vec3f{10.0f, 0.0f, -10.0f}, Vec3f{10.0f, 0.0f, 10.0f},
          Vec3f{-10.0f, 0.0f, 10.0f}};
}

}  // namespace

static void testInsidePolygon() {
  const std::vector<Vec3f> square = squarePolygon();
  CHECK(hud::insidePolygon(square, 0.0f, 0.0f));
  CHECK(hud::insidePolygon(square, 9.0f, -9.0f));
  CHECK(!hud::insidePolygon(square, 11.0f, 0.0f));
  CHECK(!hud::insidePolygon(square, 0.0f, -11.0f));
  // A polygon that is not one at all encloses nothing.
  CHECK(!hud::insidePolygon({Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{1.0f, 0.0f, 1.0f}}, 0.5f, 0.5f));
}

// The square of world the overlay covers is twice the polygon, so the hole
// should land in the middle four texels of a 4x4 picture.
static void testHole() {
  const texture::Texture hatch = solidHatch(4);
  const hud::WorldSquare over{-20.0f, -20.0f, 20.0f, 20.0f};
  const texture::Texture out = hud::buildCombatAreaOverlay(hatch, squarePolygon(), over);

  CHECK_EQ(int(out.width), 4);
  for (std::uint32_t row = 0; row < 4; ++row) {
    for (std::uint32_t column = 0; column < 4; ++column) {
      const bool middle = (row == 1 || row == 2) && (column == 1 || column == 2);
      CHECK_EQ(int(alphaAt(out, column, row)), middle ? 0 : 255);
    }
  }
  // The colour is untouched everywhere: only the alpha is written.
  CHECK_EQ(int(static_cast<std::uint8_t>(out.data[(1 * 4 + 1) * 4 + 2])), 139);
}

// v runs against z: the top row of the picture is the far edge of the square.
// A polygon in the half of the world where z is positive must clear the top.
static void testOrientation() {
  const texture::Texture hatch = solidHatch(4);
  const std::vector<Vec3f> north = {Vec3f{-20.0f, 0.0f, 1.0f}, Vec3f{20.0f, 0.0f, 1.0f},
                                    Vec3f{20.0f, 0.0f, 20.0f}, Vec3f{-20.0f, 0.0f, 20.0f}};
  const texture::Texture out =
      hud::buildCombatAreaOverlay(hatch, north, hud::WorldSquare{-20.0f, -20.0f, 20.0f, 20.0f});
  CHECK_EQ(int(alphaAt(out, 0, 0)), 0);
  CHECK_EQ(int(alphaAt(out, 0, 3)), 255);
}

// Anything that is not the game's own picture comes back as it went in: the
// overlay is that picture with a hole, not a picture of ours.
static void testLeavesForeignTexturesAlone() {
  texture::Texture compressed;
  compressed.format = texture::Format::Bc1;
  compressed.width = 4;
  compressed.height = 4;
  compressed.mips.push_back(texture::MipLevel{4, 4, 0, 8});
  compressed.data.assign(8, std::byte{0x7f});
  const texture::Texture out =
      hud::buildCombatAreaOverlay(compressed, squarePolygon(), hud::WorldSquare{-1, -1, 1, 1});
  CHECK(out.format == texture::Format::Bc1);
  CHECK_EQ(out.data.size(), std::size_t(8));
}

TEST_MAIN({
  testInsidePolygon();
  testHole();
  testOrientation();
  testLeavesForeignTexturesAlone();
})
