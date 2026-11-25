// The world's state from captured traffic.
//
// The sample `tests/data/bf2-spawned.bin` was taken after the player spawned and
// with input sent (`openbf2 --connect ... --record`), so it holds everything at
// once: the player events, the controlled-object state and the ghost records.
#include <bit>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "obf2/net/bf2_world.h"
#include "obf2/net/bf2_link.h"
#include "check.h"

namespace {

std::vector<std::vector<std::byte>> loadCapture(const std::string& path) {
  std::vector<std::vector<std::byte>> packets;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return packets;
  while (true) {
    std::uint32_t length = 0;
    if (std::fread(&length, sizeof(length), 1, file) != 1) break;
    if (length == 0 || length > 4096) break;
    std::vector<std::byte> packet(length);
    if (std::fread(packet.data(), 1, length, file) != length) break;
    packets.push_back(std::move(packet));
  }
  std::fclose(file);
  return packets;
}

}  // namespace

void testWorldViewCollectsPlayersAndObjects() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-spawned.bin");
  CHECK(!packets.empty());

  obf2::net::bf2::WorldView world;
  world.setOwnName("OpenBF2");
  for (const auto& packet : packets) world.feed(packet);

  // The players from the capture: it held both us and somebody else.
  CHECK(!world.players().empty());
  // We recognised ourselves by name — the server sends it with a leading space.
  CHECK(world.ownPlayer() >= 0);
  CHECK(world.ownTeam() > 0);
  CHECK_EQ(world.tickets(1).value_or(-1), 99);
  CHECK_EQ(world.tickets(2).value_or(-1), 77);

  // The world's objects assembled too, and all of them are within the map.
  CHECK(!world.objects().empty());
  for (const auto& [id, object] : world.objects()) {
    CHECK(id != 0);
    CHECK(std::abs(object.position.x) < 1024.0f);
    CHECK(std::abs(object.position.z) < 1024.0f);
  }
}

// The compression reference point comes from the controlled-object state and does
// not stay zero: without it the positions from the stream would scatter.
void testWorldViewTakesCompressionReference() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-spawned.bin");
  obf2::net::bf2::WorldView world;
  for (const auto& packet : packets) world.feed(packet);

  const obf2::Vec3f& reference = world.compressionReference();
  const bool zero = reference.x == 0.0f && reference.y == 0.0f && reference.z == 0.0f;
  CHECK(!zero);
  CHECK(std::abs(reference.x) < 1024.0f);
  CHECK(std::abs(reference.z) < 1024.0f);
}

// Another player's objects from their ghost records.
// `tests/data/bf2-karkand-other-player.bin`: the original client (`defaultPlayer`)
// sat in the gas station's jeep on Strike at Karkand while ours spawned. The jeep
// is 1841 (template 5184), his soldier 1731 (template 3283). The jeep has a player
// in it and is still a jeep: every one of its positions has to lie on the spot its
// `CreateObjectEvent` named (-149.53, 162.14, -269.96) — before our spawn, when
// the server writes raw floats, and after it, when it writes a difference from our
// own soldier's position. Read with the soldier's layout, as it was while "a
// player entered it" meant "a soldier", they scattered.
void testOtherPlayersObjectsFromTheirRecords() {
  const auto packets =
      loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-karkand-other-player.bin");
  CHECK(!packets.empty());
  obf2::net::bf2::WorldView world;
  world.setOwnName("OpenBF2");
  int updates = 0, onSpot = 0, before = 0;
  for (const auto& packet : packets) {
    before = world.objects().count(1841) ? world.objects().at(1841).updates : 0;
    world.feed(packet);
    const auto found = world.objects().find(1841);
    if (found == world.objects().end() || found->second.updates == before) continue;
    ++updates;
    const obf2::Vec3f& at = found->second.position;
    if (std::abs(at.x + 149.53f) < 1.0f && std::abs(at.y - 162.14f) < 1.0f &&
        std::abs(at.z + 269.96f) < 1.0f) {
      ++onSpot;
    }
  }
  std::printf("  jeep 1841: updates %d, on its spot %d\n", updates, onSpot);
  CHECK(updates >= 10);
  CHECK_EQ(onSpot, updates);

  // The classes come from the full records' masks.
  CHECK(world.objects().at(1841).netClass == obf2::net::bf2::GhostClass::SimpleObject);
  CHECK(world.objects().count(1731) == 1);
  const auto& soldier = world.objects().at(1731);
  CHECK(soldier.netClass == obf2::net::bf2::GhostClass::Soldier);
  // His soldier's full record puts it by the spot its `CreateObjectEvent` named
  // in this capture (-151.03, 162.30, -271.28): -151.01, 162.30, -271.18.
  CHECK(soldier.fromGhostStream);
  CHECK(std::abs(soldier.position.x + 151.03f) < 0.2f);
  CHECK(std::abs(soldier.position.y - 162.30f) < 0.2f);
  CHECK(std::abs(soldier.position.z + 271.28f) < 0.2f);
}

