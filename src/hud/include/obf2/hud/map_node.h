#pragma once
// The map: where it travels, how it grows and where `MapFullSize` and
// `MapMinSize`.
//
// This is not a "show the big map" flag. In the client the map is a node of its
// own with its own target/current pair, and **the size is driven by animation**,
// while both show variables are derived from the size every frame. That is
// exactly why in the original the transition between the minimap and the big one
// is smooth, and the frames (`MinSizeAlpha`, `MapFrameOpen`) switch not
// instantly but when the size has reached its end.
//
// Sources:
//   `BF2.exe`, 0x777dc0 — the HUD's state -> the target (position and size);
//   `BF2.exe`, 0x77c330 — the animation step and deriving the flags.
//
// The map node's fields (`BF2.exe`, offsets from the map object):
//
//   +0x68c  `MapFullSize`   derived from the size, 0x77d3f8
//   +0x68d  `MapMinSize`    the same place
//   +0x68e  commander mode (also the index into the zoom tables)
//   +0x690  spawn-screen mode       set by 0x777f9a
//   +0x691  commander mode          set by 0x7781b0
//   +0x692  menu mode (0x11/0x12)   set by 0x778299
//   +0x694  1 - target height / screen height (the other way round for the commander)
//   +0x698  the current zoom, +0x6d0 its index
//   +0x778  target: width           +0x77c  target: height
//   +0x780  target: X               +0x784  target: Y
//   +0x788  `setMiniPos`   (0x75f319)   +0x798  `setMiniSize`
//   +0x7a8  `setCommanderPos`           +0x7b0  `setCommanderSize`
//   +0x7b8  `setMaxiSize`  (0x75f423)   +0x7c0  `setMaxiPos` (0x75f555)
//   +0x776  whether the size arrived (as of last frame)
//   +0x7f8  "snap at once", without smoothing
//
// The current position and size lie not in the node itself but in shared cells
// (`FUN_0065ddd0` — position, `FUN_00659990` — size); half of an 800x600 screen
// is added to the position, as in `useMapView`.
#include "obf2/hud/hud.h"

namespace obf2::hud {

// The approach speed towards the target is 6.0 (`BF2.exe`, 0x77d1c1 and its
// neighbours: `FUN_00402ee0(dt * -6.0, ...)`, then e^x). The step is:
//   value += (1 - e^(-6*dt)) * (target - value)
inline constexpr float kMapApproachRate = 6.0f;

// Closer than this we simply snap to the target (0x77d18f for position and size).
inline constexpr float kMapSnapDistance = 0.1f;

// The tolerance at which the size counts as "arrived" (0x77d3d1: `- 3.0`).
inline constexpr float kMapSizeTolerance = 3.0f;

// The magnification's base is 2.3 (`BF2.exe`, a double constant at 0x930150,
// taken at 0x77397c). Every next zoom index magnifies by another factor of
// 2.3.
inline constexpr float kMapZoomBase = 2.3f;

// There are three zoom indices: the tables in the map node hold three numbers
// each (+0x6d8, +0x6e4, +0x6f0 — chosen at 0x77cf6a).
inline constexpr int kMapZoomLevels = 3;

// The HUD state numbers the map distinguishes (`BF2.exe`, 0x777e11 — a switch on
// the state number; the state table is docs/functions/hud-states.md).
inline constexpr int kMapStateIngame = 0;      // the minimap in the corner
inline constexpr int kMapStateSpawn = 1;       // the spawn screen
inline constexpr int kMapStateBigMap = 2;      // the big map (the M key)
inline constexpr int kMapStateCommander = 15;  // 0xf
inline constexpr int kMapStateSquadLeaderMenu = 17;  // 0x11
inline constexpr int kMapStateCommanderMenu = 18;    // 0x12

struct MapPoint {
  float x = 0.0f;
  float y = 0.0f;
};

// The angle's approach speed is 9.0 (`BF2.exe`, the constant at 0x930334 = -9.0,
// used twice: 0x77c542 and 0x77c68b).
inline constexpr float kMapAngleRate = 9.0f;

// The "already there" threshold for the angles is 0.001 (`FUN_00772310`, the
// constant 0x3a83126f at 0x77c4fd and 0x77c63e).
inline constexpr float kMapAngleEpsilon = 0.001f;

// The map's angle: two smoothers in a row.
//
// The first (+0x75c) drives the angle towards the player's current direction, the
// second (+0x760) towards the first. The second is `MinimapDelayedMapAngle`: the
// name is assembled from the node's name at 0x780a49 and attached to +0x760.
// Because of that the minimap's compass lags behind the player's turn twice
// over, and that is exactly how it looks in the original.
//
// The direction itself is computed at 0x751d8b: `atan2(direction.x, direction.z)`
// — so zero looks north (+Z).
class MapAngle {
 public:
  // Where the player is looking, in radians (0x772470 writes this into +0x770).
  // The camera's wobble after a sharp turn (fields +0x724, +0x758, +0x775,
  // +0x777) we do **not** reproduce: it exists in the original, but it has not
  // been cross-checked.
  void setTarget(float radians) { target_ = radians; }

