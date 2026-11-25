// The session module's own helpers (`obf2/session/remote_world.h`).
//
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "check.h"
#include "obf2/session/remote_world.h"

using namespace obf2;

// The tolerance is deliberately narrow: both sides take a control point's
// position from the same data, so a match has to be all but exact, and a wider
// one would start inventing correspondences.
static void testNearestKnownMatchesOnlyWhatIsClose() {
  std::vector<session::KnownObject> known;
  known.push_back({"gasstation", Vec3f{-161.0f, 157.0f, -263.0f}});
  known.push_back({"market", Vec3f{-196.0f, 150.0f, 183.0f}});

  const auto* hit = session::nearestKnown(known, Vec3f{-161.4f, 157.0f, -263.2f});
  CHECK(hit != nullptr);
  if (hit != nullptr) CHECK_EQ(hit->name, std::string("gasstation"));

  // Three metres away is past the two the tolerance allows.
  CHECK(session::nearestKnown(known, Vec3f{-164.0f, 157.0f, -263.0f}) == nullptr);
  // And with nothing to match against there is no answer rather than a crash.
  CHECK(session::nearestKnown({}, Vec3f{}) == nullptr);
}

// Every stage has a name, because the report prints it and an unnamed one would
// read as a question mark next to an object that does not appear.
static void testEveryDrawStageIsNamed() {
  const session::DrawStage all[] = {
      session::DrawStage::Drawn,          session::DrawStage::NoTemplate,
      session::DrawStage::NoTree,         session::DrawStage::NoGeometryName,
      session::DrawStage::GeometryInChild, session::DrawStage::NoGeometryFile,
      session::DrawStage::NoMesh};
  for (const session::DrawStage stage : all) {
    const std::string_view name = session::drawStageName(stage);
    CHECK(!name.empty());
    CHECK(name != "?");
  }
}

// The settings the session takes are its own, not the application's `Args`: the
// defaults have to be the ones a plain connection wants.
static void testSettingsDefaults() {
  const session::Settings settings;
  CHECK(settings.connectTo.empty());
  CHECK(settings.playerName.empty());
  // -1 means "take the ordinal the server's challenge names" (bf2_protocol.h).
  CHECK_EQ(settings.ordinal, -1);
  CHECK(!settings.blockReady);
  CHECK(!settings.skipContent);
  CHECK(!settings.skipDatabase);
  CHECK(!settings.startSimulation);
}

// A render frame can outlast the tail of a hold. The action clock must be
// able to sample and send the release before the next camera update.
static void testInfantryReleaseDuringRenderFrame() {
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.ourSoldier = 1;
  world.playerSpawned = true;
  net::bf2::PlayerAction forward;
  forward.axes[net::bf2::kAxisThrottle] = net::bf2::throttleToWire(1.0f, true);

  for (unsigned tick = 0; tick < 28; ++tick) {
    world.queueInput(0.0f, 0.0f, forward);
    world.advanceActions(server::kTickTime);
  }
  world.queueInput(0.0f, 0.0f, {});
  world.advanceActions(server::kTickTime);

  CHECK_EQ(world.actionTick, 29u);
  CHECK_EQ(world.sent.size(), std::size_t{29});
  for (const auto& [number, action] : world.sent) {
    const auto throttle = action.axes[net::bf2::kAxisThrottle];
    CHECK_EQ(throttle, number < 28 ? 100 : 0);
  }
}

static void testCameraUsesLatestTickPairAfterASlowFrame() {
  using namespace obf2;
  FileSystem files;
  level::Level terrain;
  session::RemoteWorld world({}, files);
  world.terrain = &terrain;
  world.ourSoldier = 1;
  world.playerSpawned = true;
  world.correct({0.0f, 50.0f, 0.0f});
  world.body.velocity = {6.0f, 0.0f, 0.0f};
  world.tickAllowance = 3;
  world.tick.pending = 0.5f * server::kTickTime;

  world.predict(0.1f);
  const Vec3f penultimate = world.bodyAt[1].body.position;
  const Vec3f latest = world.body.position;
  CHECK(latest.x > penultimate.x);
  CHECK(std::abs(world.drawnSoldierPosition()->x - (penultimate.x + latest.x) * 0.5f) < 0.001f);

  // Once the remaining half-tick passes, the next pair begins at that same
  // endpoint. Catching up three ticks must not leave two extra ticks of lag.
  world.predict(0.5f * server::kTickTime + 0.0001f);
  CHECK(std::abs(world.drawnSoldierPosition()->x - latest.x) < 0.002f);
}