// A player's own networkable (docs/functions/player-state.md): the records whose
// network id `CreatePlayerEvent` named are read with `Player::setNetUpdate`'s
// layout (0x500570), and a layout that is right ends every record exactly at its
// length. Checked on every capture from the original server we keep.
void testPlayerRecordsReadToTheirLength() {
  int total = 0;
  for (const char* name : {"bf2-spawned.bin", "bf2-karkand-lives.bin", "bf2-karkand-other-player.bin",
                           "bf2-ghosts.bin"}) {
    obf2::net::bf2::WorldView world;
    for (const auto& packet : loadCapture(std::string(OBF2_TEST_DATA) + "/" + name)) {
      world.feed(packet);
    }
    std::printf("  %s: player records %d, exact %d\n", name, world.playerRecords(),
                world.playerRecordsExact());
    CHECK_EQ(world.playerRecords(), world.playerRecordsExact());
    total += world.playerRecords();
    // The spawn groups' ghosts too (`SpawnGroup::setNetUpdate`, Linux 0x4b9ab0).
    std::printf("  %s: spawn group records %d, exact %d\n", name, world.spawnGroupRecords(),
                world.spawnGroupRecordsExact());
    CHECK_EQ(world.spawnGroupRecords(), world.spawnGroupRecordsExact());
    // A player whose score came has score = 2 x kills + teamwork, BF2's own
    // arithmetic — a check on the score block's order that owes nothing to us.
    for (const auto& [id, player] : world.players()) {
      if (!player.score) continue;
      CHECK_EQ(player.score->score, 2 * player.score->kills + player.score->teamwork);
    }
  }
  CHECK(total > 0);
}

// A soldier's weapon index (0x1000) is as wide as his template's inventory size
// (soldier_state.h, `BF2.exe` 0x62dbcc), and every soldier of the game has
// `ObjectTemplate.inventorySize 10` (`objects/soldiers/*/*.tweak`). Fields follow
// it, so a wrong width shows as records that do not end at their length: given 2
// (three bits), every one of the 8 records here that carry the field fails. What it cannot
// tell apart is the sizes that give the same four bits, 6 to 13.
void testSoldierWeaponIndexWidth() {
  int complete = 0, withWeapon = 0, weaponRecords = 0;
  for (const char* name : {"bf2-spawned.bin", "bf2-karkand-lives.bin", "bf2-karkand-other-player.bin",
                           "bf2-ghosts.bin"}) {
    obf2::net::bf2::WorldView world;
    world.setInventorySize([](std::uint32_t) { return 10; });
    for (const auto& packet : loadCapture(std::string(OBF2_TEST_DATA) + "/" + name)) {
      world.feed(packet);
    }
    std::printf("  %s: soldier records read to the end %d, exact %d, with a weapon %d\n", name,
                world.soldierRecordsComplete(), world.soldierRecordsExact(),
                world.soldierRecordsWithWeapon());
    CHECK_EQ(world.soldierRecordsComplete(), world.soldierRecordsExact());
    complete += world.soldierRecordsComplete();
    weaponRecords += world.soldierRecordsWithWeapon();
    for (const auto& [id, object] : world.objects()) {
      if (object.lastWeaponIndex < 0) continue;
      ++withWeapon;
      // An `itemIndex` a kit carries: 1 to 9 in the game's kits.
      CHECK(object.lastWeaponIndex >= 1 && object.lastWeaponIndex <= 9);
    }
  }
  std::printf("  soldiers with a weapon read: %d\n", withWeapon);
  CHECK(complete > 0);
  CHECK(weaponRecords >= 8);
  CHECK(withWeapon > 0);
}

