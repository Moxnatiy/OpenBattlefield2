#include "obf2/session/remote_world.h"

#include <cctype>
#include <charconv>
#include <set>
#include <sstream>

#include "obf2/game/scene.h"

#include "obf2/core/parallel.h"
#include "obf2/core/path.h"
#include "obf2/level/gameplay.h"

namespace obf2::session {

namespace bf2 = net::bf2;

void RemoteWorld::loadRadioMessages() {
  if (radioLoaded) return;
  radioLoaded = true;
  const auto load = [this](const con::Command& command) {
    if (command.lowerPath == "gamelogic.messages.addhelpmessage" && command.args.size() >= 2) {
      std::string name = command.args[0];
      for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      helpVoices.emplace_back(std::move(name), command.args[1]);
    } else if (command.lowerPath == "gamelogic.messages.addmessage" && !command.args.empty()) {
      radioMessages.push_back({command.args[0], {}, {}, {}, 0});
    } else if (command.lowerPath == "gamelogic.messages.addradiovoice" && command.args.size() >= 3 &&
               !radioMessages.empty() && radioMessages.back().caption.empty()) {
      auto& message = radioMessages.back();
      message.caption = command.args[2];
      message.sample = command.args[1].substr(0, command.args[1].find(','));
      if (command.args.size() >= 4) {
        message.localSample = command.args[3].substr(0, command.args[3].find(','));
        message.localFlags = command.argInt(4).value_or(0);
      }
    }
  };
  con::Interpreter interpreter(files, load);
  interpreter.runFile("Common/Sound/VoiceMessages.con");
}

std::string RemoteWorld::helpVoicePath(std::string_view name, const std::string& language) {
  loadRadioMessages();
  std::string lower(name);
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const std::string probe = "Common/Sound/" + language + "/Help_VO/HUD_HELP_BATTLERECORDER_controlsQ.ogg";
  const std::string folder = files.exists(probe) ? language : std::string("English");
  for (const auto& [voice, sample] : helpVoices) {
    if (voice == lower) return "Common/Sound/" + folder + "/" + sample;
  }
  return {};
}

void RemoteWorld::receiveRadio(const bf2::RadioCall& call) {
  if (call.team < 1 || call.team > 2) return;
  loadRadioMessages();
  if (call.message >= radioMessages.size()) return;
  const auto& message = radioMessages[call.message];
  const bool friendly = call.team == static_cast<unsigned>(world.ownTeam());
  // A teammate's radio message reaches 0x4441d0, which hands it on as channel
  // message type 3 (0x444702, 0x4c515b); 0x440c00 case 3 posts HUD event 60
  // with the speaker's object (player vtbl +0xa0: the vehicle when seated) unless
  // the speaker is the local player. The map node's case 0x3c (0x782990) marks
  // it for 12 s (+0xa2c), flash 0, kind 1.
  if (friendly && static_cast<int>(call.sender) != world.ownPlayer() && world.ownPlayer() >= 0) {
    if (const auto speaker = world.players().find(call.sender);
        speaker != world.players().end() && speaker->second.object) {
      const auto soldier = static_cast<std::uint16_t>(speaker->second.object);
      const auto root = vehicleRootOf(soldier);
      addMapMark(root ? root : soldier, 1, 12.0f, 0.0f);
    }
  }
  if (friendly) {
    chats.push_back({static_cast<int>(call.channel), static_cast<int>(call.type),
                     static_cast<int>(call.sender), 0, 0,
                     message.caption.empty() ? message.name : message.caption});
  }
  if (friendly || (message.localFlags & 1)) {
    // The voice folders are the data's (`Common/Sound/{Chinese,MEC,EU,English}/`,
    // Common_client.zip); which army speaks from which is taken from their names,
    // not read in the binary.
    const auto faction = terrain ? terrain->teamNames[call.team] : "";
    const char* language = faction == "CH" ? "Chinese" : faction == "MEC" ? "MEC" :
                           faction == "EU" ? "EU" : "English";
    const auto prefix = std::string("Common/Sound/") + language + '/';
    if (friendly && !(message.localFlags & 2) && !message.sample.empty() &&
        files.exists(prefix + message.sample)) {
      radioSamples.push_back(prefix + message.sample);
    } else if (!message.localSample.empty()) {
      radioSamples.push_back(prefix + message.localSample);
    }
  }
  std::printf("  RADIO: channel %u, team %u, sender %u, %s\n",
              call.channel, call.team, call.sender, message.name.c_str());
}

void RemoteWorld::noteSquadEvent(const bf2::Event& event) {
  const auto own = world.ownPlayer();
  const auto player = world.players().find(static_cast<unsigned>(own));
  if (player == world.players().end()) return;
  // The commander assignment (`CommanderEvent`) for our team: ours ends the
  // offer; another's ends our overhead view and the support we were waiting on.
  if (event.commanderAssignment &&
      static_cast<int>(event.commanderAssignment->team) == player->second.team) {
    if (event.commanderAssignment->player == world.ownPlayer()) {
      commanderOffer = false;
    } else {
      if (commanderViewTarget) commanderViewTarget.reset();
      pendingSupport.reset();
    }
    std::printf("  COMMANDER: player %d holds the command\n", event.commanderAssignment->player);
  }
}

bool RemoteWorld::isCommander() const {
  const auto self = world.players().find(static_cast<unsigned>(world.ownPlayer()));
  return commanderEnabled && self != world.players().end() && self->second.team == world.ownTeam() &&
         self->second.commander.value_or(false);
}

std::string RemoteWorld::commanderRole(bf2::CommanderAction action) {
  const int team = world.ownTeam();
  const int player = world.ownPlayer();
  if (!socket || !databaseSentAt || player < 0 || team < 1 || team > 2) {
    return "Waiting for team assignment.";
  }
  if (!commanderEnabled) return "Commander is disabled on this server.";
  using Action = bf2::CommanderAction;
  if (action == Action::Apply && std::ranges::any_of(world.players(), [team](const auto& entry) {
        return entry.second.team == team && entry.second.commander.value_or(false);
      })) return "Your team already has a commander.";
  if (action == Action::Resign && !isCommander()) return "You are outside the commander role.";
  if ((action == Action::Accept || action == Action::Decline) && !commanderOffer) {
    return "Waiting for a commander offer.";
  }
  link.queue(bf2::commanderEvent(action, team, player));
  if (action == Action::Accept || action == Action::Decline) commanderOffer = false;
  return "Commander request sent.";
}

std::optional<Vec3f> RemoteWorld::commanderTarget(float u, float v) const {
  if (!terrain || !std::isfinite(u) || !std::isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1) {
    return std::nullopt;
  }
  const float extent = 2 * terrain->halfExtent();
  Vec3f point{(u - 0.5f) * extent * terrain->primary.scale.x, 0,
              (0.5f - v) * extent * terrain->primary.scale.z};
  point.y = terrain->groundHeightAt(point);
  return point;
}

std::optional<Vec3f> RemoteWorld::commanderCameraTarget() const {
  return isCommander() ? commanderViewTarget : std::nullopt;
}

std::optional<Vec3f> RemoteWorld::commanderCameraEye() const {
  const auto target = commanderCameraTarget();
  if (!target || !terrain) return std::nullopt;
  // The overhead camera's height before the server places it: a quarter of the
  // map, at least 100 m — **not measured**.
  const float height = std::max(100.0f, std::min(terrain->halfExtent() * terrain->primary.scale.x,
                                                 terrain->halfExtent() * terrain->primary.scale.z) * 0.25f);
  return world.commanderCameraPosition().value_or(*target + Vec3f{0, height, 0});
}

void RemoteWorld::resetCommanderState() {
  commanderOffer = false;
  commanderViewTarget.reset();
  commanderEffects.clear();
  commanderReloadUntil = {};
  vehicleDropRemaining.reset();
  pendingSupport.reset();
  commanderScanUntil = 0;
  mapMarks.clear();
  satelliteScan = {};
  mapSpin = mapSpinStep = 0.0f;
  ownCallMedic = ownCallAmmo = ownCallRepair = false;
}

void RemoteWorld::addMapMark(std::uint16_t object, int kind, float duration, float flash,
                             std::string texture) {
  if (object == 0) return;
  const auto now = world.gameTick();
  std::erase_if(mapMarks, [&](const MapMark& mark) {
    return static_cast<float>(now - mark.start) * server::kTickTime >= mark.flash + mark.duration;
  });
  // 0x781ff0: any kind but 3 returns when the id is already in the list.
  if (std::ranges::any_of(mapMarks, [&](const MapMark& mark) { return mark.object == object; }))
    return;
  mapMarks.push_back({object, kind, now, duration, flash, std::move(texture)});
}

const RemoteWorld::MapMark* RemoteWorld::mapMarkOf(std::uint16_t object) const {
  const auto now = world.gameTick();
  for (const auto& mark : mapMarks) {
    if (mark.object == object &&
        static_cast<float>(now - mark.start) * server::kTickTime < mark.flash + mark.duration)
      return &mark;
  }
  return nullptr;
}

float RemoteWorld::mapMarkFade(const MapMark& mark) const {
  const float elapsed = static_cast<float>(world.gameTick() - mark.start) * server::kTickTime;
  if (elapsed < mark.flash || mark.duration <= 0.0f) return 1.0f;
  return std::clamp(1.0f - (elapsed - mark.flash) / mark.duration, 0.0f, 1.0f);
}

// The kit's `Ability` component (`Kits/Mec/MEC_Engineer.con`:
// `ability.hasRepairingAbility 1`); Linux AbilityGLCompTemplate keeps healing,
// repairing and ammo at +0x18/+0x19/+0x1a, the bytes 0, 1 and 2 the map reads.
// Which kit: the player's networked kit row (PlayerState 0x1000, `setKit`) in
// the level's `gameLogic.setKit <team> <row> <kit>` table, else the kit a
// pickup gave the soldier.
static bool kitHas(const RemoteWorld& remote, const bf2::RemotePlayer& player, const char* key) {
  const std::string* name = nullptr;
  if (remote.terrain && player.kit && *player.kit >= 0 && *player.kit < level::Level::kKitsPerTeam &&
      player.team >= 1 && player.team <= 2 && !remote.terrain->kits[player.team][*player.kit].empty())
    name = &remote.terrain->kits[player.team][*player.kit];
  if (!name) name = remote.templateNumbers.nameOf(remote.world.kitTemplateOf(player.object));
  const auto* kit = name ? remote.registry.find(*name) : nullptr;
  const auto* part = kit ? kit->component("ability") : nullptr;
  if (!part) return false;
  const auto found = part->properties.find(key);
  return found != part->properties.end() && !found->second.empty() &&
         found->second.back().asInt().value_or(0) != 0;
}

// Player::setIsTalking(true) (Linux 0x494920) stamps the last talk time; the
// soldier draw reads getIsTalking (stepMap).
void RemoteWorld::noteVoice(std::uint32_t player) { lastTalkTick[player] = world.gameTick(); }

// HUD events 46..50 (ECHudHECallForMedic 46, CallForAmmo 47, CallForRepair 48,
// CallForRevive 49, AutoCallForRevive 50 — their enum registration, BF2.exe
// 0x74e83c..0x74e8e4). The server forwards a client's SpottedEvent as remote
// event (2, +0x24) with the 24-byte body from +0x10 (Linux
// SpottedEvent::executeServer 0x430321); 50 also comes from the server itself
// when a soldier goes down (Soldier::handleMessage 0x54d2e9 →
// Game::callForSpecialist). The map node's case 0x2e..0x32 (0x782990) reads
// the caller's object id from the first two bytes.
void RemoteWorld::receiveSpecialistCall(const bf2::RemoteEvent& event) {
  if (event.number < 46 || event.number > 50 || event.data.size() < 2) return;
  const auto caller = static_cast<std::uint16_t>(event.data[0] | (event.data[1] << 8));
  const auto own = world.players().find(static_cast<std::uint32_t>(world.ownPlayer()));
  if (caller == 0 || own == world.players().end()) return;
  const auto from = std::ranges::find_if(world.players(), [&](const auto& entry) {
    return entry.second.object == caller;
  });
  if (from == world.players().end() || from->second.team != own->second.team) return;
  const bool medic = event.number == 46 || event.number == 49 || event.number == 50;
  if (from->first != own->first) {
    // Another player's call: the caller is marked for 12 s (+0xa2c, flash 0,
    // kind 4) when the local player's kit can answer it. The node's +0x7f
    // would skip that test; its only store is the constructor's 0 (0x781a17).
    const char* key = medic ? "hashealingability" : event.number == 47 ? "hasammoability"
                                                                       : "hasrepairingability";
    if (!kitHas(*this, own->second, key)) return;
    // 0x7730b0: +0x9b4 for 46, +0x9a8 for 47, +0x9ac for 48, +0x9b0 for 49/50;
    // the loader (0x7815f0..0x7816d1) fills them with these.
    const char* file = event.number == 46 ? "mini_Medic.tga" : event.number == 47 ? "mini_Ammo.tga"
                     : event.number == 48 ? "mini_Repair.tga" : "mini_Revive.tga";
    addMapMark(caller, 4, 12.0f, 0.0f, std::string("Ingame/Player/Icons/Minimap/") + file);
    return;
  }
  // The local player's own call: +0x7c, +0x7d or +0x7e until the map's next
  // draw (stepMap).
  ownCallMedic = medic;
  ownCallAmmo = event.number == 47;
  ownCallRepair = event.number == 48;
}

int RemoteWorld::stepMap(float dt, std::optional<float> drawnWidth, float worldSize) {
  constexpr float kTwoPi = 6.2831855f;           // 0x8ac48c
  constexpr float kTurnRate = 4.0f;              // +0xac0, 0x7818e8
  constexpr float kScanLineWidth = 150.0f;       // +0xaa0, 0x78189a
  constexpr float kScanFirstStep = 10.0f;        // 0x8913c0
  constexpr float kUavRadius = 100.0f;           // getUAVRadius, Linux 0x477813
  constexpr const char* kIcons = "Ingame/Player/Icons/Minimap/";
  const auto icon = [&](const char* file) { return std::string(kIcons) + file; };
  int sounds = 0;

  // 0x77b280: the spin, wrapped to 0 past 2π (0x77b2b3..0x77b2c0).
  mapSpinStep = dt * kTurnRate;
  mapSpin += mapSpinStep;
  if (mapSpin > kTwoPi) mapSpin = 0.0f;

  // 0x772680: while +0xa88 < +0xa84 the scan runs; +0xa98 follows it. After,
  // the reload +0xaa4 counts down and +0xa7d goes off (0x7727fb).
  auto& scan = satelliteScan;
  if (scan.elapsed < scan.active) {
    scan.elapsed += dt;
    if (scan.on && drawnWidth && *drawnWidth > 0.0f)
      scan.line = (kScanLineWidth + *drawnWidth) / scan.active * scan.elapsed;
  } else {
    if (scan.reload > 0.0f) scan.reload = std::max(0.0f, scan.reload - dt);
    scan.on = false;
  }

  const auto own = world.players().find(static_cast<std::uint32_t>(world.ownPlayer()));
  const int team = world.ownTeam();
  const auto tick = world.gameTick();

  // The +0xb3c list: the UAV while it lasts (centre +0xab0, radius +0xad0) and
  // the local team's radar objects (0x77bfe0; `ability.radarRadius`).
  struct Beam { float x, z, radius; };
  std::vector<Beam> beams;
  for (const auto& effect : commanderEffects) {
    if (effect.tool == bf2::CommanderTool::UAV && effect.expires > tick)
      beams.push_back({effect.position.x, effect.position.z, kUavRadius});
  }
  for (const auto& [id, object] : world.objects()) {
    const auto* name = templateNumbers.nameOf(object.templateId);
    const auto* definition = name ? registry.find(*name) : nullptr;
    const auto* ability = definition ? definition->component("ability") : nullptr;
    if (!ability) continue;
    const auto found = ability->properties.find("radarradius");
    const int radius = found == ability->properties.end() || found->second.empty()
        ? 0 : found->second.back().asInt().value_or(0);
    const bool destroyed = object.vehicleHealth && *object.vehicleHealth <= 0;
    if (radius > 0 && !destroyed && (object.team == 0 || object.team == team))
      beams.push_back({object.position.x, object.position.z, static_cast<float>(radius)});
  }

  // 0x782580 (the satellite) and 0x782710 (the UAV) for one drawn object.
  const auto sweep = [&](std::uint16_t id, Vec3f at, const char* uavTexture) {
    if (scan.on && drawnWidth && *drawnWidth > 0.0f && worldSize > 0.0f) {
      const float line = (scan.line / *drawnWidth - 0.5f) * worldSize;
      // +0xa9c is the previous test's line: the first object of a frame sees
      // the frame's step, the rest the same line again and so 10.
      const float step = scan.previous != 0.0f && line != scan.previous ? line - scan.previous
                                                                       : kScanFirstStep;
      const float behind = at.x - line;
      scan.previous = line;
      if (std::fabs(behind) < std::fabs(step) && behind < 0.0f) {
        addMapMark(id, 2, 2.0f, 7.0f, icon("mini_SatelliteObject.tga"));  // flash +0xa8c
        ++sounds;
      }
    }
    for (const auto& beam : beams) {
      const float dx = at.x - beam.x, dz = at.z - beam.z;
      if (std::hypot(dx, dz) > beam.radius) continue;
      float bearing = std::atan2(dx, -dz);  // map u with x, v against z
      if (bearing < 0.0f) bearing += kTwoPi;
      if (std::fabs((mapSpin - mapSpinStep) - bearing) < mapSpinStep) {
        addMapMark(id, 2, 0.5f, 0.3f, uavTexture ? icon(uavTexture) : std::string{});  // +0xaec
        ++sounds;
      }
    }
  };

  const auto talking = [&](std::uint32_t player) {
    const auto found = lastTalkTick.find(player);
    // getIsTalking: now − last <= 0.5 s (Linux 0x494950, Player::Player 0x49ed6c).
    return found != lastTalkTick.end() &&
           static_cast<float>(tick - found->second) * server::kTickTime <= 0.5f;
  };

  // The soldier draw (0x783440).
  for (const auto& [id, player] : world.players()) {
    if (!player.object || player.manDown.value_or(false) || !player.alive.value_or(true)) continue;
    const auto soldier = static_cast<std::uint16_t>(player.object);
    if (vehicleRootOf(soldier) || vehicleSeatOf(soldier)) continue;  // vtbl +0xa0 != +0x44
    const auto object = world.objects().find(soldier);
    if (object == world.objects().end() || !object->second.fromGhostStream) continue;
    if (player.team != team) {
      sweep(soldier, object->second.position, "mini_Soldier.tga");  // +0x988, 0x781579
      continue;
    }
    if (static_cast<int>(id) == world.ownPlayer() || own == world.players().end()) continue;
    // 0x783929/0x783994/0x7839ff: the local player's call marks a teammate
    // whose kit can answer, duration 0, flash +0xa2c = 12 s, kind 4.
    if (ownCallMedic && kitHas(*this, player, "hashealingability"))
      addMapMark(soldier, 4, 0.0f, 12.0f, icon("mini_Medic.tga"));
    if (ownCallRepair && kitHas(*this, player, "hasrepairingability"))
      addMapMark(soldier, 4, 0.0f, 12.0f, icon("mini_Repair.tga"));
    if (ownCallAmmo && kitHas(*this, player, "hasammoability"))
      addMapMark(soldier, 4, 0.0f, 12.0f, icon("mini_Ammo.tga"));
    // 0x783a97..0x783b41: talking (vtbl +0x278, Linux +0x27c getIsTalking) and
    // not muted (+0x274, getIsMuted) — kind 1, 12 s, flash 0. The client's
    // player slots sit one below the Linux ones (client +0xe4 team is Linux
    // `getTeam`, beside `setTeam` at +0xe4).
    if (talking(id) && !muted(id)) addMapMark(soldier, 1, 12.0f, 0.0f);
  }
  // The vehicle draw (0x783f90): texture 0 from the UAV (0x784716).
  for (const auto& [id, object] : world.objects()) {
    bool enemy = false;
    for (const auto& [playerId, occupant] : world.players()) {
      if (occupant.object && occupant.team != team && vehicleRootOf(occupant.object) == id) enemy = true;
    }
    if (enemy) sweep(id, object.position, nullptr);
  }
  // 0x785200: the map draw clears the three flags.
  ownCallMedic = ownCallAmmo = ownCallRepair = false;
  return sounds;
}

std::string RemoteWorld::setCommanderCamera(std::optional<Vec3f> target) {
  if (!isCommander()) return "Only the commander can open the overhead camera.";
  if (target && !playerSpawned) return "Deploy before switching to the overhead camera.";
  using Action = bf2::CommanderAction;
  if (target.has_value() != commanderViewTarget.has_value()) {
    link.queue(bf2::commanderEvent(target ? Action::OverheadView : Action::SoldierView,
                                   world.ownTeam(), world.ownPlayer()));
    // Action 8 uses the team byte for the top-down flag. On entry retain the
    // real team byte for retail's authorization and on exit use soldier mode.
    if (target) link.queue(bf2::commanderEvent(Action::TopDown, world.ownTeam(), world.ownPlayer()));
  }
  if (target && terrain) {
    // The same height as commanderCameraEye: **not measured**.
    const float height = std::max(100.0f, std::min(terrain->halfExtent() * terrain->primary.scale.x,
                                                   terrain->halfExtent() * terrain->primary.scale.z) * 0.25f);
    link.queue(bf2::commanderCameraEvent(*target + Vec3f{0, height, 0}));
  }
  commanderViewTarget = target;
  return target ? "Commander overhead camera." : "Soldier camera.";
}

bool RemoteWorld::commanderAssetReady(bf2::CommanderTool tool) const {
  if (!isCommander() || world.gameTick() < commanderReloadUntil[static_cast<unsigned>(tool)]) {
    return false;
  }
  using Tool = bf2::CommanderTool;
  const auto& self = world.players().at(static_cast<unsigned>(world.ownPlayer()));
  switch (tool) {
    case Tool::UAV: return self.uavReady && self.commanderFlags[4] && self.commanderArmor[2] > 0;
    case Tool::Scan: return self.scanReady && self.commanderFlags[2] && self.commanderArmor[0] > 0;
    case Tool::Artillery: return self.artilleryAvailable && self.commanderArmor[1] > 0;
    case Tool::Vehicle: return vehicleDropRemaining.value_or(0) > 0;
    default: return true;
  }
}

void RemoteWorld::sendSupportReply(const bf2::SquadOrder& request, bool accepted) {
  link.queue(bf2::orderResponseEvent({request.squad, request.team, false, bf2::OrderRoute::Request,
                                      request.id}));
  const auto leader = std::ranges::find_if(world.players(), [&](const auto& entry) {
    return entry.second.team == static_cast<int>(request.team) && entry.second.squad == request.squad &&
           entry.second.squadLeader;
  });
  if (leader == world.players().end()) return;
  constexpr std::array<std::string_view, 4> accept{"artillery_accept", "uav_accept",
                                                "vehicledrop_accept", "supply_accept"};
  constexpr std::array<std::string_view, 4> decline{"artillery_deny", "uav_deny",
                                                 "vehicledrop_deny", "supply_deny"};
  loadRadioMessages();
  const auto message = std::ranges::find(radioMessages, (accepted ? accept : decline)[request.id],
                                         &RadioMessage::name);
  if (message != radioMessages.end() && message - radioMessages.begin() <= 255) {
    link.queue(bf2::radioCallEvent({static_cast<unsigned>(message - radioMessages.begin()),
        static_cast<unsigned>(world.ownPlayer()), request.team, leader->first, 14, 3}));
  }
}

void RemoteWorld::receiveCommanderHud(const bf2::RemoteEvent& event) {
  using Tool = bf2::CommanderTool;
  const auto number = event.number;
  const auto word = [&](std::size_t offset) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(event.data[offset + i]) << (i * 8);
    return result;
  };
  const auto fraction = [&](std::size_t offset) {
    const float value = std::bit_cast<float>(word(offset));
    vehicleDropRemaining = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
  };
  if (number == 33 && event.data.size() == 8 && event.value == world.ownPlayer()) {
    fraction(4);
    return;
  }
  // 34 is `ECHudHECommanderDecline` (hudEventName): the offer is withdrawn.
  if (number == 34 && commanderOffer) {
    commanderOffer = false;
    return;
  }
  if (number == 75 && event.data.size() == 4) {
    fraction(0);
    return;
  }
  if (number == 41 && event.data.empty()) {
    // Case 0x29 (0x782990): only when the reload +0xaa4 is spent. +0xa84 =
    // getSatelliteScanActiveTime 11 s, +0xaa4 = getSatelliteScanReloadTime
    // 16 s (Linux GameLogic::GameLogic 0x47782b, 0x477837).
    if (satelliteScan.reload == 0.0f) {
      satelliteScan = {11.0f, 0.0f, 0.0f, 0.0f, 16.0f, true};
    }
    // The scan's 11 s as ticks (getSatelliteScanActiveTime above, 11 / (1/30)).
    commanderScanUntil = world.gameTick() + 330;
    std::printf("  COMMANDER: satellite scan\n");
    return;
  }

