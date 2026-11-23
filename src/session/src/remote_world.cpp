#include "obf2/session/remote_world.h"

#include <cctype>

#include <set>


#include "obf2/game/scene.h"

#include "obf2/core/parallel.h"
#include "obf2/core/path.h"
#include "obf2/level/gameplay.h"

namespace obf2::session {

namespace bf2 = net::bf2;

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