// `tests/data/bf2-ragdoll.bin`: 2400 frames on the live server (Strike at
// Karkand, bots), three soldiers dying in it. A dead soldier's record is the
// ragdoll's state and nothing after it (soldier_state.h, 0x62d6da): a 9-bit
// length, the first particle raw, the rest wide vectors from it. Read right,
// every one of those records ends exactly at its length, and its particles are a
// body — close together, which a misread raw float or level would not be.
void testRagdollRecordsReadWhole() {
  obf2::net::bf2::WorldView world;
  world.setInventorySize([](std::uint32_t) { return 10; });
  for (const auto& packet : loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-ragdoll.bin")) {
    world.feed(packet);
  }
  int ragdolls = 0, exact = 0, bodies = 0;
  for (const auto& [id, object] : world.objects()) {
    ragdolls += object.ragdollRecords;
    exact += object.ragdollExact;
    if (object.ragdollParticles.empty()) continue;
    ++bodies;
    for (const obf2::Vec3f& particle : object.ragdollParticles) {
      CHECK(obf2::length(particle - object.ragdollParticles.front()) < 1.0f);
    }
  }
  std::printf("  ragdoll records %d, exact %d, bodies lying at the end %d\n", ragdolls, exact,
              bodies);
  CHECK(ragdolls > 0);
  CHECK_EQ(exact, ragdolls);
  CHECK(bodies > 0);
  // And with that, every soldier record of the run is read to its end.
  CHECK_EQ(world.soldierRecordsComplete(), world.soldierRecordsExact());
}

static void testUnitSpotVisibilityAndExpiry() {
  using namespace obf2::net::bf2;
  WorldView teamOne;
  WorldView teamTwo;
  teamOne.setOwnConnection(1);
  teamTwo.setOwnConnection(2);
  Link server;
  for (unsigned team : {1u, 2u}) {
    EventBits player;
    player.put(5, kEventTypeBits).put(team, 3).put(0, 4).put(0, 1)
        .put(team, 8).put(100 + team, 16).put(0, 16).put(0, 1);
    for (int i = 0; i < 32; ++i) player.put(0, 8);
    server.queue(player);
  }
  const auto start = WorldView::Clock::time_point{};
  const auto roster = writeDataPacket(0, server, nullptr);
  teamOne.feed(roster, start);
  teamTwo.feed(roster, start);

  EventBits spot;
  spot.put(kPostRemoteEvent, kEventTypeBits).put(2, 4).put(43, 32).put(0, 32).put(24, 8);
  for (std::uint32_t value : {0x00070000u, 1u, std::bit_cast<std::uint32_t>(100.0f),
                             std::bit_cast<std::uint32_t>(-50.0f), 0u, 43u}) {
    for (unsigned i = 0; i < 4; ++i) spot.put((value >> (8 * i)) & 0xff, 8);
  }
  server.queue(spot);
  const auto broadcast = writeDataPacket(0, server, nullptr);
  teamOne.feed(broadcast, start);
  teamTwo.feed(broadcast, start);
  const auto visible = teamOne.visibleSpots(start + std::chrono::seconds(11));
  CHECK_EQ(visible.size(), std::size_t(1));
  if (!visible.empty()) {
    CHECK_EQ(visible.front().target, 7);
    CHECK_EQ(visible.front().x, 100.0f);
    CHECK_EQ(visible.front().z, -50.0f);
  }
  CHECK(teamTwo.visibleSpots(start).empty());
  CHECK(teamOne.visibleSpots(start + std::chrono::seconds(12)).empty());
  teamOne.clearSpots();
  CHECK(teamOne.visibleSpots(start).empty());
}

static void testOccupancyDoesNotInventAnObjectAtTheOrigin() {
  using namespace obf2::net::bf2;
  WorldView world;
  Link server;
  EventBits enter;
  enter.put(9, kEventTypeBits).put(77, 8).put(123, 16).put(0, 1);
  server.queue(enter);
  world.feed(writeDataPacket(0, server, nullptr));
  CHECK(world.players().at(77).object == 123);
  CHECK(world.ownerOf(123) != nullptr);
  CHECK(!world.objects().contains(123));

  EventBits created;
  created.put(6, kEventTypeBits).put(42, 32).put(122, 16).put(0, 2)
      .put(1, 1).put(2, 8);
  server.queue(created);
  world.feed(writeDataPacket(0, server, nullptr));
  CHECK(world.objects().size() == 2);
  CHECK(world.objects().at(123).templateId == 42);
  CHECK(!world.objects().at(123).hasCreationPosition);
}

