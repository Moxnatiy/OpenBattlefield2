#pragma once
// Which LOD of a placed mesh is drawn, by distance from the camera.
//
// `RendDX9.dll`: the LOD load (StaticMeshTemplate 0x10119a80) keeps one
// switch distance per LOD left after the quality skip but the last; the
// static-mesh draw (0x100fe5d0) picks, every frame, LOD i + 1 for the highest
// i whose `scale · distance[i]` is below `fovFactor · d − baseRadius`.
#include <cstddef>
#include <vector>

#include "obf2/mesh/bf2_mesh.h"

namespace obf2::game {

// The switch distances of one geometry after `skip` leading LODs were dropped,
// for `count` LODs left (count − 1 values). From 0x10119a80: a running value
// starts at (skip + 1) · 25 m; slot i takes the template's
// `setSubGeometryLodDistance` value at skip + i when the template's list for
// this geometry is longer than i, and the running value grows by that list's
// value at i; otherwise slot i takes the running value and it grows by 25 m.
// A list longer than i but not than skip + i is read past its end in the
// binary; that value is not measured, and the running value is used instead.
// `step` is 25 m for a static mesh (0x10119a80) and 50 m for a bundled one,
// whose load (0x100ea5f0) fills its list by the same rule from (skip + 1) · 50.
inline constexpr float kStaticLodStep = 25.0f;
inline constexpr float kBundledLodStep = 50.0f;
std::vector<float> lodSwitchDistances(const std::vector<float>* given, std::size_t skip,
                                      std::size_t count, float step = kStaticLodStep);

// The base the distance is reduced by: the radius of the box of the first LOD
// left, about the box's centre (0x100fc630 copies the LOD's +0x20, which
// 0x101c6570 computes as the farthest corner from the centre).
float lodBaseRadius(const mesh::Aabb& box);

// `renderer+0x8b0 · renderer+0x84c`, both set by the geometry quality at
// 0x1000fb50: 2 · 0.5 at low, 1 · 0.8 at medium, 1 · 1 at high.
float lodDistanceScale(int geometryQuality);

// The bundled-mesh draw (0x101050c0) scales by `renderer+0x84c ·
// renderer+0x8b4`. +0x8b4 (`globalBundleMeshLodDistanceScale`, console
// accessor 0x1005aeb0) is 1 from the renderer's constructor (0x1002dc40) and
// the quality does not touch it, so the scale is 0.5 at low, 0.8 at medium,
// 1 at high.
float lodDistanceScaleBundled(int geometryQuality);

// The camera's LOD factor (camera +0x20, vtable +0x2c): setFov (0x101c2bd0)
// makes it tan(fov / 2) / tan(firstFov / 2), the first field of view the
// camera was ever given standing for 1.
float lodFovFactor(float fov, float firstFov);

// The LOD index, 0 … distances.size(), for an object `distance` metres from
// the camera (0x100fe5d0).
std::size_t pickLod(const std::vector<float>& distances, float scale, float fovFactor,
                    float distance, float baseRadius);

}  // namespace obf2::game