  Tool tool;
  std::size_t offset = 0;
  if (number == 40 && event.data.size() == 12) {
    tool = Tool::UAV;
  } else if (number == 42 && event.data.size() == 16) {
    tool = Tool::Artillery;
    if (!event.data[12]) {
      if (pendingSupport && pendingSupport->request.id == 0) pendingSupport.reset();
      return;
    }
  } else if (number == 74 && event.data.size() == 20) {
    tool = Tool::Vehicle;
    offset = 8;
    fraction(4);
  } else if (number == 83 && event.data.size() == 12) {
    tool = Tool::Supplies;
  } else {
    return;
  }
  const Vec3f position{std::bit_cast<float>(word(offset)), std::bit_cast<float>(word(offset + 4)),
                       std::bit_cast<float>(word(offset + 8))};
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return;

  const auto tick = world.gameTick();
  const auto seconds = [](unsigned value) {
    return static_cast<unsigned>(value / server::kTickTime + 0.5f);
  };
  std::erase_if(commanderEffects, [&](const auto& effect) {
    return effect.tool == tool || effect.expires <= tick;
  });
  // How long an asset's mark lasts (30 s for the UAV, 20 s for the rest) and its
  // reload (60 s): **not measured**; the client's own timers are not read.
  commanderEffects.push_back({tool, position, tick + seconds(tool == Tool::UAV ? 30 : 20), tick});
  if (tool == Tool::UAV) mapSpin = 0.0f;  // 0x77a0d0 zeroes +0xabc
  if (tool != Tool::UAV) commanderReloadUntil[static_cast<unsigned>(tool)] = tick + seconds(60);
  constexpr std::array requested{Tool::Artillery, Tool::UAV, Tool::Vehicle, Tool::Supplies};
  // The 30 m that ties a dispatch to the request it answers: **not measured**.
  if (pendingSupport && tick <= pendingSupport->expires && isCommander() &&
      requested[pendingSupport->request.id] == tool &&
      std::hypot(position.x - pendingSupport->position.x, position.z - pendingSupport->position.z) < 30.0f) {
    sendSupportReply(pendingSupport->request, true);
    pendingSupport.reset();
  }
  std::printf("  COMMANDER: asset %u at %.1f %.1f %.1f\n", static_cast<unsigned>(tool), position.x,
              position.y, position.z);
}