  void update(float dt);

  // +0x75c — the smoothed direction.
  float angle() const { return angle_; }
  // +0x760 — `MinimapDelayedMapAngle`, what the compass turns by.
  float delayed() const { return delayed_; }

 private:
  float target_ = 0.0f;
  float angle_ = 0.0f;
  float delayed_ = 0.0f;
};

class MapNode {
 public:
  // The three rectangles from the data (`HudElementsMap.con`: setMiniPos/Size,
  // setMaxiPos/Size, setCommanderPos/Size). We take them from the assembled tree
  // so as not to duplicate the parsing.
  void takeRects(const Node& node);

  // The HUD's state changed. Verbatim 0x777dc0: the map changes its target only
  // in a few states and touches nothing in the rest.
  void applyState(int state);

  // The animation step, `dt` in seconds (0x77c330).
  void update(float dt);

  // Snap to the target at once — field +0x7f8. Used when the map has just been
  // created: otherwise it would drive in from zero.
  void snap();

  // Where the map looks: the player's position in fractions of the world's size,
  // both in [0, 1]. 0x773630 writes it into the fields +0x740/+0x744, and
  // 0x751d55 computes it: `(sizeX/2 + playerX) / sizeX`. On the second axis the
  // client's number is negative (it has `-1/sizeZ`), while ours is a plain `v`,
  // as in the map's markers.
  void setCentre(float u, float v);

  // The zoom index, 0..2. The console sets it: `MiniMap.setZoom`
  // (`BF2.exe`, 0x57a97e writes straight into the map node's field +0x6d0).
  void setZoomIndex(int index);
  int zoomIndex() const { return zoomIndex_; }

  MapPoint position() const { return position_; }
  MapPoint size() const { return size_; }

  // +0x748/+0x74c — the smoothed centre, and it is what gets drawn.
  MapPoint centre() const { return centre_; }

  // +0x698 — the smoothed zoom index.
  float zoom() const { return zoom_; }

  // How many times magnified. `pow(2.3, zoom)` — the base lies as a double
  // constant at 0x930150, and the exponent itself is taken at 0x77397c.
  float zoomScale() const;

  // The show variables derived from the size (0x77d3f8).
  bool fullSize() const { return fullSize_; }
  bool minSize() const { return minSize_; }
  // +0x776: the size stands at one of its ends rather than in transit.
  bool settled() const { return settled_; }

 private:
  MapPoint mini_{197.0f, -300.0f};
  MapPoint miniSize_{197.0f, 197.0f};
  MapPoint maxi_{-122.0f, -273.0f};
  MapPoint maxiSize_{512.0f, 512.0f};
  MapPoint commander_{-161.0f, -281.0f};
  MapPoint commanderSize_{561.0f, 561.0f};

  MapPoint targetPosition_{197.0f, -300.0f};
  MapPoint targetSize_{197.0f, 197.0f};

  // The current one is already in screen coordinates (target + half a screen), as in
  // `useMapView`.
  MapPoint position_{kReferenceWidth * 0.5f + 197.0f, kReferenceHeight * 0.5f - 300.0f};
  MapPoint size_{197.0f, 197.0f};

  // The centre: the target (+0x740/+0x744) and the smoothed one (+0x748/+0x74c).
  // While there is no player we look at the middle of the world — just as
  // 0x751d78 does when there is no controlled object yet.
  MapPoint targetCentre_{0.5f, 0.5f};
  MapPoint centre_{0.5f, 0.5f};

  // The zoom: the index (+0x6d0) and the smoothed value (+0x698). The value is
  // driven towards the index itself — visible from the comparison at 0x77d4ad,
  // where +0x698 is checked against `(float)+0x6d0`.
  int zoomIndex_ = 0;
  float zoom_ = 0.0f;

  bool commanderMode_ = false;
  bool snapNext_ = true;
  bool fullSize_ = false;
  bool minSize_ = true;
  bool settled_ = true;
  int state_ = kMapStateIngame;
};

}  // namespace obf2::hud