static void testDestroyObjectClearsTheReplicatedObject() {
  using namespace obf2::net::bf2;
  WorldView world;
  const auto apply = [&](const EventBits& event) {
    Link server;
    server.queue(event);
    world.feed(writeDataPacket(0, server, nullptr));
  };
  EventBits create;
  create.put(6, kEventTypeBits).put(42, 32).put(123, 16).put(0, 2).put(1, 1).put(1, 8);
  apply(create);
  CHECK(world.objects().contains(123));

  EventBits destroy;
  destroy.put(7, kEventTypeBits).put(123, 16);
  apply(destroy);
  CHECK(world.explicitlyDestroyed(123));
  CHECK(!world.objects().contains(123));

  apply(create);
  CHECK(!world.explicitlyDestroyed(123));
  CHECK(world.objects().contains(123));
}

static void testDroppedKitCanBePickedUpAgain() {
  using namespace obf2::net::bf2;
  WorldView world;
  const auto apply = [&](const EventBits& bits) {
    Link server;
    server.queue(bits);
    world.feed(writeDataPacket(0, server, nullptr));
  };
  const auto create = [&](int id, int definition) {
    EventBits kit;
    kit.put(31, kEventTypeBits).put(definition, 32).put(id, 16)
        .put(0, 32).put(0, 32).put(0, 32).put(0, 4).put(0, 4);
    apply(kit);
  };
  const auto pickup = [&](int kit, int soldier) {
    EventBits event;
    event.put(14, kEventTypeBits).put(5, 8).put(kit, 16).put(soldier, 16);
    apply(event);
  };

  create(123, 42);
  pickup(123, 200);
  CHECK_EQ(world.carriedKit(200), 123u);
  CHECK_EQ(world.kitTemplateOf(200), 42u);
  CHECK(!world.droppedKits().contains(123));

  // The retail swap can replace one instance with another of the same template.
  // A later drop of the old instance must keep the new one assigned.
  create(124, 42);
  pickup(124, 200);
  CHECK_EQ(world.carriedKit(200), 124u);
  CHECK(!world.droppedKits().contains(124));
  EventBits drop;
  drop.put(13, kEventTypeBits).put(5, 8).put(123, 16).put(200, 16)
      .put(std::bit_cast<std::uint32_t>(2.0f), 32)
      .put(std::bit_cast<std::uint32_t>(3.0f), 32)
      .put(std::bit_cast<std::uint32_t>(4.0f), 32);
  apply(drop);
  CHECK_EQ(world.carriedKit(200), 124u);
  CHECK_EQ(world.kitTemplateOf(200), 42u);
  CHECK(world.droppedKits().contains(123));
  CHECK_EQ(world.droppedKits().at(123).position.y, 3.0f);

  pickup(123, 201);
  CHECK_EQ(world.carriedKit(201), 123u);
  CHECK_EQ(world.kitTemplateOf(201), 42u);
  CHECK(!world.droppedKits().contains(123));

  create(125, 44);
  EventBits destroy;
  destroy.put(7, kEventTypeBits).put(125, 16);
  apply(destroy);
  CHECK(!world.droppedKits().contains(125));
  create(126, 44);
  apply(postRemoteEvent(kGameLogicCategory, kGameLogicReset));
  CHECK(world.droppedKits().empty());
  CHECK_EQ(world.kitTemplateOf(201), 0u);
}

static void testRevivedSoldierPoseBeforeGhostUpdate() {
  using namespace obf2::net::bf2;
  WorldView world;
  Event create;
  create.object = CreateObject{.templateId = 42, .networkId = 123, .position = obf2::Vec3f{1, 2, 3}};
  world.feedEvents({create});

  Event revive;
  revive.remote.emplace();
  revive.remote->category = kGameLogicCategory;
  revive.remote->number = kGameLogicRevived;
  revive.remote->data = {123, 0};
  world.feedEvents({revive});
  const auto pose = world.poseOf(123, 0.0f);
  CHECK(pose.has_value());
  if (pose) {
    CHECK_EQ(pose->stance, kRevivedSoldierPose);
    CHECK_EQ(pose->position.y, 2.0f);
  }
}