std::string RemoteWorld::commanderTool(bf2::CommanderTool tool, Vec3f position, unsigned squad) {
  if (!isCommander()) return "Only the commander can send this action.";
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
    return "Choose a valid map position.";
  }
  using Tool = bf2::CommanderTool;
  const int team = world.ownTeam();
  const bool order = tool == Tool::Move || tool == Tool::Attack || tool == Tool::Defend;
  if (order && (squad < 1 || squad > 9 || std::ranges::none_of(world.players(), [&](const auto& entry) {
        return entry.second.team == team && entry.second.squad == squad && entry.second.squadLeader;
      }))) return "Choose a squad with a leader.";
  if (!order && !commanderAssetReady(tool)) {
    return "The server reports that this asset is unavailable.";
  }
  link.queue(bf2::commanderToolEvent(tool, team, squad, position));
  return order ? "Squad order sent." : "Commander asset requested.";
}

std::string RemoteWorld::commanderRespond(unsigned squad, unsigned id, bool accept) {
  if (!isCommander()) return "Only the commander can respond to support requests.";
  const auto requests = world.commanderRequests();
  const auto request = std::ranges::find_if(requests, [&](const auto& item) {
    return item.squad == squad && item.id == id;
  });
  if (request == requests.end()) return "That support request is no longer available.";
  if (accept) {
    const auto point = world.orderPosition(*request);
    if (!point) return "The server has not supplied a target position.";
    constexpr std::array tools{bf2::CommanderTool::Artillery, bf2::CommanderTool::UAV,
                               bf2::CommanderTool::Vehicle, bf2::CommanderTool::Supplies};
    const auto tick = world.gameTick();
    if (pendingSupport && pendingSupport->expires > tick) {
      return "Waiting for the server to confirm the previous support dispatch.";
    }
    if (!commanderAssetReady(tools[id])) return "The server reports that this asset is unavailable.";
    link.queue(bf2::commanderToolEvent(tools[id], request->team, 0, *point));
    // How long the dispatch is waited for, 10 s in ticks: **not measured**.
    pendingSupport = PendingSupport{*request, *point, tick + 300};
    return "Asset requested. Waiting for the server to confirm dispatch.";
  }
  sendSupportReply(*request, false);
  return "Support decline sent.";
}