static void testSparseCorrectionReplaysFromAcknowledgedLook() {
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.ourSoldier = 1;
  world.correct({});
  world.physics.lookFactorX = 5.0f;
  net::bf2::PlayerAction turn;
  turn.axes[net::bf2::kAxisMouseX] = 100;

  for (std::uint32_t tick = 0; tick < 3; ++tick) {
    world.sent.push_back({tick, turn});
    world.playAction(tick, turn);
  }
  const auto predictedYaw = world.yaw();
  net::bf2::SoldierState state;
  state.position = Vec3f{};
  world.reconcile(state, 1);
  CHECK_EQ(world.sent.size(), std::size_t{1});
  CHECK(std::abs(world.yaw() - predictedYaw) < 0.001f);

  state.position.reset();
  state.bodyYaw = 0.0f;
  state.aimYaw = 8.0f;
  state.pitch = 3.0f;
  world.reconcile(state, 1);
  CHECK_EQ(world.sent.size(), std::size_t{1});
  CHECK(std::abs(world.yaw() - 13.0f) < 0.001f);
  CHECK_EQ(world.pitch(), -3.0f);
  world.reconcile({}, 2);
  CHECK(world.sent.empty());
}

static void testDismountWaitsForTheControlledSoldierPosition() {
  using namespace obf2;
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.playerSpawned = true;
  world.ourSoldier = 234;
  world.placeSoldier({10, 10, 10});
  world.bodyReady = false;
  world.vehicleDismountPending = true;

  net::bf2::ControlObjectState control;
  control.networkId = 234;
  control.counter = 8;
  control.soldier.emplace();
  world.resumeDismountedSoldier(control);
  CHECK(!world.soldierPosition());

  control.soldier->position = Vec3f{40, 30, 20};
  control.soldier->bodyYaw = 90;
  control.networkId = 235;
  world.resumeDismountedSoldier(control);
  CHECK(!world.soldierPosition());

  control.networkId = 234;
  world.resumeDismountedSoldier(control);
  CHECK(world.bodyReady);
  CHECK_EQ(world.ourSoldier, 234);
  CHECK_EQ(world.body.position.x, 40);
  CHECK_EQ(world.body.position.z, 20);
  CHECK_EQ(world.yaw(), 90);
}

static void testReviveResumesTheLocalSoldier() {
  namespace bf2 = net::bf2;
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.infantryObject = 0x234;
  world.physics.pivotHeight = 1.0f;
  world.body.position = {1, 2, 3};

  bf2::RemoteEvent event;
  event.category = bf2::kGameLogicCategory;
  event.number = bf2::kGameLogicRevived;
  event.data = {0x34};
  CHECK(!bf2::readRevivedSoldier(event));
  event.data.push_back(0x02);
  const auto revived = bf2::readRevivedSoldier(event);
  CHECK_EQ(revived.value_or(0), 0x234);

  world.resumeRevivedSoldier(0x235, std::nullopt);
  CHECK(!world.playerSpawned);
  bf2::ControlObjectState control;
  control.networkId = 0x234;
  control.soldier = bf2::SoldierState{};
  control.soldier->position = Vec3f{9, 8, 7};
  world.resumeRevivedSoldier(*revived, control);
  CHECK(world.playerSpawned);
  CHECK(world.bodyReady);
  CHECK_EQ(world.ourSoldier, 0x234);
  CHECK_EQ(world.body.position.x, 9.0f);
  CHECK_EQ(world.body.position.y, 7.0f);
  CHECK_EQ(world.body.pose, server::kPoseProne);
  CHECK_EQ(world.body.requestedPose, server::kPoseProne);

  world.playerSpawned = false;
  control.soldier->ragdoll = true;
  control.soldier->position = Vec3f{10, 10, 10};
  control.soldier->pose = server::kPoseStand;
  world.resumeRevivedSoldier(*revived, control);
  CHECK_EQ(world.body.position.x, 9.0f);
  CHECK_EQ(world.body.pose, server::kPoseProne);

  world.playerSpawned = false;
  control.soldier->ragdoll = false;
  control.soldier->position.reset();
  control.soldier->pose = server::kPoseCrouch;
  world.resumeRevivedSoldier(*revived, control);
  CHECK_EQ(world.body.pose, server::kPoseCrouch);
  CHECK_EQ(world.body.requestedPose, server::kPoseCrouch);

  world.actionTick = 90;
  world.resumeRevivedSoldier(*revived, std::nullopt);
  CHECK_EQ(world.body.pose, server::kPoseProne);
  CHECK_EQ(world.body.position.x, 9.0f);
  CHECK_EQ(world.actionTick, 90u);
}

static void testVehiclePositionSelectPressIsOneTick() {
  namespace bf2 = net::bf2;
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.registry.feed({{"ObjectTemplate", "create"}, "objecttemplate.create",
                       {"PlayerControlObject", "jeep"}, {}, 0});
  world.templateNumbers.add("jeep");
  bf2::Event create;
  create.object.emplace();
  create.object->templateId = *world.templateNumbers.numberOf("jeep");
  create.object->networkId = 101;
  world.world.feedEvents({create});
  world.playerSpawned = true;
  world.ourSoldier = 100;

  bf2::PlayerAction input;
  input.buttons = bf2::kButtonPositionSelect(3);
  CHECK_EQ(input.buttons, 1u << 17);
  world.queueInput(0, 0, input);
  CHECK_EQ(world.pendingPresses, 0u);
  world.ourSoldier = 101;
  world.queueInput(0, 0, input);
  CHECK_EQ(world.pendingPresses, 0u);

  world.queueInput(0, 0, {});
  world.queueInput(0, 0, input);
  world.queueInput(0, 0, {});
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, input.buttons);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, 0u);

  world.queueInput(0, 0, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, input.buttons);
  world.queueInput(0, 0, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, 0u);

  world.queueInput(0, 0, {});
  world.queueInput(0, 0, input);
  world.ourSoldier = 100;
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, 0u);
}

