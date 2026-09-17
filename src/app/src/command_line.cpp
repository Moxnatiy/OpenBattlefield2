#include "obf2/app/command_line.h"

#include <cstdio>
#include <cstdlib>

#include "obf2/con/interpreter.h"

namespace obf2::app {

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--mod" && i + 1 < argc) args.modDir = argv[++i];
    else if (flag == "--mesh" && i + 1 < argc) args.meshPath = argv[++i];
    else if (flag == "--object" && i + 1 < argc) args.objectName = argv[++i];
    else if (flag == "--level" && i + 1 < argc) args.levelName = argv[++i];
    else if (flag == "--geom" && i + 1 < argc) args.geometryIndex = std::atoi(argv[++i]);
    else if (flag == "--lod" && i + 1 < argc) args.lodIndex = std::atoi(argv[++i]);
    else if (flag == "--frames" && i + 1 < argc) args.frames = std::atoi(argv[++i]);
    else if (flag == "--screenshot" && i + 1 < argc) args.screenshot = argv[++i];
    else if (flag == "--screenshot-at" && i + 1 < argc) {
      // `<frame>:<file>` — a shot on a chosen frame, so several frames of the same
      // run can be laid side by side (a moving soldier, a turning vehicle).
      const std::string text = argv[++i];
      const std::size_t colon = text.find(':');
      if (colon == std::string::npos) {
        std::fprintf(stderr, "--screenshot-at expects <frame>:<file>, not %s\n", text.c_str());
        continue;
      }
      args.screenshotAt.push_back({std::atoi(text.substr(0, colon).c_str()),
                                   text.substr(colon + 1)});
    }
    else if (flag == "--screen" && i + 1 < argc) args.screen = argv[++i];
    else if (flag == "--hosted") args.hosted = true;
    else if (flag == "--topdown") args.topDown = true;
    else if (flag == "--no-hud") args.noHud = true;
    else if (flag == "--no-lightmaps") args.noLightmaps = true;
    else if (flag == "--camera" && i + 1 < argc) {
      obf2::con::Command command;
      command.args.emplace_back(argv[++i]);
      if (const auto point = command.argVec3(0)) {
        args.camera = obf2::Vec3f{point->x, point->y, point->z};
      }
    }
    else if (flag == "--collision-near" && i + 1 < argc) {
      obf2::con::Command command;
      command.args.emplace_back(argv[++i]);
      if (const auto point = command.argVec3(0)) {
        args.collisionNear = obf2::Vec3f{point->x, point->y, point->z};
      }
    }
    else if (flag == "--angles" && i + 2 < argc) {
      args.cameraYaw = static_cast<float>(std::atof(argv[++i]));
      args.cameraPitch = static_cast<float>(std::atof(argv[++i]));
    }
    else if (flag == "--own-box") args.showOwnBox = true;
    else if (flag == "--draw-predicted") args.drawPredicted = true;
    else if (flag == "--watch-soldier") args.watchSoldier = true;
    else if (flag == "--trace-own-state") args.traceOwnState = true;
    else if (flag == "--jump-at" && i + 1 < argc) args.jumps.push_back(std::atoi(argv[++i]));
    else if (flag == "--flash" && i + 1 < argc) args.flashSwf = argv[++i];
    else if (flag == "--connect" && i + 1 < argc) args.connectTo = argv[++i];
    else if (flag == "--probe") args.probe = true;
    else if (flag == "--no-content") args.skipContent = true;
    else if (flag == "--no-database") args.skipDatabase = true;
    else if (flag == "--start-sim") args.startSimulation = true;
    else if (flag == "--block-ready") args.blockReady = true;
    else if (flag == "--width" && i + 1 < argc) args.width = std::atoi(argv[++i]);
    else if (flag == "--height" && i + 1 < argc) args.height = std::atoi(argv[++i]);
    else if (flag == "--hud-screen" && i + 1 < argc) args.hudScreenName = argv[++i];
    else if (flag == "--hud-rects") args.hudRects = true;
    else if (flag == "--hud-vars") args.hudVars = true;
    else if (flag == "--connect-password" && i + 1 < argc) args.connectPassword = argv[++i];
    else if (flag == "--name" && i + 1 < argc) args.playerName = argv[++i];
    else if (flag == "--calibrate" && i + 1 < argc) args.calibrate = argv[++i];
    else if (flag == "--ordinal" && i + 1 < argc) args.ordinal = std::atoi(argv[++i]);
    else if (flag == "--record" && i + 1 < argc) args.recordTo = argv[++i];
    else if (flag == "--exec" && i + 1 < argc) args.execLines.emplace_back(argv[++i]);
    else if (flag == "--mouse-scale" && i + 1 < argc) args.mouseScale = std::atof(argv[++i]);
    else if (flag == "--team" && i + 1 < argc) args.team = std::atoi(argv[++i]);
    else if (flag == "--kit" && i + 1 < argc) args.kit = std::atoi(argv[++i]);
    else if (flag == "--group" && i + 1 < argc) {
      args.spawnGroup = std::atoi(argv[++i]);
      args.spawnGroupGiven = true;
    }
    else if (flag == "--anim" && i + 1 < argc) args.animationPaths.emplace_back(argv[++i]);
    else if (flag == "--skeleton" && i + 1 < argc) args.skeletonPath = argv[++i];
    else if (flag == "--frame" && i + 1 < argc) args.frame = std::atoi(argv[++i]);
    else if (flag == "--click") args.click = true;
    else if (flag == "--exec-at" && i + 1 < argc) {
      const std::string text = argv[++i];
      const std::size_t colon = text.find(':');
      if (colon == std::string::npos) {
        std::fprintf(stderr, "--exec-at expects <frame>:<console line>, not %s\n", text.c_str());
        continue;
      }
      args.scheduledLines.push_back({std::atoi(text.substr(0, colon).c_str()), text.substr(colon + 1)});
    }
    else if (flag == "--trace-frames" && i + 1 < argc) {
      std::sscanf(argv[++i], "%d:%d", &args.traceFrom, &args.traceFrames);
    }
    else if (flag == "--look-at" && i + 1 < argc) {
      Args::ScheduledLook look{};
      if (std::sscanf(argv[++i], "%d:%d:%f:%f", &look.frame, &look.frames, &look.dx, &look.dy) != 4) {
        std::fprintf(stderr, "--look-at expects <frame>:<frames>:<dx>:<dy>, not %s\n", argv[i]);
        continue;
      }
      args.looks.push_back(look);
    }
    else if (flag == "--move-at" && i + 1 < argc) {
      Args::ScheduledLook move{};
      if (std::sscanf(argv[++i], "%d:%d:%f:%f:%d", &move.frame, &move.frames, &move.dx, &move.dy,
                      &move.sprint) < 4) {
        std::fprintf(stderr, "--move-at expects <frame>:<frames>:<forward>:<right>[:<sprint>], not %s\n",
                     argv[i]);
        continue;
      }
      args.moves.push_back(move);
    }
    else if (flag == "--click-at" && i + 1 < argc) {
      // "frame:x:y" — three numbers separated by colons.
      const std::string text = argv[++i];
      const std::size_t first = text.find(':');
      const std::size_t second = text.find(':', first == std::string::npos ? 0 : first + 1);
      if (first == std::string::npos || second == std::string::npos) {
        std::fprintf(stderr, "--click-at expects <frame>:<x>:<y>, not %s\n", text.c_str());
        continue;
      }
      args.clicks.push_back({std::atoi(text.substr(0, first).c_str()),
                             static_cast<float>(std::atof(text.substr(first + 1, second - first - 1).c_str())),
                             static_cast<float>(std::atof(text.substr(second + 1).c_str()))});
    }
    else if (flag == "--mouse" && i + 2 < argc) {
      args.mouseX = static_cast<float>(std::atof(argv[++i]));
      args.mouseY = static_cast<float>(std::atof(argv[++i]));
    }
    else if (flag == "--dist" && i + 1 < argc) args.distance = static_cast<float>(std::atof(argv[++i]));
    else if (flag == "--focus" && i + 1 < argc) {
      // The format is the game's: x/y/z
      obf2::con::Command command;
      command.args.emplace_back(argv[++i]);
      if (const auto point = command.argVec3(0)) {
        args.focus = obf2::Vec3f{point->x, point->y, point->z};
      }
    }
  }
  return args;
}

}  // namespace obf2::app
