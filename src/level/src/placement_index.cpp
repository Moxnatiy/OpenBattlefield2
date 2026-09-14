#include "obf2/level/placement_index.h"

#include <cmath>

namespace obf2::level {
namespace {

float distance(const Vec3f& a, const Vec3f& b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

std::string spawnerVehicle(const GameplayObjects& gameplay, const ObjectSpawner& spawner) {
  if (spawner.templateByTeam.empty()) return {};
  if (const ControlPoint* point = gameplay.controlPoint(spawner.controlPointId)) {
    if (const auto found = spawner.templateByTeam.find(point->team);
        found != spawner.templateByTeam.end()) {
      return found->second;
    }
  }
  return spawner.templateByTeam.begin()->second;
}

PlacementIndex::PlacementIndex(const GameplayObjects& gameplay, const Level& level) {
  for (const ObjectSpawner& spawner : gameplay.spawners) {
    std::string vehicle = spawnerVehicle(gameplay, spawner);
    if (vehicle.empty()) continue;
    spawners_.push_back(Spawner{spawner.position, spawner.rotation, std::move(vehicle)});
  }
  for (const StaticObject& object : level.objects) {
    statics_.push_back(Static{object.position, object.rotation, object.hasRotation, object.templateName});
  }
}

PlacedAt PlacementIndex::at(const Vec3f& position) const {
  PlacedAt out;
  float best = kTolerance;
  for (const Spawner& spawner : spawners_) {
    const float d = distance(spawner.position, position);
    if (d > best) continue;
    best = d;
    out.kind = PlacedAt::Kind::Spawned;
    out.templateName = spawner.vehicle;
    out.rotation = spawner.rotation;
    out.hasRotation = true;
  }
  if (out.kind == PlacedAt::Kind::Spawned) return out;
  for (const Static& object : statics_) {
    const float d = distance(object.position, position);
    if (d > best) continue;
    best = d;
    out.kind = PlacedAt::Kind::Static;
    out.templateName = object.name;
    out.rotation = object.rotation;
    out.hasRotation = object.hasRotation;
  }
  return out;
}

}  // namespace obf2::level