void RemoteWorld::receiveVote(const bf2::Vote& vote) {
  if (vote.type != bf2::VoteMap && vote.type != bf2::VoteKick) return;
  auto& active = vote.type == bf2::VoteMap ? mapVote : kickVote;
  std::string message;
  switch (vote.status) {
    case bf2::VoteIssue: {
      active = vote;
      if (static_cast<int>(vote.caster) == world.ownPlayer()) {
        lastVoteIssued = std::chrono::steady_clock::now();
      }
      if (vote.type == bf2::VoteMap) {
        message = "Map vote";
        if (vote.item < mapList.entries.size()) {
          message += " for " + mapList.entries[vote.item].levelName;
        }
      } else {
        const auto player = world.players().find(vote.item);
        message = "Kick vote";
        if (player != world.players().end()) message += " for " + player->second.name;
      }
      message += ". Use vote " + std::string(vote.type == bf2::VoteMap ? "map" : "kick") +
                 " yes or no.";
      break;
    }
    case bf2::VoteUpdate:
      if (active && active->item == vote.item) {
        active->forCount = vote.forCount;
        active->againstCount = vote.againstCount;
        if (vote.needed) active->needed = vote.needed;
      }
      return;
    case bf2::VoteFailed:
    case bf2::VoteCancelled:
    case bf2::VotePassed:
      if (active && active->item == vote.item) active.reset();
      message = vote.status == bf2::VotePassed ? "Vote passed." :
                vote.status == bf2::VoteFailed ? "Vote failed." : "Vote cancelled.";
      break;
    case bf2::VoteMissingTarget: message = "The vote target is no longer available."; break;
    case bf2::VoteConflict: message = "The server refused a conflicting vote."; break;
    case bf2::VoteTooFew: message = "Too few players are eligible to vote."; break;
    default: return;
  }
  // How the original's HUD shows a vote is not read; it is logged.
  std::printf("  VOTE: %s\n", message.c_str());
}