static void testVehicleUsePressIsOneTick() {
  FileSystem files;
  session::RemoteWorld world({}, files);
  net::bf2::PlayerAction action;
  action.buttons = net::bf2::kButtonUse;
  world.queueInput(0, 0, action);
  world.queueInput(0, 0, action);
  CHECK_EQ(world.pendingPresses, net::bf2::kButtonUse);
  CHECK_EQ(world.pendingKeys.buttons, 0u);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, net::bf2::kButtonUse);
  world.queueInput(0, 0, action);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, 0u);
  world.queueInput(0, 0, {});
  world.queueInput(0, 0, action);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, net::bf2::kButtonUse);
}

static void testPilotActionsAreSeatSpecific() {
  namespace bf2 = net::bf2;
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.registry.feed({{"ObjectTemplate", "create"}, "objecttemplate.create",
                       {"PlayerControlObject", "jet"}, {}, 0});
  world.registry.feed({{"ObjectTemplate", "vehicleCategory"}, "objecttemplate.vehicleCategory",
                       {"VCAir"}, {}, 0});
  world.templateNumbers.add("jet");
  bf2::Event create;
  create.object.emplace();
  create.object->templateId = *world.templateNumbers.numberOf("jet");
  create.object->networkId = 101;
  world.world.feedEvents({create});
  world.playerSpawned = true;
  world.ourSoldier = 101;
  CHECK(world.pilotControls() == session::RemoteWorld::PilotControls::Jet);

  world.queueInput(0.25f, -0.5f, {});
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.axes[bf2::kAxisRoll], 25);
  CHECK_EQ(world.action.axes[bf2::kAxisPitch], 50);

  bf2::PlayerAction input;
  input.buttons = bf2::kButtonCrouch;
  world.queueInput(0.25f, -0.5f, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.axes[bf2::kAxisRoll], 0);
  CHECK_EQ(world.action.axes[bf2::kAxisPitch], 0);
  CHECK_EQ(world.action.buttons, bf2::kButtonMouseLook);

  world.ourSoldier = 100;
  world.queueInput(0.25f, -0.5f, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.axes[bf2::kAxisRoll], 0);
  CHECK_EQ(world.action.buttons, bf2::kButtonCrouch);
}

static void testPredictedDeathPoseDoesNotMoveTheAircraftWreck() {
  namespace bf2 = net::bf2;
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.registry.feed({{"ObjectTemplate", "create"}, "objecttemplate.create",
                       {"PlayerControlObject", "jet"}, {}, 0});
  world.templateNumbers.add("jet");
  bf2::Event create;
  create.object.emplace();
  create.object->templateId = *world.templateNumbers.numberOf("jet");
  create.object->networkId = 101;
  create.object->position = Vec3f{12, 0, 80};
  world.world.feedEvents({create});
  bf2::SimpleObjectState crash;
  crash.position = Vec3f{384, 14, 259};
  world.world.feedControlledVehicle(101, 200, crash);

  // The death arrives before the exit event clears the aircraft's controlled ID.
  world.ourSoldier = 101;
  world.playerSpawned = false;
  world.body.position = *create.object->position;
  CHECK(!world.usesLocalDeathPose(101));
  const auto wreck = world.world.poseAtTime(101, 200 * bf2::kGhostTickMs);
  CHECK(wreck && wreck->position.x == 384 && wreck->position.z == 259);

  world.ourSoldier = 102;
  CHECK(world.usesLocalDeathPose(102));
  world.playerSpawned = true;
  CHECK(!world.usesLocalDeathPose(102));
}

