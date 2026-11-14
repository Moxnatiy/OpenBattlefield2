#include "obf2/session/remote_world.h"

#include <cctype>
#include <set>

#include "obf2/core/parallel.h"
#include "obf2/core/path.h"
#include "obf2/level/gameplay.h"

namespace obf2::session {

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

std::vector<KnownObject> buildKnownObjects(obf2::FileSystem& files, const std::string& levelName,
                                           std::string* error) {
  std::vector<KnownObject> known;
  if (const auto gameplay =
          obf2::level::loadGameplayObjects(files, levelName, "gpm_cq", 16, error)) {
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
