#include "obf2/mesh/skinning.h"

#include <cmath>

namespace obf2::mesh {
namespace {

// Matrix multiplication in the same layout as in core: column-major.
Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 out;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[column * 4 + k];
      out.m[column * 4 + row] = sum;
    }
  }
  return out;
}

// A quaternion (x, y, z, w) and a translation into a matrix.
//
// The engine under DirectX works with row vectors and we with columns, so the
// rotation matrix here is transposed relative to the usual formula. The same is
// already done in `rotationY` in core/math.h.
Mat4 fromRotationTranslation(const float q[4], const Vec3& position) {
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  Mat4 out{};
  out.m[0] = 1.0f - 2.0f * (y * y + z * z);
  out.m[1] = 2.0f * (x * y - z * w);
  out.m[2] = 2.0f * (x * z + y * w);
  out.m[4] = 2.0f * (x * y + z * w);
  out.m[5] = 1.0f - 2.0f * (x * x + z * z);
  out.m[6] = 2.0f * (y * z - x * w);
  out.m[8] = 2.0f * (x * z - y * w);
  out.m[9] = 2.0f * (y * z + x * w);
  out.m[10] = 1.0f - 2.0f * (x * x + y * y);
  out.m[12] = position.x;
  out.m[13] = position.y;
  out.m[14] = position.z;
  out.m[15] = 1.0f;
  return out;
}

Vec3 transformPoint(const Mat4& m, const Vec3& v) {
  return Vec3{m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
              m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
              m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]};
}

Vec3 transformDirection(const Mat4& m, const Vec3& v) {
  return Vec3{m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
              m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
              m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
}

}  // namespace

namespace {

// Spherical interpolation between two quaternions. For close values it falls
// back to linear — otherwise dividing by the sine loses precision.
void slerp(const float from[4], const float to[4], float t, float out[4]) {
  float target[4] = {to[0], to[1], to[2], to[3]};
  float cosine = from[0] * to[0] + from[1] * to[1] + from[2] * to[2] + from[3] * to[3];
  if (cosine < 0.0f) {
    // The quaternions q and -q give the same rotation; take the closer one.
    for (float& value : target) value = -value;
    cosine = -cosine;
  }

  float weightFrom = 1.0f - t;
  float weightTo = t;
  if (cosine < 0.9995f) {
    const float angle = std::acos(cosine);
    const float sine = std::sin(angle);
    if (sine > 1e-6f) {
      weightFrom = std::sin((1.0f - t) * angle) / sine;
      weightTo = std::sin(t * angle) / sine;
    }
  }

  float length = 0.0f;
  for (int i = 0; i < 4; ++i) {
    out[i] = from[i] * weightFrom + target[i] * weightTo;
    length += out[i] * out[i];
  }
  length = std::sqrt(length);
  if (length > 1e-6f) {
    for (int i = 0; i < 4; ++i) out[i] /= length;
  }
}

}  // namespace

std::vector<Mat4> poseSkeleton(const Skeleton& skeleton, const std::vector<PoseStage>& stages) {
  // Step 1: spread the clips over the bones the way the engine does —
  // a stack per bone, and a weight of 1 clears it.
  struct Applied {
    const BoneAnimation* animation = nullptr;
    std::size_t track = 0;
    std::uint32_t frame = 0;
    float weight = 1.0f;
  };
  std::vector<std::vector<Applied>> perBone(skeleton.bones.size());

  for (const PoseStage& stage : stages) {
    if (stage.animation == nullptr || stage.weight <= 0.0f) continue;
    const float weight = stage.weight > 1.0f ? 1.0f : stage.weight;

    for (std::size_t track = 0; track < stage.animation->boneIds.size(); ++track) {
      const std::uint16_t bone = stage.animation->boneIds[track];
      if (bone >= perBone.size()) continue;

      std::vector<Applied>& stack = perBone[bone];
      if (weight >= 1.0f) stack.clear();
      else if (stack.size() >= static_cast<std::size_t>(kMaxPoseStagesPerBone)) {
        stack.erase(stack.begin());
      }
      stack.push_back(Applied{stage.animation, track, stage.frame, weight});
    }
  }

  // Step 2: the pose itself.
  std::vector<Mat4> world(skeleton.bones.size());
  for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
    const SkeletonBone& bone = skeleton.bones[i];

    float rotation[4] = {bone.rotation[0], bone.rotation[1], bone.rotation[2], bone.rotation[3]};
    Vec3 position = bone.position;

    for (const Applied& applied : perBone[i]) {
      float clipRotation[4];
      Vec3 clipPosition;
      applied.animation->sample(applied.track, applied.frame, clipRotation, &clipPosition);

      if (applied.weight >= 1.0f) {
        for (int k = 0; k < 4; ++k) rotation[k] = clipRotation[k];
        position = clipPosition;
      } else {
        float blended[4];
        slerp(rotation, clipRotation, applied.weight, blended);
        for (int k = 0; k < 4; ++k) rotation[k] = blended[k];
        position.x += (clipPosition.x - position.x) * applied.weight;
        position.y += (clipPosition.y - position.y) * applied.weight;
        position.z += (clipPosition.z - position.z) * applied.weight;
      }
    }

    const Mat4 local = fromRotationTranslation(rotation, position);
    world[i] = bone.parent >= 0 ? multiply(world[static_cast<std::size_t>(bone.parent)], local)
                                : local;
  }
  return world;
}