static void testSurfaceVehicleHullUsesServerReference() {
  namespace bf2 = net::bf2;
  for (const auto* name : {"boat_rib", "usjep_hmmwv"}) {
    FileSystem files;
    session::RemoteWorld match({}, files);
    match.registry.feed({{"ObjectTemplate", "create"}, "objecttemplate.create",
                         {"PlayerControlObject", name}, {}, 0});
    match.templateNumbers.add(name);
    bf2::Event create;
    create.object.emplace();
    create.object->templateId = *match.templateNumbers.numberOf(name);
    create.object->networkId = 101;
    match.world.feedEvents({create});
    match.playerSpawned = true;
    match.ourSoldier = match.controlObject = 101;

    std::array<std::byte, 32> packet{};
    net::BitWriter writer(packet);
    writer.writeBits(0, 1);
    writer.writeBits(bf2::kObjectStatePosition, bf2::kObjectStateMaskBits);
    writer.writeCompressedVector({}, {}, bf2::kObjectPositionPrecision);
    bf2::ControlObjectState update;
    update.networkId = 101;
    update.stateStart = 1;
    update.stateEnd = writer.bitPosition();

    match.updateControlledVehicle(packet, update, 100);
    update.delta = true;
    update.compressionReference.x = 4.2f;
    match.updateControlledVehicle(packet, update, 103);
    CHECK_EQ(match.world.objects().at(101).position.x, 4.2f);

    update.baselineSlot = 1;  // The delta's baseline has yet to arrive.
    update.compressionReference.x = 5.6f;
    match.updateControlledVehicle(packet, update, 104);
    CHECK_EQ(match.world.objects().at(101).position.x, 5.6f);
    CHECK(match.controlledHullTrack.newest() &&
          match.controlledHullTrack.newest()->position.x == 5.6f);

    update.delta = false;
    update.compressionReference.x = 4.2f;
    match.updateControlledVehicle(packet, update, 103);
    CHECK(match.controlledVehicleBaselines[1]);
    CHECK_EQ(match.world.objects().at(101).position.x, 5.6f);

    update.delta = true;
    update.compressionReference.x = 5000.0f;
    match.updateControlledVehicle(packet, update, 105);
    CHECK_EQ(match.world.objects().at(101).position.x, 5.6f);
    CHECK(match.controlledHullTrack.newest() &&
          match.controlledHullTrack.newest()->position.x == 5.6f);
  }
}

static void testWeaponSelectPressIsOneTick() {
  namespace bf2 = net::bf2;
  FileSystem files;
  session::RemoteWorld world({}, files);
  bf2::PlayerAction input;
  input.buttons = bf2::kButtonWeaponSelect(3);
  world.queueInput(0, 0, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, input.buttons);
  world.queueInput(0, 0, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, 0u);
  world.queueInput(0, 0, {});
  world.queueInput(0, 0, input);
  world.queueInput(0, 0, {});
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, input.buttons);

  world.playerSpawned = true;
  world.ourSoldier = 100;
  world.queueInput(0, 0, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.sent.size(), std::size_t{2});
  if (world.sent.size() == 2) {
    CHECK_EQ(world.sent.front().second.buttons, input.buttons);
    CHECK_EQ(world.sent.back().second.buttons, 0u);
  }
  world.acknowledgeActions(8);
  CHECK_EQ(world.actionTick, 9u);
  CHECK(world.sent.empty());
}

static void testLocalCombatCuesUseDamageAndOneDeath() {
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.playerSpawned = true;
  net::bf2::ControlObjectState control;
  control.soldier.emplace();
  control.playerSync.emplace();
  control.playerSync->health = 1.0f;
  world.applySoldierSync(control);
  CHECK(!world.ownCombatFeedback.hit);

  control.playerSync->health = 0.6f;
  world.applySoldierSync(control);
  CHECK(world.ownCombatFeedback.hit);
  world.ownCombatFeedback.hit = false;
  world.applySoldierSync(control);
  CHECK(!world.ownCombatFeedback.hit);

  control.playerSync->health = 0.0f;
  world.applySoldierSync(control);
  CHECK(world.ownCombatFeedback.death);
  CHECK(!world.ownCombatFeedback.hit);
  world.ownCombatFeedback.death = false;
  world.announceOwnDeath();
  CHECK(!world.ownCombatFeedback.death);
  world.resetRound();
  CHECK(!world.ownCombatFeedback.down);
}

static void testKitPickupPressIsOneTick() {
  FileSystem files;
  session::RemoteWorld world({}, files);
  net::bf2::PlayerAction input;
  input.buttons = net::bf2::kButtonDrop;
  world.queueInput(0, 0, input);
  CHECK_EQ(world.pendingKeys.buttons, 0u);
  CHECK_EQ(world.pendingPresses, net::bf2::kButtonDrop);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, net::bf2::kButtonDrop);
  world.queueInput(0, 0, input);
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, 0u);
  world.queueInput(0, 0, {});
  world.queueInput(0, 0, input);
  world.queueInput(0, 0, {});
  world.predict(server::kTickTime);
  CHECK_EQ(world.action.buttons, net::bf2::kButtonDrop);
}

