#pragma once
// The world's state, assembled from the server's packets.
//
// The client learns about the world by three different routes, and all three
// converge here:
//
//   * **events** — who is playing (`CreatePlayerEvent`), which objects were
//     created (`CreateObjectEvent`), who controls what (`EnterVehicleEvent`);
//   * **the controlled-object state** — the compression reference point per packet;
//   * **the ghost stream's records** — where the moving objects are now.
//
// Keeping this in the frame loop makes no sense: there is no window here, no
// time and no input — only packets. So it lives separately and is checked by a
// test on captured traffic (`tests/data/bf2-spawned.bin`) rather than "by eye in the game".
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>

#include "obf2/core/math.h"
#include "obf2/net/bf2_events.h"

namespace obf2::net::bf2 {

// An object the server created while in the game: another player's soldier or a vehicle.
struct RemoteObject {
  Vec3f position;
  std::optional<float> yaw;  // degrees, when a yaw arrived
  // The owner's team. Zero means it is not a player's soldier (a vehicle, level property).
  int team = 0;
  bool fromGhostStream = false;  // the position is refined by the stream, not only by an event

  // The track is a measure of the parsing, not decoration. A soldier stands on
  // the ground, so a constant difference from the terrain means the object's
  // origin is offset, while a growing difference means a parsing error. For a
  // motionless player `travelled` has to stay close to zero.
  Vec3f firstSeen;
  int updates = 0;
  float travelled = 0.0f;    // the largest displacement from the first position
  float aboveGround = 0.0f;  // the sum of the heights above the terrain
};

struct RemotePlayer {
  std::string name;
  int team = 0;
  std::uint16_t object = 0;  // the object the player occupied
};

class WorldView {
 public:
  // One data packet from the server. The order inside matters: the events first
  // (from them we learn which object is a soldier), then the controlled-object
  // state (the reference point), and only then the ghost records.
  void feed(std::span<const std::byte> packet);

  // The name we recognise ourselves by. The server assembles it as "clan tag,
  // space, name", so we compare by the tail.
  void setOwnName(std::string name) { ownName_ = std::move(name); }
  void setPlayerSpawned(bool spawned) { playerSpawned_ = spawned; }

  int ownPlayer() const { return ownPlayer_; }
  int ownTeam() const { return ownTeam_; }
  std::uint16_t ownObject() const { return ownObject_; }

  const std::map<std::uint32_t, RemotePlayer>& players() const { return players_; }
  const std::map<std::uint16_t, RemoteObject>& objects() const { return objects_; }
  const Vec3f& compressionReference() const { return compressionReference_; }

  // How many positions arrived from the stream and how many we rejected as unreadable.
  int positionUpdates() const { return positionUpdates_; }
  int rejected() const { return rejected_; }

  // A check on the parsing: a soldier stands on the ground, so a constant
  // difference from the terrain is an offset of the object's origin, while a
  // growing divergence is a parsing error. Whoever calls knows the terrain.
  void setGroundProbe(std::function<float(const Vec3f&)> probe) { ground_ = std::move(probe); }

 private:
  bool isSoldier(std::uint16_t id) const;
  Vec3f referenceFor(std::uint16_t id) const;
  bool looksSane(const Vec3f& at, bool soldier) const;

  std::string ownName_;
  int ownPlayer_ = -1;
  int ownTeam_ = 0;
  std::uint16_t ownObject_ = 0;
  bool playerSpawned_ = false;

  std::map<std::uint32_t, RemotePlayer> players_;
  std::map<std::uint16_t, RemoteObject> objects_;
  std::map<std::uint16_t, std::uint32_t> owners_;  // object -> player
  Vec3f compressionReference_;
  std::function<float(const Vec3f&)> ground_;
  int positionUpdates_ = 0;
  int rejected_ = 0;
};

}  // namespace obf2::net::bf2