std::vector<Mat4> poseSkeleton(const Skeleton& skeleton, const BoneAnimation* animation,
                               std::uint32_t frame) {
  if (animation == nullptr) return poseSkeleton(skeleton, std::vector<PoseStage>{});
  return poseSkeleton(skeleton, std::vector<PoseStage>{PoseStage{animation, frame, 1.0f}});
}

void skinMesh(const RenderMesh& bindPose, const std::vector<Mat4>& boneWorld, RenderMesh& out) {
  if (bindPose.skin.size() != bindPose.vertices.size() || bindPose.rigs.empty()) return;
  out.vertices = bindPose.vertices;

  // Every vertex belongs to its own range, and a range to its own rig.
  // We walk the ranges so as to know which rig to apply.
  std::vector<int> rigForVertex(bindPose.vertices.size(), -1);
  for (const DrawRange& range : bindPose.ranges) {
    if (range.rig < 0) continue;
    for (std::uint32_t i = 0; i < range.indexCount; ++i) {
      const std::size_t at = range.indexStart + i;
      if (at >= bindPose.indices.size()) break;
      const std::uint32_t vertex = bindPose.indices[at];
      if (vertex < rigForVertex.size()) rigForVertex[vertex] = range.rig;
    }
  }

  for (std::size_t i = 0; i < bindPose.vertices.size(); ++i) {
    const int rigIndex = rigForVertex[i];
    if (rigIndex < 0 || static_cast<std::size_t>(rigIndex) >= bindPose.rigs.size()) continue;
    const Rig& rig = bindPose.rigs[static_cast<std::size_t>(rigIndex)];

    const SkinBinding& binding = bindPose.skin[i];
    const auto boneMatrix = [&](std::uint8_t rigBone, Mat4* result) {
      if (rigBone >= rig.bones.size()) return false;
      const Bone& entry = rig.bones[rigBone];
      if (entry.id >= boneWorld.size()) return false;
      *result = multiply(boneWorld[entry.id], entry.transform);
      return true;
    };

    Mat4 first{}, second{};
    const bool hasFirst = boneMatrix(binding.boneA, &first);
    const bool hasSecond = boneMatrix(binding.boneB, &second);
    if (!hasFirst && !hasSecond) continue;

    const float weightA = hasFirst ? (hasSecond ? binding.weight : 1.0f) : 0.0f;
    const float weightB = 1.0f - weightA;

    const Vertex& source = bindPose.vertices[i];
    Vec3 position{}, normal{};
    if (hasFirst) {
      const Vec3 p = transformPoint(first, source.position);
      const Vec3 n = transformDirection(first, source.normal);
      position.x += p.x * weightA; position.y += p.y * weightA; position.z += p.z * weightA;
      normal.x += n.x * weightA; normal.y += n.y * weightA; normal.z += n.z * weightA;
    }
    if (hasSecond) {
      const Vec3 p = transformPoint(second, source.position);
      const Vec3 n = transformDirection(second, source.normal);
      position.x += p.x * weightB; position.y += p.y * weightB; position.z += p.z * weightB;
      normal.x += n.x * weightB; normal.y += n.y * weightB; normal.z += n.z * weightB;
    }

    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length > 1e-6f) {
      normal.x /= length; normal.y /= length; normal.z /= length;
    }
    out.vertices[i].position = position;
    out.vertices[i].normal = normal;
  }
}

}  // namespace obf2::mesh
