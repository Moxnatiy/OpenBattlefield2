// What a connection to a live server takes from the command line, the probe
// mode (`--probe`: connect and report, no window) and the drawability check the
// session asks for. Moved here from main.cpp unchanged.
#include "obf2/app/launch.h"

#include "obf2/game/object_mesh.h"
#include "obf2/session/remote_world.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// Where an object is lost on its way to the screen (`obf2/game/object_mesh.h`).
// The session asks for it by name; the two numbers the command line may override
// are applied here rather than at every call.
obf2::game::DrawStage checkDrawable(obf2::FileSystem& files, const obf2::game::Registry& registry,
                                    const std::string& templateName, const Args& args) {
  return obf2::game::checkDrawable(files, registry, templateName, args.geometryIndex,
                                   args.lodIndex);
}

// The three hashes for the content check.
//
// The first is the one the server computes itself at startup; we cannot compute it
// yet, so it is passed with the `--misc-hash` flag. The second and third are read
// from the fingerprint files: the mod's archives and the level itself.

// Text mode: connect, spin for a while and report what arrived.
// What the session takes from the command line, out of everything `Args` holds.
obf2::session::Settings sessionSettings(const Args& args) {
  obf2::session::Settings settings;
  settings.connectTo = args.connectTo;
  settings.connectPassword = args.connectPassword;
  settings.playerName = args.playerName;
  settings.modDir = args.modDir;
  settings.recordTo = args.recordTo;
  settings.ordinal = args.ordinal;
  settings.blockReady = args.blockReady;
  settings.skipContent = args.skipContent;
  settings.skipDatabase = args.skipDatabase;
  settings.startSimulation = args.startSimulation;
  settings.traceOwnState = args.traceOwnState;
  return settings;
}

// There is deliberately no window here — this is an instrument for taking the protocol apart.
int runProbe(const Args& args, obf2::FileSystem& files) {
  obf2::session::RemoteWorld remote(sessionSettings(args), files);
  remote.drawabilityOf = [&](const std::string& name) {
    return checkDrawable(files, remote.registry, name, args);
  };
  if (!remote.connect()) return 1;
  // In a probe there is nothing to load: no window, no scene.
  remote.join.setClientLoaded();
  // There is no spawn screen either, so the choice comes from the command line, and
  // we make it at once.
  remote.askSpawnGroup(args.team, args.kit, args.spawnGroup);
  // --frames here says how many turns to listen for: thirty is enough for short
  // experiments (whether the server breaks us off on the content check).
  const int loops = args.frames > 0 ? args.frames : 90;
  for (int i = 0; i < loops; ++i) remote.pump(500);
  remote.report();
  remote.disconnect();
  return 0;
}

}  // namespace obf2::app
