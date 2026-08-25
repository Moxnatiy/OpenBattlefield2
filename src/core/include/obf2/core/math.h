#pragma once
// Мінімальна математика для рендера. Матриці — 4x4, стовпцями (column-major),
// як їх очікує Metal (float4x4) і як їх приймає SDL_GPU. Глибина в NDC — [0,1],
// а не [-1,1]: так працюють і Metal, і D3D12, і Vulkan.
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

// BF2 задає повороти трійкою градусів yaw/pitch/roll (Y, X, Z) — саме в
// такому порядку вони й застосовуються.
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

// Напрямок — без переносу. Для нерівномірного масштабу знадобилася б
// обернено-транспонована матриця, але у BF2 частини не масштабуються.
inline Vec3f transformDirection(const Mat4& m, Vec3f v) {
  return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
          m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
          m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
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

// --- Відсікання невидимого -------------------------------------------------

struct Plane {
  Vec3f normal;
  float distance = 0.0f;  // площина: dot(normal, точка) + distance = 0

  float signedDistance(Vec3f point) const { return dot(normal, point) + distance; }
};

// Шість площин піраміди видимості, нормалі дивляться всередину.
struct Frustum {
  Plane planes[6];

  // Сфера повністю за якоюсь площиною -> об'єкт не видно.
  bool intersectsSphere(Vec3f center, float radius) const {
    for (const Plane& plane : planes) {
      if (plane.signedDistance(center) < -radius) return false;
    }
    return true;
  }
};

// Витягує площини з матриці вигляд-проєкція (метод Gribb-Hartmann): рядки
// матриці вже містять потрібні комбінації, лишається їх скласти й відняти.
// Матриця у нас по стовпцях, тому m[column * 4 + row].
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

    // Нормуємо, інакше порівняння з радіусом не має сенсу.
    const float len = length(plane.normal);
    if (len > 0.0f) {
      plane.normal = plane.normal * (1.0f / len);
      plane.distance /= len;
    }
    return plane;
  };

  Frustum frustum;
  frustum.planes[0] = makePlane(x, w, true);   // ліва
  frustum.planes[1] = makePlane(x, w, false);  // права
  frustum.planes[2] = makePlane(y, w, true);   // нижня
  frustum.planes[3] = makePlane(y, w, false);  // верхня
  // Глибина в NDC у нас [0,1], тому ближня площина це просто рядок z,
  // а не z + w, як було б для діапазону [-1,1].
  frustum.planes[4] = Plane{Vec3f{z[0], z[1], z[2]}, z[3]};
  const float nearLength = length(frustum.planes[4].normal);
  if (nearLength > 0.0f) {
    frustum.planes[4].normal = frustum.planes[4].normal * (1.0f / nearLength);
    frustum.planes[4].distance /= nearLength;
  }
  frustum.planes[5] = makePlane(z, w, false);  // дальня

  return frustum;
}

}  // namespace obf2
