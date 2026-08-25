#pragma once
// Мінімальна математика для рендера. Матриці — 4x4, стовпцями (column-major),
// як їх очікує Metal (float4x4) і як їх приймає SDL_GPU. Глибина в NDC — [0,1],
// а не [-1,1]: так працюють і Metal, і D3D12, і Vulkan.
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

// Права система координат, камера дивиться вздовж -Z.
inline Mat4 lookAt(Vec3f eye, Vec3f target, Vec3f up) {
  const Vec3f f = normalize(target - eye);
  const Vec3f s = normalize(cross(f, up));
  const Vec3f u = cross(s, f);

  Mat4 out = Mat4::identity();
  out.m[0] = s.x;  out.m[4] = s.y;  out.m[8]  = s.z;
  out.m[1] = u.x;  out.m[5] = u.y;  out.m[9]  = u.z;
  out.m[2] = -f.x; out.m[6] = -f.y; out.m[10] = -f.z;
  out.m[12] = -dot(s, eye);
  out.m[13] = -dot(u, eye);
  out.m[14] = dot(f, eye);
  return out;
}

inline Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 out;
  out.m[0] = f / aspect;
  out.m[5] = f;
  out.m[10] = farZ / (nearZ - farZ);
  out.m[11] = -1.0f;
  out.m[14] = (farZ * nearZ) / (nearZ - farZ);
  return out;
}

}  // namespace obf2
