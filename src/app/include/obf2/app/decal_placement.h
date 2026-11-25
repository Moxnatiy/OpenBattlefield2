#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace obf2::app {

// The renderer's shared random state for a decal insertion (RendDX9.dll,
// docs/functions/m4-effects.md, "Decal size, rotation and atlas frame").
// Generator 0x1009f860: Park-Miller, two Schrage steps by 0x5e30 and 0x661f,
// so one step multiplies by 630360016 modulo 2^31 - 1, and every value lies
// in [1, 2^31 - 2]. Insertion 0x101ae980 and geometry 0x101ae450 both pass
// ECX = 0x1023b618, whose static initial value in .data is 0x759dc150.
// Other renderer users of 0x1023b618 are not enumerated, so a Retail sequence
// is not reproduced; only the generator, its range and the draw order are.
struct DecalRandom {
  std::uint32_t state = 0x759dc150u;

  std::uint32_t next() {
    state = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(state) * 630360016u) % 2147483647u);
    return state;
  }
  // `((int)(value | 0x80) >> 7) + 1`, the integer every call site converts
  // (after the calls at 0x101ae475, 0x101ae4c8, 0x101aea9d, 0x101aeb75): [2, 2^24].
  std::int32_t scaled() { return (static_cast<std::int32_t>(next() | 0x80u) >> 7) + 1; }
  // Integer * 2^-23 - 1 (constants 0x10221810, 0x101ddde4), in (-1, 1].
  float signedUnit() { return static_cast<float>(scaled()) * 1.1920928955078125e-7f - 1.0f; }
};

// The template fields the placement reads. Property names map to renderer
// DecalTemplate vtable 0x10221928 slots through the BF2.exe setters
// 0x646f10..0x6473c0 (registration 0x560600..0x560c00):
//   size +0x18 (slot +0x08/+0x0c), randomSize +0x1c (+0x10/+0x14),
//   randomRotation +0x24 (+0x40/+0x44), rotation +0x28 (+0x48/+0x4c),
//   randomColorIntensity +0x04 (+0x18/+0x1c), animationEnable +0x88 (+0x58),
//   animationRandomizedStartFrame +0x80 (+0x60), animationFrameCount +0x94
//   (+0x90), animationFrameCountX +0x98 (+0x98).
struct DecalTemplateValues {
  float size = 0.0f;
  float randomSize = 0.0f;
  float rotation = 0.0f;        // degrees
  float randomRotation = 0.0f;  // degrees
  bool animationEnable = false;
  bool randomizedStartFrame = false;
  std::uint32_t frameCount = 0;
  std::uint32_t frameCountX = 0;
};

struct DecalPlacement {
  float halfExtent = 0.0f;  // the prototype is the unit square +-1 (0x1023aef8)
  float radians = 0.0f;
  std::uint32_t frame = 0;  // atlas frame; 0 when the atlas is off
  float brightnessUnit = 0.0f;  // the colour draw, consumed but not applied
};

// One insertion, in Retail's draw order: colour (0x101aea9d), start frame
// (0x101aeb75, only with animationRandomizedStartFrame), rotation
// (0x101ae475), size (0x101ae4c8). Rotation and size are drawn only on the
// projected-geometry path 0x101ae450; the alternative 0x101acdf0 is not read.
inline DecalPlacement placeDecal(const DecalTemplateValues& t, DecalRandom& random) {
  DecalPlacement out;
  // Brightness is clamp(1 + r * randomColorIntensity, 0, 1) * color * 255 with
  // alpha 0xff (0x101aeaca..0x101aeb64). Scene draw items carry no tint, so the
  // value is not applied here: not modeled. Stock small decals use intensity 0
  // and colour 1/1/1 (Effects/decals/small/*.tweak).
  out.brightnessUnit = random.signedUnit();
  // Entry +0x18 = float(scaled) * float(frameCount - 1) * 2^-24
  // (0x101aeb7a..0x101aeba9, constant 0x1020cf68), otherwise 0 (0x101aebae).
  float frameValue = 0.0f;
  if (t.randomizedStartFrame) {
    const auto scaled = static_cast<float>(random.scaled());
    const auto span = static_cast<float>(t.frameCount - 1u);  // unsigned, as at 0x101aeb82
    frameValue = scaled * span * 5.9604644775390625e-8f;
  }
  // Angle = (rotation + r * randomRotation) * pi/180 (0x1023add0 = 0.0174533).
  out.radians = (t.rotation + random.signedUnit() * t.randomRotation) * 0.01745329238474369f;
  // Scale = (size + r * randomSize) * argument 4. Argument 4 comes from the
  // insertion's caller and is not measured; it is taken as 1 here.
  out.halfExtent = t.size + random.signedUnit() * t.randomSize;
  // The frame is rounded only when animationEnable and a nonzero frame count
  // gate the atlas (template +0x88 and +0x94 in 0x101ae450). x87 rounding uses the current control word,
  // which is not sampled at that call; round-to-nearest is the x87 default.
  if (t.animationEnable && t.frameCount != 0) {
    out.frame = static_cast<std::uint32_t>(std::nearbyint(frameValue));
  }
  return out;
}

// Atlas offset of a frame relative to the template's base UV (0x101ae450,
// template +0x98 count, +0x9c = 1 / count from setter 0x101af8b0):
// u = (frame % countX) * frameWidth, v = (frame - frame % countX) / countX *
// frameHeight. Retail divides by countX without a guard; a zero count is
// clamped to 1 here and is not measured.
struct DecalFrameOffset { float u = 0.0f, v = 0.0f; };
inline DecalFrameOffset decalFrameOffset(std::uint32_t frame, std::uint32_t countX,
                                         float width, float height) {
  const std::uint32_t columns = std::max<std::uint32_t>(countX, 1u);
  const std::uint32_t column = frame % columns;
  return {static_cast<float>(column) * width,
          static_cast<float>(frame - column) * (1.0f / static_cast<float>(columns)) * height};
}

}  // namespace obf2::app