std::string RemoteWorld::manageVote(std::string_view command) {
  if (!serverMatchOptions || !serverMatchOptions->voting) return "Voting is disabled on this server.";
  if (endOfRound) return "Wait for the next round to vote.";
  const int own = world.ownPlayer();
  const auto self = world.players().find(static_cast<unsigned>(own));
  if (self == world.players().end()) return "Waiting for team assignment.";
  std::istringstream input{std::string(command)};
  std::string category, argument;
  input >> category;
  std::getline(input >> std::ws, argument);
  const bool map = category == "map";
  if (!map && category != "kick") return "Use vote map <index> or vote kick <player>.";
  const auto& active = map ? mapVote : kickVote;
  const unsigned type = map ? bf2::VoteMap : bf2::VoteKick;
  if (argument == "yes" || argument == "no") {
    if (!active) return "There is no active vote of that type.";
    if (!map && active->item == static_cast<unsigned>(own)) return "You cannot vote on your own kick.";
    link.queue(bf2::voteEvent({.item = active->item, .option = argument == "yes" ? 0u : 1u,
                               .status = bf2::VoteUpdate, .type = type}));
    return "Ballot sent.";
  }
  if (active) return "A vote of that type is already open.";
  // Five minutes between our own votes: **not measured** (the server's own
  // limit is not read).
  if (lastVoteIssued != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() - lastVoteIssued < std::chrono::minutes(5)) {
    return "Wait five minutes between starting votes.";
  }
  unsigned target = 0;
  if (map) {
    if (mapList.entries.empty()) return "The server has not supplied a map rotation.";
    if (argument.empty()) {
      std::string names = "Map rotation";
      for (std::size_t i = 0; i < mapList.entries.size(); ++i) {
        names += " | " + std::to_string(i) + " " + mapList.entries[i].levelName;
      }
      return names;
    }
    const auto result = std::from_chars(argument.data(), argument.data() + argument.size(), target);
    if (result.ec != std::errc{} || result.ptr != argument.data() + argument.size() ||
        target >= mapList.entries.size() || target > 255) return "Choose a map rotation index.";
  } else {
    const auto player = std::ranges::find_if(world.players(), [&](const auto& entry) {
      return std::to_string(entry.first) == argument || entry.second.name == argument;
    });
    if (player == world.players().end() || player->first >= 254 || player->second.aiPlayer ||
        player->first == static_cast<unsigned>(own) ||
        (serverMatchOptions->teamVoteOnly && player->second.team != self->second.team)) {
      return "Choose an eligible player by ID or name.";
    }
    target = player->first;
  }
  link.queue(bf2::voteEvent({.item = target, .caster = static_cast<unsigned>(own),
                             .status = bf2::VoteIssue, .type = type}));
  return "Vote requested.";
}

std::string RemoteWorld::scoreboardKick(std::uint32_t player, int column) {
  // The manage page's kick columns: with a kick vote open on this player, column 7
  // is yes and 8 no; with none open, either starts one on him. Everything the
  // console's `vote kick` checks (voting on, not ourselves, our team only when
  // the server says so, the five minutes between votes) still holds.
  if (kickVote && kickVote->item == player) return manageVote(column == 7 ? "kick yes" : "kick no");
  if (kickVote) return "A vote of that type is already open.";
  return manageVote("kick " + std::to_string(player));
}

