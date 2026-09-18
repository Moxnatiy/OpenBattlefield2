#pragma once
// `--collision-near x/y/z`: what stands at a point and what of it collides.
//
// The measure for "the server stops us at a wall our world does not have": the
// placed objects within fifteen metres, the collision mesh each one's template
// and children name, and how many contacts a sphere at the point really finds. It
// is what found the howitzer whose barrel we ran through — the level's statics
// had nothing there at all, because the piece is spawned rather than placed.
#include "obf2/core/math.h"
#include "obf2/game/object_template.h"
#include "obf2/level/level.h"
#include "obf2/server/collision_world.h"

namespace obf2::app {

void probeCollisionNear(const Vec3f& at, const level::Level& level,
                        const game::Registry& registry, const server::CollisionWorld& world);

}  // namespace obf2::app