static void testProjectileDetonationQueuesOnce() {
  using namespace obf2::net::bf2;
  using obf2::net::BitWriter;
  WorldView world;
  world.setTemplateNet([](std::uint32_t) -> std::optional<WorldView::TemplateNet> {
    return WorldView::TemplateNet{.projectile = true};
  });
  Event create;
  create.object = CreateObject{.templateId = 42, .networkId = 123, .position = obf2::Vec3f{1, 2, 3}};
  world.feedEvents({create});

  std::vector<std::byte> payload(64);
  BitWriter body(payload);
  body.writeBits(kProjectileDetonation, 5);
  body.writeBits(1, 1);
  body.writeBits(0, 16);
  body.writeHighCompressedVector({}, {}, 0.01f);
  body.writeBits(0, 1);

  std::vector<std::byte> packet(128);
  BitWriter writer(packet);
  writer.writeBits(static_cast<unsigned>(PacketKind::Data), 4);
  for (const auto width : {8u, 6u, 6u, 32u, kStreamFramingBits}) writer.writeBits(0, width);
  writer.writeBits(0, 1);  // actions
  writer.writeBits(0, 1);  // events
  writer.writeBits(1, 1);  // ghosts
  writer.writeBits(10, 32);
  writer.writeBits(1, 8);  // records
  writer.writeBits(0, 1);  // controlled state
  writer.writeBits(1, 2);  // update
  writer.writeBits(123, 16);
  writer.writeBits(0, 1);  // baseline
  writer.writeBits(static_cast<std::uint32_t>(body.bitPosition()), kGhostLengthBits);
  obf2::net::BitReader reader(payload);
  for (std::size_t i = 0; i < body.bitPosition(); ++i) writer.writeBits(*reader.readBits(1), 1);
  packet.resize(writer.byteSize());
  world.feedGhosts(packet);
  CHECK_EQ(world.detonations().size(), std::size_t{1});
  CHECK_EQ(world.detonations().front().position.y, 2.0f);
  world.detonations().clear();
  world.feedGhosts(packet);
  CHECK(world.detonations().empty());
  CHECK_EQ(world.detonationCount(), 1);
}

static void testControlOwnershipFollowsSeatChanges() {
  using namespace obf2::net::bf2;
  WorldView world;
  Event driver, passenger, switchSeat, exit, takePassengerSeat;
  driver.enter = EnterVehicle{1, 500, false};
  passenger.enter = EnterVehicle{2, 501, false};
  world.feedEvents({driver, passenger});
  CHECK_EQ(world.players().at(1).object, 500);
  CHECK_EQ(world.players().at(2).object, 501);

  switchSeat.enter = EnterVehicle{2, 502, false};
  world.feedEvents({switchSeat});
  CHECK(world.ownerOf(501) == nullptr);
  CHECK(world.ownerOf(500) == &world.players().at(1));
  CHECK(world.ownerOf(502) == &world.players().at(2));

  takePassengerSeat.enter = EnterVehicle{3, 502, false};
  exit.exitPlayer = 2;
  world.feedEvents({takePassengerSeat, exit});
  CHECK_EQ(world.players().at(2).object, 0);
  CHECK(world.ownerOf(502) == &world.players().at(3));
}

static void testInfantryOwnershipSurvivesVehicleEntry() {
  using namespace obf2::net::bf2;
  WorldView world;
  world.setTemplateNet([](std::uint32_t id) -> std::optional<WorldView::TemplateNet> {
    return WorldView::TemplateNet{.soldier = id == 1};
  });
  Event soldier, infantry, exit, driver, gunner, destroy;
  soldier.object = CreateObject{.templateId = 1, .networkId = 100};
  infantry.enter = EnterVehicle{1, 100, false};
  exit.exitPlayer = 1;
  driver.enter = EnterVehicle{1, 200, false};
  gunner.enter = EnterVehicle{1, 201, false};
  destroy.destroyObject = 100;
  world.feedEvents({infantry, soldier, exit, driver});
  CHECK(world.ownerOf(100) == nullptr);
  CHECK(world.infantryOwnerOf(100) == &world.players().at(1));
  CHECK_EQ(world.players().at(1).object, 200);
  world.feedEvents({exit, gunner, exit, infantry});
  CHECK(world.infantryOwnerOf(100) == world.ownerOf(100));
  CHECK(world.ownerOf(200) == nullptr);
  CHECK(world.ownerOf(201) == nullptr);
  world.feedEvents({destroy});
  CHECK(world.infantryOwnerOf(100) == nullptr);
}

TEST_MAIN({
  testInfantryOwnershipSurvivesVehicleEntry();
  testControlOwnershipFollowsSeatChanges();
  testProjectileDetonationQueuesOnce();
  testDroppedKitCanBePickedUpAgain();
  testRevivedSoldierPoseBeforeGhostUpdate();
  testDestroyObjectClearsTheReplicatedObject();
  testOccupancyDoesNotInventAnObjectAtTheOrigin();
  testUnitSpotVisibilityAndExpiry();
  testRagdollRecordsReadWhole();
  testSoldierWeaponIndexWidth();
  testPlayerRecordsReadToTheirLength();
  testWorldViewCollectsPlayersAndObjects();
  testWorldViewTakesCompressionReference();
  testOtherPlayersObjectsFromTheirRecords();
});