std::string RemoteWorld::communicate(std::string_view command, Vec3f eye, Vec3f direction,
                                     std::optional<Vec3f> selectedPoint) {
  const int own = world.ownPlayer();
  const auto self = world.players().find(static_cast<unsigned>(own));
  if (!socket || !databaseSentAt || self == world.players().end() ||
      (self->second.team != 1 && self->second.team != 2)) return "Waiting for team assignment.";

  const unsigned team = self->second.team;
  const unsigned squad = self->second.squad.value_or(0);
  const bool leader = squad && self->second.squadLeader;
  const auto split = command.find(' ');
  const auto verb = command.substr(0, split);
  auto argument = split == command.npos ? std::string_view{} : command.substr(split + 1);
  while (!argument.empty() && argument.front() == ' ') argument.remove_prefix(1);
  const auto queue = [&](bf2::EventBits event) { link.queue(std::move(event)); };
  if (verb == "vote") return manageVote(argument);
  if (verb == "commander") {
    using Action = bf2::CommanderAction;
    using Tool = bf2::CommanderTool;
    std::istringstream input{std::string(argument)};
    std::string action;
    input >> action;
    if (action == "apply") return commanderRole(Action::Apply);
    if (action == "accept") return commanderRole(Action::Accept);
    if (action == "decline") return commanderRole(Action::Decline);
    if (action == "resign") return commanderRole(Action::Resign);
    if (action == "soldier") return setCommanderCamera(std::nullopt);
    if (action == "status") {
      if (!commanderEnabled) return "Commander is disabled on this server.";
      if (!isCommander()) return commanderOffer ? "Commander offer received. Accept or decline."
                                                : "You are outside the commander role.";
      std::string status = "Ready:";
      for (const auto& [label, tool] : std::array{
          std::pair{" scan", Tool::Scan}, std::pair{" uav", Tool::UAV},
          std::pair{" artillery", Tool::Artillery}, std::pair{" supplies", Tool::Supplies},
          std::pair{" vehicle", Tool::Vehicle}}) {
        if (commanderAssetReady(tool)) status += label;
      }
      return status == "Ready:" ? "No commander assets ready." : status;
    }
    if (action == "requests") {
      if (!isCommander()) return "Only the commander can see support requests.";
      std::string notice = "Requests (squad / type)";
      for (const auto& request : world.commanderRequests()) {
        constexpr std::array kinds{"artillery", "uav", "vehicle", "supplies"};
        notice += " | " + std::to_string(request.squad) + " " + kinds[request.id];
      }
      return notice;
    }
    if (action == "approve" || action == "deny") {
      unsigned targetSquad;
      std::string asset;
      if (!(input >> targetSquad >> asset)) return "Enter a squad number and requested asset.";
      constexpr std::array<std::string_view, 4> kinds{"artillery", "uav", "vehicle", "supplies"};
      const auto id = std::ranges::find(kinds, asset);
      if (id == kinds.end()) return "Choose artillery, uav, vehicle, or supplies.";
      return commanderRespond(targetSquad, static_cast<unsigned>(id - kinds.begin()), action == "approve");
    }
    constexpr std::array<std::string_view, 8> choices{
        "move", "attack", "defend", "uav", "scan", "artillery", "supplies", "vehicle"};
    const auto choice = std::ranges::find(choices, action);
    if (choice == choices.end() && action != "camera") {
      return "Commander: apply, accept, decline, resign, status, camera, soldier, requests, approve, deny, move, attack, defend, uav, scan, artillery, supplies, vehicle.";
    }
    unsigned targetSquad = 0;
    if (choice - choices.begin() < 3 && !(input >> targetSquad)) return "Enter a squad number.";
    float u = 0.5f, v = 0.5f;
    if (input >> std::ws && !input.eof()) {
      if (!(input >> u >> v)) return "Enter map coordinates between 0 and 1.";
      selectedPoint = commanderTarget(u, v);
    } else if (!selectedPoint) {
      selectedPoint = action == "camera" || action == "scan" ? commanderTarget(u, v) : commanderCameraTarget();
    }
    if (!selectedPoint) return "Choose a map point using U V coordinates or the overhead camera.";
    if (action == "camera") return setCommanderCamera(selectedPoint);
    return commanderTool(static_cast<Tool>(choice - choices.begin()), *selectedPoint, targetSquad);
  }
  const auto channel = verb == "all" ? bf2::ChatChannel::All :
                       verb == "team" ? bf2::ChatChannel::Team : bf2::ChatChannel::Squad;
  if (verb == "all" || verb == "team" || verb == "squad") {
    if (channel == bf2::ChatChannel::Squad && !squad) return "Join a squad to use squad chat.";
    if (argument.empty()) return "Enter a message.";
    std::string message;
    if (channel == bf2::ChatChannel::Team) message = "HUD_TEXT_CHAT_TEAM";
    if (channel == bf2::ChatChannel::Squad) message = "HUD_TEXT_CHAT_SQUAD";
    message += argument;
    auto events = bf2::chatEvents(channel, own, message);
    if (events.empty()) return "Message exceeds the retail chat limit or contains a line break.";
    for (auto& event : events) queue(std::move(event));
    // Retail sends the message to its recipients and displays the sender's copy locally.
    chats.push_back({static_cast<int>(channel), 2, own, 0, 0, std::move(message)});
    return "Message sent.";
  }

  const auto number = [](std::string_view text) {
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? value : 0u;
  };
  if (verb == "switch") {
    const unsigned targetTeam = number(argument);
    if ((targetTeam != 1 && targetTeam != 2) || targetTeam == team) return "Choose the other team (1 or 2).";
    queue(bf2::postRemoteEvent(bf2::kNetworkCategory, bf2::kNetSelectTeam, targetTeam));
    return "Team switch requested.";
  }
  if (verb == "accept" || verb == "decline") {
    if (!leader) return "Only the squad leader can respond to a commander order.";
    const auto orders = world.ownOrders();
    const auto order = std::ranges::find(orders, bf2::OrderRoute::Commander, &bf2::SquadOrder::route);
    if (order == orders.end()) return "No commander order to respond to.";
    const bool accepted = verb == "accept";
    if (accepted) {
      auto adopted = *order;
      adopted.route = bf2::OrderRoute::Squad;
      adopted.accepted = true;
      queue(bf2::squadOrderEvent(adopted));
    }
    queue(bf2::commanderOrderReplyEvent(squad, team, accepted));
    queue(bf2::orderResponseEvent({squad, team, false, bf2::OrderRoute::Commander, 5}));
    return accepted ? "Order acceptance sent." : "Order declined.";
  }

  const auto radio = [&](std::string_view name, unsigned channel) {
    loadRadioMessages();
    const auto message = std::ranges::find(radioMessages, name, &RadioMessage::name);
    if (message == radioMessages.end() || message - radioMessages.begin() > 255) return false;
    const bf2::RadioCall call{static_cast<unsigned>(message - radioMessages.begin()),
                             static_cast<unsigned>(own), team, 0, channel, 3};
    queue(bf2::radioCallEvent(call));
    receiveRadio(call);
    return true;
  };
  if (verb == "radio") {
    if (argument == "medic" || argument == "ammo" || argument == "repair") {
      if (!ourSoldier) return "Deploy before calling for a specialist.";
      const unsigned hudEvent = argument == "medic" ? 46 : argument == "ammo" ? 47 : 48;
      queue(bf2::spotEvent(ourSoldier, 0, team, {}, hudEvent));
      const auto message = argument == "medic" ? "medic" : argument == "ammo" ? "need_ammo" : "need_repair";
      return radio(message, argument == "medic" ? 6 : 17) ? "Radio call sent." : "Specialist marker requested.";
    }
    struct Call {
      std::string_view input, voice;
      unsigned channel;
      bool teamInVehicle = false;
    };
    static constexpr Call calls[] = {
        {"roger", "roger_that", 17, true}, {"negative", "negative", 17, true},
        {"backup", "backup", 17, true}, {"thanks", "PLAYER_CONFIRM_thankyou", 17, true},
        {"sorry", "PLAYER_CONFIRM_sorry", 17, true}, {"go", "PLAYER_TACTICS_gogogo", 10},
        {"follow", "PLAYER_TACTICS_followme", 10}, {"pickup", "req_pickup", 7},
        {"get-in", "get_in", 10}, {"bail-out", "bail_out", 11},
        // The squad member's rose (Squad.con, `game.radioMessage TCSquad …`):
        // TCSquad is talk channel 3 (BF2.exe 0x442239); the leader's
        // `sendRequestForOrder` (0x7afe90) sends req_new_order on 2, TCCommander
        // (0x44220f).
        {"squad-orders", "req_new_order", 3}, {"squad-artillery", "req_artillery", 3},
        {"squad-supplies", "PLAYER_REQUEST_requestsupplies", 3},
        {"squad-vehicle", "PLAYER_REQUEST_requestvehicledrop", 3},
        {"new-order", "req_new_order", 2}};
    const auto call = std::ranges::find(calls, argument, &Call::input);
    if (call == std::end(calls)) {
      return "Radio: roger, negative, backup, thanks, sorry, go, follow, pickup, get-in, bail-out, medic, ammo, repair.";
    }
    const unsigned channel = call->teamInVehicle && vehicleRootOf(ourSoldier) ? 1 : call->channel;
    return radio(call->voice, channel) ? "Radio call sent." : "This radio call is unavailable.";
  }

  if ((verb != "spot" && verb != "order" && verb != "request") ||
      !playerSpawned || !ourSoldier) return "Deploy to spot, issue an order, or request support.";
  // An order or a request picks at 1000 m: the rose's opening, BF2.exe 0x758540,
  // manager slot 0x14c(&object, &point, 1000.0f, 0). The spot's 350 m is ours
  // (source not found).
  float range = verb == "spot" ? 350.0f : 1000.0f;
  Vec3f point = eye + direction * range;
  bool hit = false;
  if (!selectedPoint && collision) {
    const auto wall = collision->segmentContact(eye, direction * range, eye + direction * (range * 0.5f),
                                                  range * 0.5f + 2.0f);
    if (wall) {
      range += wall->along;
      point = wall->point;
      hit = true;
    }
  }
  // The engine's pick (manager slot 0x14c) is not ported. Ours: the collision
  // world's first contact, then the terrain in 2 m steps — **not measured**.
  if (!selectedPoint && terrain) {
    for (float d = 2.0f; d <= range; d += 2.0f) {
      const auto candidate = eye + direction * d;
      if (candidate.y <= terrain->groundHeightAt(candidate)) {
        range = d;
        point = candidate;
        hit = true;
        break;
      }
    }
  }
  if (verb == "spot") {
    std::uint16_t target = 0;
    for (const auto& [id, player] : world.players()) {
      if (!player.object || player.team != static_cast<int>(3 - team) ||
          player.manDown.value_or(false) || !player.alive.value_or(true)) continue;
      const auto seat = vehicleSeatOf(player.object);
      const auto netId = seat ? seat->parent : player.object;
      const auto object = world.objects().find(netId);
      if (object == world.objects().end() || !object->second.fromGhostStream) continue;
      // The target's sphere, its centre over the pivot and its radius (a vehicle's
      // 0.8 and 2, a soldier's 0.95 and 0.85): **not measured**.
      const Vec3f centre = object->second.position + Vec3f{0, seat ? 0.8f : 0.95f, 0};
      const auto offset = centre - eye;
      const float radius = seat ? 2.0f : 0.85f;
      const float distance = dot(offset, direction);
      if (distance <= 0 || distance > range + radius ||
          length(offset - direction * distance) > radius) continue;
      target = netId;
      range = distance;
      point = centre;
    }
    if (!target && !hit) return "Aim at an enemy or a position in the world.";
    queue(bf2::spotEvent(ourSoldier, target, team, point));
    radio("spotted", 1);
    return "Spot reported.";
  }
  if (!leader) return "Only the squad leader can issue orders or request support.";
  const auto id = verb == "order" ?
      // HUD/HudSetup/Communication/SquadLeader.con: `SquadLeader.sendOrder` 4 move,
      // 5 attack, 6 defend, 7 repair, 8 destroy.
      (argument == "move" ? 4u : argument == "attack" ? 5u : argument == "defend" ? 6u :
       argument == "repair" ? 7u : argument == "destroy" ? 8u : 255u) :
      (argument == "artillery" ? 0u : argument == "uav" ? 1u :
       argument == "vehicle" ? 2u : argument == "supplies" ? 3u : 255u);
  if (id == 255) return verb == "order" ? "Order options: move, attack, defend, repair, destroy."
                                        : "Request options: artillery, uav, vehicle, supplies.";
  if (!selectedPoint && !hit) return "Aim at the ground or choose a map position.";
  const auto route = verb == "order" ? bf2::OrderRoute::Squad : bf2::OrderRoute::Request;
  queue(bf2::squadOrderEvent({squad, team, id, 0, {selectedPoint.value_or(point), {}, {}},
                             route, true}));
  return verb == "order" ? "Order sent." : "Support request sent.";
}


