#pragma once
// Minimal maths for the renderer. Matrices are 4x4, column-major, the way
// Metal expects them (float4x4) and SDL_GPU takes them. Depth in NDC is [0,1],
// not [-1,1]: that is how Metal, D3D12 and Vulkan all work.
#include <array>
#include <cmath>

namespace obf2 {

struct Vec3f {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3f operator+(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3f operator-(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3f operator*(Vec3f v, float s) { return {v.x * s, v.y * s, v.z * s}; }

inline float dot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3f cross(Vec3f a, Vec3f b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3f v) { return std::sqrt(dot(v, v)); }
inline Vec3f normalize(Vec3f v) {
  const float len = length(v);
  return len > 0.0f ? v * (1.0f / len) : v;
}

struct Mat4 {
  // m[column * 4 + row]
  float m[16]{};

  static Mat4 identity() {
    Mat4 out;
    out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
    return out;
  }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
  Mat4 out;
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
      out.m[col * 4 + row] = sum;
    }
  }
  return out;
}

inline Mat4 translation(Vec3f t) {
  Mat4 out = Mat4::identity();
  out.m[12] = t.x;
  out.m[13] = t.y;
  out.m[14] = t.z;
  return out;
}

inline Mat4 scaling(float s) {
  Mat4 out = Mat4::identity();
  out.m[0] = out.m[5] = out.m[10] = s;
  return out;
}

inline Mat4 rotationY(float radians) {
  Mat4 out = Mat4::identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  out.m[0] = c;
  out.m[2] = -s;
  out.m[8] = s;
  out.m[10] = c;
  return out;
}

inline Mat4 rotationX(float radians) {
  Mat4 out = Mat4::identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  out.m[5] = c;
  out.m[6] = s;
  out.m[9] = -s;
  out.m[10] = c;
  return out;
}

inline Mat4 rotationZ(float radians) {
  Mat4 out = Mat4::identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  out.m[0] = c;
  out.m[1] = s;
  out.m[4] = -s;
  out.m[5] = c;
  return out;
}

// BF2 gives rotations as a triple of degrees yaw/pitch/roll (Y, X, Z) — and
// they are applied in exactly that order.
inline Mat4 rotationYawPitchRoll(float yawDegrees, float pitchDegrees, float rollDegrees) {
  constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
  return rotationY(yawDegrees * kToRadians) * rotationX(pitchDegrees * kToRadians) *
         rotationZ(rollDegrees * kToRadians);
}

inline Vec3f transformPoint(const Mat4& m, Vec3f v) {
  return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
          m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
          m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]};
}

// A direction, so no translation. Non-uniform scale would need the inverse
// transpose, but parts are never scaled in BF2.
inline Vec3f transformDirection(const Mat4& m, Vec3f v) {
  return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
          m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
          m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
}

// Left-handed, the camera looks along +Z — the same as `D3DXMatrixLookAtLH`:
// right is cross(up, forward), not the other way round.
inline Mat4 lookAt(Vec3f eye, Vec3f target, Vec3f up) {
  const Vec3f f = normalize(target - eye);
  const Vec3f s = normalize(cross(up, f));
  const Vec3f u = cross(f, s);

  Mat4 out = Mat4::identity();
  out.m[0] = s.x;  out.m[4] = s.y;  out.m[8]  = s.z;
  out.m[1] = u.x;  out.m[5] = u.y;  out.m[9]  = u.z;
  out.m[2] = f.x;  out.m[6] = f.y;  out.m[10] = f.z;
  out.m[12] = -dot(s, eye);
  out.m[13] = -dot(u, eye);
  out.m[14] = -dot(f, eye);
  return out;
}

// Left-handed, as in the game's engine.
//
// This is not an assumption: `RendDX9.dll` imports exactly
// `D3DXMatrixPerspectiveFovLH`, `D3DXMatrixLookAtLH` and `D3DXMatrixOrthoLH`.
// Refractor 2 is a DirectX 9 engine: X right, Y up, Z **into the screen**.
//
// The difference matters not for depth but for left and right: in `LookAtLH`
// the screen's right axis is `cross(up, forward)`, while in a right-handed
// system it is `cross(forward, up)` — exactly the opposite. Take the game's
// data, draw it with a right-handed pipeline, and the whole world comes out
// mirrored. That is precisely what we had.
inline Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 out;
  out.m[0] = f / aspect;
  out.m[5] = f;
  out.m[10] = farZ / (farZ - nearZ);
  out.m[11] = 1.0f;
  out.m[14] = -(farZ * nearZ) / (farZ - nearZ);
  return out;
}

// --- Culling ---------------------------------------------------------------

struct Plane {
  Vec3f normal;
  float distance = 0.0f;  // plane: dot(normal, point) + distance = 0

  float signedDistance(Vec3f point) const { return dot(normal, point) + distance; }
};

// The six frustum planes, normals pointing inwards.
struct Frustum {
  Plane planes[6];

  // The sphere is entirely behind one of the planes -> the object is not visible.
  bool intersectsSphere(Vec3f center, float radius) const {
    for (const Plane& plane : planes) {
      if (plane.signedDistance(center) < -radius) return false;
    }
    return true;
  }
};

// Pulls the planes out of the view-projection matrix (Gribb-Hartmann): the
// matrix rows already hold the needed combinations, they only have to be
// added and subtracted. Ours is column-major, hence m[column * 4 + row].
inline Frustum extractFrustum(const Mat4& m) {
  auto row = [&](int r) {
    return std::array<float, 4>{m.m[0 * 4 + r], m.m[1 * 4 + r], m.m[2 * 4 + r], m.m[3 * 4 + r]};
  };
  const auto x = row(0);
  const auto y = row(1);
  const auto z = row(2);
  const auto w = row(3);

  auto makePlane = [](const std::array<float, 4>& a, const std::array<float, 4>& b, bool add) {
    Plane plane;
    const float sign = add ? 1.0f : -1.0f;
    plane.normal = Vec3f{b[0] + sign * a[0], b[1] + sign * a[1], b[2] + sign * a[2]};
    plane.distance = b[3] + sign * a[3];

    // Normalise, otherwise comparing with the radius makes no sense.
    const float len = length(plane.normal);
    if (len > 0.0f) {
      plane.normal = plane.normal * (1.0f / len);
      plane.distance /= len;
    }
    return plane;
  };

  Frustum frustum;
  frustum.planes[0] = makePlane(x, w, true);   // left
  frustum.planes[1] = makePlane(x, w, false);  // right
  frustum.planes[2] = makePlane(y, w, true);   // bottom
  frustum.planes[3] = makePlane(y, w, false);  // top
  // Our NDC depth is [0,1], so the near plane is simply the z row and not
  // z + w, as it would be for the [-1,1] range.
  frustum.planes[4] = Plane{Vec3f{z[0], z[1], z[2]}, z[3]};
  const float nearLength = length(frustum.planes[4].normal);
  if (nearLength > 0.0f) {
    frustum.planes[4].normal = frustum.planes[4].normal * (1.0f / nearLength);
    frustum.planes[4].distance /= nearLength;
  }
  frustum.planes[5] = makePlane(z, w, false);  // far

  return frustum;
}

}  // namespace obf2