static void testRecoilIsReplayedWithoutChangingWireInput() {
  FileSystem files;
  session::RemoteWorld world({}, files);
  world.ourSoldier = 1;
  world.playerSpawned = true;
  world.correct({});
  world.look = {};
  world.actionContext.mouseSpeedMod = 0.5f;
  int samples = 0;
  world.recoilAt = [&](std::uint32_t) {
    ++samples;
    return game::RecoilDelta{0.25f, -0.5f};
  };
  world.predict(server::kTickTime);
  world.predict(server::kTickTime);
  CHECK_EQ(samples, 2);
  CHECK_EQ(world.yaw(), 0.5f);
  CHECK_EQ(world.pitch(), 1.0f);
  CHECK_EQ(world.sent.back().second.axes[net::bf2::kAxisMouseY], 0);

  net::bf2::SoldierState baseline;
  baseline.position = Vec3f{};
  baseline.bodyYaw = baseline.aimYaw = baseline.pitch = 0.0f;
  world.reconcile(baseline, 0);
  CHECK_EQ(samples, 2);
  CHECK_EQ(world.yaw(), 0.25f);
  CHECK_EQ(world.pitch(), 0.5f);
}

static void testConnectRetriesOnlyUntilServerReplies() {
#if !defined(_WIN32)
  namespace bf2 = net::bf2;
  const int peer = ::socket(AF_INET, SOCK_DGRAM, 0);
  CHECK(peer >= 0);
  if (peer < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t length = sizeof(address);
  if (::bind(peer, reinterpret_cast<const sockaddr*>(&address), length) != 0 ||
      ::getsockname(peer, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    CHECK(false);
    ::close(peer);
    return;
  }

  FileSystem files;
  session::Settings settings;
  settings.connectTo = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
  session::RemoteWorld match(std::move(settings), files);
  bool connected = false;
  std::thread client([&] { connected = match.connect(); });
  sockaddr_in first{}, second{};
  const auto receive = [&](sockaddr_in& source) {
    pollfd waiting{peer, POLLIN, 0};
    if (::poll(&waiting, 1, 3000) != 1) return std::vector<std::byte>{};
    std::vector<std::byte> packet(512);
    socklen_t sourceLength = sizeof(source);
    const auto size = ::recvfrom(peer, packet.data(), packet.size(), MSG_DONTWAIT,
                                 reinterpret_cast<sockaddr*>(&source), &sourceLength);
    packet.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
    return packet;
  };
  const auto initial = receive(first);
  const auto retry = receive(second);
  CHECK(!retry.empty() && retry == initial);
  CHECK_EQ(first.sin_port, second.sin_port);
  if (!retry.empty()) {
    const auto kicked = bf2::writeDisconnect(0, 21);
    CHECK_EQ(::sendto(peer, kicked.data(), kicked.size(), 0,
                      reinterpret_cast<const sockaddr*>(&second), sizeof(second)),
             static_cast<ssize_t>(kicked.size()));
  }
  client.join();
  CHECK(!connected);
  CHECK(match.serverDisconnected && !match.socket);
  CHECK_EQ(match.failure, "The server disconnected this client while waiting for connection "
                          "acceptance. (reason 21)");
  ::close(peer);
#endif
}

static void testRoundResetAndDisconnect() {
#if !defined(_WIN32)
  namespace bf2 = net::bf2;
  const int peer = ::socket(AF_INET, SOCK_DGRAM, 0);
  CHECK(peer >= 0);
  if (peer < 0) return;

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t length = sizeof(address);
  if (::bind(peer, reinterpret_cast<const sockaddr*>(&address), length) != 0 ||
      ::getsockname(peer, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    CHECK(false);
    ::close(peer);
    return;
  }

  for (const bool loading : {false, true}) {
    FileSystem files;
    session::RemoteWorld world({}, files);
    world.socket = net::UdpSocket::connect("127.0.0.1", ntohs(address.sin_port));
    CHECK(world.socket != nullptr);
    if (!world.socket) break;
    CHECK(world.socket->send(bf2::writeShortPacket(bf2::PacketKind::ConnectAcceptAck, 0)));

    sockaddr_in client{};
    socklen_t clientLength = sizeof(client);
    std::byte ack[2]{};
    pollfd waiting{peer, POLLIN, 0};
    const int ready = ::poll(&waiting, 1, 1000);
    CHECK_EQ(ready, 1);
    if (ready != 1) break;
    const auto received = ::recvfrom(peer, ack, sizeof(ack), MSG_DONTWAIT,
                                     reinterpret_cast<sockaddr*>(&client), &clientLength);
    CHECK_EQ(received, 2);
    if (received != 2) break;
    if (!loading) {
      // Exercise the round reset on the same connection as an active soldier.
      world.playerSpawned = true;
      world.ourSoldier = 42;
      world.bodyReady = true;
      bf2::Link server;
      bf2::EventBits end;
      end.put(48, bf2::kEventTypeBits).put(1, 32).put(2, 32).put(6, 32);
      server.queue(std::move(end));
      std::array<std::byte, 14> summary{};
      summary[8] = std::byte{30};
      server.queue(bf2::dataBlockHeaderEvent(bf2::kEndOfRoundInfoBlock, summary.size()));
      server.queue(bf2::dataBlockChunkEvent(summary));
      const auto results = bf2::writeDataPacket(0, server, nullptr);
      CHECK_EQ(::sendto(peer, results.data(), results.size(), 0,
                        reinterpret_cast<const sockaddr*>(&client), clientLength),
               static_cast<ssize_t>(results.size()));
      CHECK(world.pump(500));
      CHECK(world.endOfRound && world.endOfRound->winner == 2);
      CHECK(world.endOfRoundInfo && world.endOfRoundInfo->delaySeconds == 15);
      CHECK(world.playerSpawned);

      server.queue(bf2::postRemoteEvent(bf2::kGameLogicCategory, bf2::kGameLogicReset));
      server.queue(bf2::postRemoteEvent(bf2::kNetworkCategory, bf2::kNetReset, 42));
      const auto reset = bf2::writeDataPacket(0, server, nullptr);
      CHECK_EQ(::sendto(peer, reset.data(), reset.size(), 0,
                        reinterpret_cast<const sockaddr*>(&client), clientLength),
               static_cast<ssize_t>(reset.size()));

      CHECK(world.pump(500));
      CHECK_EQ(world.roundResets, 1);
      CHECK(!world.endOfRound);
      CHECK(!world.endOfRoundInfo);
      CHECK(!world.playerSpawned);
      CHECK_EQ(world.ourSoldier, 0);
      CHECK(!world.bodyReady);
      CHECK(world.socket != nullptr);
      world.pump(0);

      pollfd replyWaiting{peer, POLLIN, 0};
      CHECK_EQ(::poll(&replyWaiting, 1, 500), 1);
      std::byte reply[128]{};
      const auto replySize = ::recv(peer, reply, sizeof(reply), MSG_DONTWAIT);
      CHECK(replySize > 0);
      if (replySize > 0) {
        const auto events = bf2::readEvents(std::span(reply, static_cast<std::size_t>(replySize)));
        CHECK_EQ(events.size(), std::size_t(1));
        if (events.size() == 1) {
          CHECK(events[0].remote && events[0].remote->category == bf2::kNetworkCategory &&
                events[0].remote->number == bf2::kNetReset);
        }
      }

      // A duplicate packet must not reset a second time, and the same socket
      // must accept subsequent events after the reset.
      ::sendto(peer, reset.data(), reset.size(), 0,
               reinterpret_cast<const sockaddr*>(&client), clientLength);
      CHECK(world.pump(500));
      CHECK_EQ(world.roundResets, 1);
      server.queue(bf2::postRemoteEvent(bf2::kNetworkCategory, bf2::kNetPlayerSpawned, 0));
      const auto next = bf2::writeDataPacket(0, server, nullptr);
      ::sendto(peer, next.data(), next.size(), 0,
               reinterpret_cast<const sockaddr*>(&client), clientLength);
      CHECK(world.pump(500));
      CHECK(world.playerSpawned);

      // A confirmed spawn may precede the first action. The server still
      // needs our acknowledgement during that gap.
      world.ourSoldier = 42;
      world.answered = true;
      world.lastDataSentAt = {};
      world.pump(0);
      pollfd heartbeatWaiting{peer, POLLIN, 0};
      CHECK_EQ(::poll(&heartbeatWaiting, 1, 500), 1);
      const auto heartbeatSize = ::recv(peer, reply, sizeof(reply), MSG_DONTWAIT);
      CHECK(heartbeatSize > 0);
      if (heartbeatSize > 0) {
        const auto packet = bf2::readPacket(std::span(reply, static_cast<std::size_t>(heartbeatSize)));
        CHECK(packet && packet->kind == bf2::PacketKind::Data && packet->extended);
        if (packet && packet->extended) CHECK_EQ(packet->extended->ack, 3);
      }

      world.levelReady = true;
      world.databaseSentAt = std::chrono::steady_clock::now();
      auto* gameSocket = world.socket.get();
      // An empty MapInfo tells the client to unload before the next named map.
      constexpr std::array<std::byte, 15> unload{};
      server.queue(bf2::dataBlockHeaderEvent(bf2::kMapInfoNetBuffer, unload.size()));
      server.queue(bf2::dataBlockChunkEvent(unload));
      const auto transition = bf2::writeDataPacket(0, server, nullptr);
      CHECK_EQ(::sendto(peer, transition.data(), transition.size(), 0,
                        reinterpret_cast<const sockaddr*>(&client), clientLength),
               static_cast<ssize_t>(transition.size()));
      CHECK(world.pump(500));
      CHECK(world.levelChanged);
      CHECK(!world.levelReady);
      CHECK(!world.databaseSentAt);
      CHECK(!world.playerSpawned);
      CHECK(world.socket.get() == gameSocket);
    }

    const auto disconnect = bf2::writeShortPacket(bf2::PacketKind::Disconnect, 0);
    CHECK_EQ(::sendto(peer, disconnect.data(), disconnect.size(), 0,
                      reinterpret_cast<const sockaddr*>(&client), clientLength), 2);

    if (loading) {
      // Keep the loading poll nonblocking, but allow the datagram to reach it.
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      do {
        world.lastKeepAlive = {};
        world.keepAlive();
        if (world.serverDisconnected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (std::chrono::steady_clock::now() < deadline);
    } else {
      CHECK(!world.pump(500));
    }
    CHECK(world.serverDisconnected);
    CHECK(!world.socket);
    CHECK(!world.pump(0));
  }

  ::close(peer);
#endif
}

// Votes (`VoteEvent`) and the mute (`VoipPlayerMuteEvent` 38,
// docs/functions/voip-mute.md): what goes out, and what an answer changes.
static void testVotesAndMute() {
  using namespace net::bf2;
  FileSystem files;
  session::RemoteWorld match({}, files);
  match.socket = net::UdpSocket::connect("127.0.0.1", 14567);
  CHECK(match.socket);
  if (!match.socket) return;
  match.databaseSentAt = std::chrono::steady_clock::now();
  match.world.setOwnConnection(7);
  Event player, target, bot;
  player.player = CreatePlayer{2, 0, 7, 177, false, "Local"};
  target.player = CreatePlayer{2, 0, 8, 178, false, "Designated"};
  bot.player = CreatePlayer{2, 0, 9, 179, true, "Bot"};
  match.world.feedEvents({player, target, bot});

  CHECK_EQ(match.communicate("vote kick Designated", {}, {}), "Voting is disabled on this server.");
  match.serverMatchOptions = ServerMatchOptions{true, true, true, false, 0, 0, 0};
  CHECK_EQ(match.communicate("vote kick Local", {}, {}), "Choose an eligible player by ID or name.");
  CHECK_EQ(match.communicate("vote kick Bot", {}, {}), "Choose an eligible player by ID or name.");
  CHECK_EQ(match.communicate("vote kick Designated", {}, {}), "Vote requested.");
  auto events = readEvents(writeDataPacket(7, match.link, nullptr));
  CHECK(events.size() == 1 && events[0].vote && events[0].vote->item == 8 &&
        events[0].vote->type == VoteKick && events[0].vote->status == VoteIssue &&
        events[0].vote->caster == 7);

  match.receiveVote(Vote{.item = 0, .caster = 8, .status = VoteIssue, .type = VoteMap});
  CHECK_EQ(match.communicate("vote map no", {}, {}), "Ballot sent.");
  events = readEvents(writeDataPacket(7, match.link, nullptr));
  CHECK(events.size() == 1 && events[0].vote && events[0].vote->type == VoteMap &&
        events[0].vote->option == 1 && events[0].vote->status == VoteUpdate);
  match.receiveVote(Vote{.item = 0, .status = VotePassed, .type = VoteMap});
  CHECK(!match.mapVote);

  // The mute: ours only, kept by id and name, and sent as {flag, us, him}.
  CHECK(!match.toggleMute(7));
  CHECK(match.toggleMute(8));
  CHECK(match.muted(8));
  events = readEvents(writeDataPacket(7, match.link, nullptr));
  CHECK(events.size() == 1 && events[0].type == 38);
  CHECK(!match.toggleMute(8));
  CHECK(!match.muted(8));
}

// A commander order to our squad: accepting adopts it as the squad's order and
// answers the commander; declining answers alone.
static void testCommanderOrderAcceptAndDecline() {
  using namespace obf2::net::bf2;
  FileSystem files;
  session::RemoteWorld match({}, files);
  match.socket = net::UdpSocket::connect("127.0.0.1", 14567);
  CHECK(match.socket);
  if (!match.socket) return;
  match.databaseSentAt = std::chrono::steady_clock::now();
  match.world.setOwnConnection(7);
  Event player;
  player.player = CreatePlayer{2, 0, 7, 177, false, "Leader"};
  match.world.feedEvents({player});

  // The player's ghost: squad 3, its leader.
  std::array<std::byte, 64> packet{};
  net::BitWriter writer(packet);
  writer.writeBits(static_cast<unsigned>(PacketKind::Data), 4);
  for (unsigned width : {8u, 6u, 6u, 32u, kStreamFramingBits}) writer.writeBits(0, width);
  writer.writeBits(0, 1);  // actions
  writer.writeBits(0, 1);  // events
  writer.writeBits(1, 1);  // ghosts
  writer.writeBits(10, 32);
  writer.writeBits(1, 8);  // record count
  writer.writeBits(0, 1);  // controlled object
  writer.writeBits(1, 2);  // update
  writer.writeBits(177, 16);
  writer.writeBits(0, 1);  // baseline
  writer.writeBits(kPlayerMaskBits + 7, kGhostLengthBits);
  writer.writeBits(0x2000, kPlayerMaskBits);
  writer.writeBits(3, 5);  // squad
  writer.writeBits(1, 1);  // squad leader
  writer.writeBits(0, 1);
  match.world.feedGhosts(std::span(packet).first(writer.byteSize()));
  CHECK_EQ(match.world.players().at(7).squad.value_or(0), 3u);
  CHECK(match.world.players().at(7).squadLeader);

  Event event;
  event.squadOrder = SquadOrder{3, 2, 5, 0, {{{19, 0, -23}, {}, {}}}, OrderRoute::Commander, false};
  match.world.feedEvents({event});
  CHECK_EQ(match.world.ownOrders().size(), std::size_t{1});
  CHECK_EQ(match.communicate("accept", {}, {}), std::string("Order acceptance sent."));
  const auto accepted = readEvents(writeDataPacket(7, match.link, nullptr));
  CHECK_EQ(accepted.size(), std::size_t{3});
  if (accepted.size() == 3) {
    CHECK(accepted[0].squadOrder && accepted[0].squadOrder->route == OrderRoute::Squad);
    CHECK(accepted[0].squadOrder && accepted[0].squadOrder->accepted);
    CHECK_EQ(accepted[1].type, 50u);
    CHECK(accepted[2].orderResponse && !accepted[2].orderResponse->accepted);
  }

  match.world.feedEvents({event});
  CHECK_EQ(match.communicate("decline", {}, {}), std::string("Order declined."));
  const auto declined = readEvents(writeDataPacket(7, match.link, nullptr));
  CHECK_EQ(declined.size(), std::size_t{2});
  if (declined.size() == 2) {
    CHECK_EQ(declined[0].type, 50u);
    CHECK(declined[1].orderResponse && !declined[1].orderResponse->accepted);
  }
}

// A squad's support request approved by the commander waits for the server's
// dispatch (HUD event 83, the supply drop) before the squad is answered.
static void testCommanderSupportWaitsForServerDispatch() {
  using namespace net::bf2;
  FileSystem files;
  session::RemoteWorld match({}, files);
  match.commanderEnabled = true;
  match.world.setOwnConnection(7);
  Event player, assignment, order;
  player.player = CreatePlayer{2, 0, 7, 177, false, "Commander"};
  assignment.commanderAssignment = Event::CommanderAssignment{2, 7};
  order.squadOrder = SquadOrder{3, 2, 3, 0, {{{19, 0, -23}, {}, {}}}, OrderRoute::Request, true};
  match.world.feedEvents({player, assignment, order});
  CHECK(match.isCommander());
  CHECK_EQ(match.world.commanderRequests().size(), std::size_t{1});
  CHECK_EQ(match.commanderRespond(3, 3, true), "Asset requested. Waiting for the server to confirm dispatch.");
  CHECK(match.pendingSupport);
  CHECK(match.commanderEffects.empty());
  const auto dispatch = readEvents(writeDataPacket(7, match.link, nullptr));
  CHECK_EQ(dispatch.size(), std::size_t{1});
  if (!dispatch.empty()) CHECK_EQ(dispatch[0].type, 37u);

  RemoteEvent confirmed;
  confirmed.category = 2;
  confirmed.number = 83;
  for (const float value : {19.0f, 100.0f, -23.0f}) {
    const auto word = std::bit_cast<std::uint32_t>(value);
    for (unsigned shift = 0; shift < 32; shift += 8) confirmed.data.push_back(static_cast<std::uint8_t>(word >> shift));
  }
  match.receiveCommanderHud(confirmed);
  CHECK(!match.pendingSupport);
  CHECK_EQ(match.commanderEffects.size(), std::size_t{1});
  CHECK(!match.commanderAssetReady(CommanderTool::Supplies));
  const auto reply = readEvents(writeDataPacket(7, match.link, nullptr));
  CHECK(std::ranges::any_of(reply, [](const auto& event) {
    return event.orderResponse && event.orderResponse->route == OrderRoute::Request &&
           !event.orderResponse->accepted && event.orderResponse->id == 3;
  }));
  assignment.commanderAssignment->player = -1;
  match.world.feedEvents({assignment});
  CHECK(!match.isCommander());
}

TEST_MAIN({
  testVotesAndMute();
  testCommanderOrderAcceptAndDecline();
  testCommanderSupportWaitsForServerDispatch();
  testNearestKnownMatchesOnlyWhatIsClose();
  testEveryDrawStageIsNamed();
  testSettingsDefaults();
  testCameraUsesLatestTickPairAfterASlowFrame();
  testInfantryReleaseDuringRenderFrame();
  testSparseCorrectionReplaysFromAcknowledgedLook();
  testDismountWaitsForTheControlledSoldierPosition();
  testReviveResumesTheLocalSoldier();
  testVehiclePositionSelectPressIsOneTick();
  testVehicleUsePressIsOneTick();
  testPilotActionsAreSeatSpecific();
  testPredictedDeathPoseDoesNotMoveTheAircraftWreck();
  testSurfaceVehicleHullUsesServerReference();
  testWeaponSelectPressIsOneTick();
  testLocalCombatCuesUseDamageAndOneDeath();
  testKitPickupPressIsOneTick();
  testRecoilIsReplayedWithoutChangingWireInput();
  testRoundResetAndDisconnect();
  testConnectRetriesOnlyUntilServerReplies();
})