const KnownObject* nearestKnown(const std::vector<KnownObject>& known, const Vec3f& at,
                                float tolerance) {
  const KnownObject* best = nullptr;
  float bestDistance = 0.0f;
  for (const auto& candidate : known) {
    const float d = length(candidate.position - at);
    if (!best || d < bestDistance) {
      best = &candidate;
      bestDistance = d;
    }
  }
  return best && bestDistance < tolerance ? best : nullptr;
}

obf2::game::Registry buildRegistry(obf2::FileSystem& files) {
  obf2::game::Registry registry;
  const auto key = [](std::string_view path) {
    std::string out = obf2::normalizeAssetPath(path);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
  };
  std::vector<std::string> cons, tweaks;
  for (auto& path : files.list()) {
    const std::string_view extension = obf2::assetExtension(path);
    if (extension == "con") cons.push_back(std::move(path));
    else if (extension == "tweak") tweaks.push_back(std::move(path));
  }
  for (auto* list : {&cons, &tweaks}) {
    std::sort(list->begin(), list->end());
    list->erase(std::unique(list->begin(), list->end()), list->end());
  }

  // The `.con` files first: each `include`s its own `.tweak` (a vehicle's
  // `usjep_hmmwv.con` ends with `include usjep_hmmwv.tweak`), which is how the
  // engine loads it. A `.tweak` is then run on its own only when no `.con`
  // reached it. Running every `.tweak` a second time used to repeat each of its
  // `addTemplate`s, so a vehicle had its tweak-added seats twice.
  //
  // Reading the files is the expensive half — unpacking each out of its archive
  // and taking it apart — and every file is read on its own, so that half is
  // done on every core. Feeding the registry is not: a `.tweak` changes the
  // template a `.con` created, so the commands go in one at a time and in the
  // order the sorted list gives, whatever order the threads finished in.
  //
  // A chunk at a time rather than all at once: the whole corpus is millions of
  // commands and holding them would cost more memory than the level.
  std::set<std::string> reached;
  const auto run = [&](const std::vector<std::string>& configs, bool skipReached) {
    constexpr std::size_t kChunk = 512;
    std::vector<std::vector<obf2::con::Command>> parsed(kChunk);
    std::vector<std::vector<std::string>> loaded(kChunk);
    for (std::size_t start = 0; start < configs.size(); start += kChunk) {
      const std::size_t count = std::min(kChunk, configs.size() - start);
      obf2::parallelFor(count, [&](std::size_t i) {
        std::vector<obf2::con::Command>& into = parsed[i];
        into.clear();
        loaded[i].clear();
        if (skipReached && reached.count(key(configs[start + i])) != 0) return;
        obf2::con::RecordingProvider recording(files);
        obf2::con::Interpreter interpreter(
            recording, [&into](const obf2::con::Command& command) { into.push_back(command); });
        interpreter.runFile(configs[start + i]);
        loaded[i] = recording.loaded();
      });
      for (std::size_t i = 0; i < count; ++i) {
        for (const obf2::con::Command& command : parsed[i]) registry.feed(command);
        for (const std::string& path : loaded[i]) reached.insert(key(path));
      }
    }
  };
  run(cons, false);
  run(tweaks, true);
  return registry;
}

