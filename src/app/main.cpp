// openbf2 — the engine's entry point.
//
//   openbf2 [--mod <path>] [--mesh <path in the VFS>]  — one mesh
//           [--object <template name>]                 — an assembled vehicle
//           [--level <level name>]                     — a whole level
//           [--geom N] [--lod N] [--frames N] [--screenshot file.bmp]
//
// The game's data is read straight out of the archives, as fileManager does in Refractor 2.
//
// This file is the arguments, the archives and the choice of mode. A session
// (the scene, the window, the HUD and the frame loop) is `runSession`
// (obf2/app/game.h); the live server's connection helpers are obf2/app/launch.h.
#include <cstdio>
#include <string>
#include <vector>

#include "obf2/app/calibrate.h"
#include "obf2/app/command_line.h"
#include "obf2/app/game.h"
#include "obf2/app/launch.h"
#include "obf2/app/render_context.h"
#include "obf2/core/math.h"
#include "obf2/core/platform.h"
#include "obf2/session/remote_world.h"
#include "obf2/vfs/filesystem.h"

namespace {

using obf2::app::Args;
using obf2::app::checkDrawable;
using obf2::app::parseArgs;
using obf2::app::runProbe;
using obf2::app::runSession;
using obf2::app::sessionSettings;
using obf2::session::RemoteWorld;

}  // namespace

int main(int argc, char** argv) {
  // Line by line, whatever stdout is: a log piped through a timestamper has to
  // show when each line was written, not when a 4 KB buffer filled.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  Args args = parseArgs(argc, argv);

  obf2::FileSystem files;
  std::vector<std::string> mountErrors;
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += obf2::mountArchivesFromCon(files, args.modDir, args.modDir / list, &mountErrors);
  }
  files.mountDirectory(args.modDir);

  std::printf("OpenBattlefield2 | %s/%s\n", OBF2_PLATFORM_NAME, OBF2_ARCH_NAME);
  std::printf("mod: %s | archives: %d\n", args.modDir.string().c_str(), mounted);
  for (const auto& e : mountErrors) std::printf("  [mount] %s\n", e.c_str());

  if (!args.calibrate.empty()) return obf2::app::runCalibrate(args, files);

  // Connecting to a real server is a mode of its own: neither a window nor a level
  // is needed here.
  if (args.probe) return runProbe(args, files);

  // Playing on a real server: first we carry the handshake to the point where the
  // server says which level it is playing, and only then open the window — the level
  // is assigned by it, not by us.
  if (!args.connectTo.empty()) {
    RemoteWorld remote(sessionSettings(args), files);
    for (const int id : args.traceTails) remote.world.traceTails(static_cast<std::uint16_t>(id));
    remote.drawabilityOf = [&](const std::string& name) {
      return checkDrawable(files, remote.registry, name, args);
    };
    if (!remote.connect()) return 1;
    for (int i = 0; i < 40 && !remote.levelReady; ++i) remote.pump(500);
    if (!remote.levelReady) {
      std::fprintf(stderr, "the server did not say which level it is playing\n");
      remote.disconnect();
      return 1;
    }
    args.levelName = remote.levelName;

    // Wait for at least a few objects: the server sends them right after the level,
    // and the very first flag says where to look. Otherwise the camera stands where
    // we put it ourselves — that is, nowhere.
    for (int i = 0; i < 20 && remote.objects.size() < 4; ++i) remote.pump(200);
    if (!args.focus && !remote.objects.empty()) {
      const obf2::Vec3f at = remote.objects.begin()->second;
      std::printf("camera: next to object %u (%.0f %.0f %.0f)\n",
                  remote.objects.begin()->first, at.x, at.y, at.z);
      args.focus = at;
      if (args.distance <= 0.0f) args.distance = 60.0f;
    }

    obf2::app::RenderContext shared;
    int code = runSession(args, files, nullptr, &remote, shared);
    // A map change ends a session on the old level (`RemoteWorld::leaveLevel`);
    // the next one opens on the level the server names now.
    while (code == 0 && remote.levelChanged && remote.socket) {
      remote.levelChanged = false;
      for (int i = 0; i < 120 && !remote.levelReady && remote.socket; ++i) remote.pump(500);
      if (!remote.levelReady) {
        std::fprintf(stderr, "the server did not say which level it plays next\n");
        code = 1;
        break;
      }
      std::printf("map change -> %s\n", remote.levelName.c_str());
      args.levelName = remote.levelName;
      args.focus.reset();
      code = runSession(args, files, nullptr, &remote, shared);
    }
    remote.report();
    remote.disconnect();
    return code;
  }

  // The menu and the game are two sessions in a row: a level chosen in the menu
  // simply starts the next one with different arguments.
  obf2::app::RenderContext shared;  // one window for the whole run
  while (true) {
    Args next;
    const int code = runSession(args, files, &next, nullptr, shared);
    if (code != 0 || next.levelName.empty()) return code;

    std::printf("menu -> game: %s %s %d\n", next.levelName.c_str(), next.loadingMode.c_str(),
                next.loadingSize);
    args = next;
  }
}