void RemoteWorld::updateControlledVehicle(std::span<const std::byte> packet,
                                          const bf2::ControlObjectState& state,
                                          std::uint32_t serverTick) {
  if (state.isSoldier || !state.stateStart ||
      state.stateEnd < state.stateStart + bf2::kObjectStateMaskBits ||
      state.stateEnd > packet.size() * 8 ||
      state.baselineSlot >= controlledVehicleBaselines.size()) return;

  const auto root = world.objects().find(state.networkId);
  const auto* name = root == world.objects().end()
      ? nullptr : templateNumbers.nameOf(root->second.templateId);
  const auto* object = name ? registry.find(*name) : nullptr;
  if (!object || object->className != "PlayerControlObject") return;

  const bool mobile = object->number("hasMobilePhysics").value_or(1.0f) != 0;
  const bool aircraft = aircraftControls(object) != PilotControls::None;
  const bool surface = mobile && !aircraft;
  auto& baseline = controlledVehicleBaselines[state.baselineSlot];
  const bool older = serverTick < controlledVehicleTick;
  if (older && (!surface || state.delta || baseline)) return;
  const auto decoded = bf2::readControlledVehicleState(packet, state, mobile,
                                                        baseline ? &*baseline : nullptr);
  if (decoded && !state.delta) baseline = *decoded;
  if (older) return;
  // The absolute hull position in the header can still be read when a
  // surface vehicle's delta arrives before its baseline.
  if (!decoded && !(surface && state.delta && !baseline)) return;
  auto pose = decoded.value_or(bf2::SimpleObjectState{});
  if (surface || aircraft) pose.position = state.compressionReference;
  if (!world.feedControlledVehicle(state.networkId, serverTick, pose)) return;
  controlledVehicleTick = serverTick;

  if (surface) {
    const bool rotated = pose.rotation && std::abs(pose.rotationLength - 1.0f) < 0.01f;
    auto& hull = controlledHullTrack.push(static_cast<float>(serverTick) * bf2::kGhostTickMs,
                                          true, rotated);
    hull.position = *pose.position;
    hull.velocity = pose.velocity;
    if (rotated) hull.rotation = pose.rotation;
  }
}

std::uint16_t RemoteWorld::vehicleRootOf(std::uint16_t id) const {
  if (!id) return 0;
  if (const auto object = world.objects().find(id); object != world.objects().end()) {
    const auto* name = templateNumbers.nameOf(object->second.templateId);
    const auto* definition = name ? registry.find(*name) : nullptr;
    if (definition && definition->className == "PlayerControlObject") return id;
    if (definition && definition->className == "Soldier") return 0;
  }
  if (const auto seat = vehicleSeatOf(id)) return seat->parent;
  return 0;
}

std::optional<RemoteWorld::VehicleSeat> RemoteWorld::vehicleSeatOf(std::uint16_t id) const {
  const auto& objects = world.objects();
  const auto independentlyCreated = objects.find(id);
  if (independentlyCreated != objects.end() && independentlyCreated->second.templateId != 0) {
    return std::nullopt;
  }
  auto parent = objects.lower_bound(id);
  while (parent != objects.begin()) {
    --parent;
    const auto seatIndex = static_cast<std::size_t>(id - parent->first);
    if (seatIndex > 32) break;
    const auto templateId = parent->second.templateId;
    if (!templateId) continue;
    auto [entry, inserted] = vehicleSeats.try_emplace(templateId);
    if (inserted) {
      const auto* name = templateNumbers.nameOf(templateId);
      const auto* root = name ? registry.find(*name) : nullptr;
      if (root && root->className == "PlayerControlObject") {
        if (const auto tree = game::flattenObject(registry, root->name)) {
          for (const auto& part : tree->parts) {
            if (part.className != "PlayerControlObject") continue;
            const auto* control = registry.find(part.templateName);
            if (control && control->property("setNetworkableInfo")) {
              entry->second.push_back(part.templateName);
            }
          }
        }
      }
    }
    // NetworkManager::analyseObject assigns consecutive IDs in control-object
    // tree order. A root without its own descriptor cannot anchor this range.
    const auto* rootName = templateNumbers.nameOf(templateId);
    if (rootName && !entry->second.empty() && normalizeAssetPath(entry->second.front()) == normalizeAssetPath(*rootName) &&
        seatIndex < entry->second.size()) {
      return VehicleSeat{parent->first, entry->second[seatIndex]};
    }
    if (parent->second.hasCreationPosition && parent->second.templateId) break;
  }
  return std::nullopt;
}

std::vector<KnownObject> buildKnownObjects(obf2::FileSystem& files, const std::string& levelName,
                                           std::string* error,
                                           const std::optional<level::GameplayObjects>* selected) {
  std::vector<KnownObject> known;
  const auto fallback = selected ? std::nullopt
                                 : level::loadGameplayObjects(files, levelName, "gpm_cq", 16, error);
  const auto& gameplay = selected ? *selected : fallback;
  if (gameplay) {
    for (const auto& point : gameplay->controlPoints) {
      known.push_back({point.templateName, point.position});
    }
    for (const auto& spawner : gameplay->spawners) {
      known.push_back({spawner.templateName, spawner.position});
      // A spawner issues different vehicles depending on the team — every variant
      // stands in the same place.
      for (const auto& [team, name] : spawner.templateByTeam) {
        (void)team;
        known.push_back({name, spawner.position});
      }
    }
  }
  // The statics too: the server also sends destructible things such as barrels and tankers.
  if (const auto loaded = obf2::level::loadLevel(files, levelName, error)) {
    for (const auto& object : loaded->objects) {
      known.push_back({object.templateName, object.position});
    }
  }
  return known;
}

std::optional<ContentHashes> contentHashes(obf2::FileSystem& files, const std::string& levelName,
                                           int ordinal) {
  ContentHashes out;


  // The first hash: an MD5 over four of the mod's files, in exactly this order.
  // The names come from `ChecksumContext::runMiscChecksum`.
  obf2::net::Md5 misc;
  for (const char* name : {"ClientArchives.con", "ServerArchives.con", "Init.con",
                           "GameLogicInit.con"}) {
    const auto data = files.read(name);
    if (!data) return std::nullopt;
    misc.update(*data);
  }
  out.misc = misc.finish();

  const auto readText = [&files](const std::string& path) -> std::string {
    const auto data = files.read(path);
    if (!data) return {};
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
  };

  const std::string archivesText = readText("std_archive.md5");
  const std::string levelText = readText("levels/" + levelName + "/archive.md5");
  const auto archives = obf2::net::bf2::readFingerprint(archivesText, ordinal);
  const auto level = obf2::net::bf2::readFingerprint(levelText, ordinal);
  if (!archives || !level) return std::nullopt;
  out.archives = *archives;
  out.level = *level;
  return out;
}

}  // namespace obf2::session
