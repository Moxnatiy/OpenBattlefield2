#pragma once
// What a connection to a live server takes from the command line, the probe
// mode, and the drawability check the session asks for by name.
#include <string>

#include "obf2/app/command_line.h"
#include "obf2/game/object_mesh.h"
#include "obf2/session/remote_world.h"

namespace obf2 {
class FileSystem;
}

namespace obf2::app {

// The session's settings, out of everything `Args` holds.
obf2::session::Settings sessionSettings(const Args& args);

// `--probe`: connect, listen for `--frames` turns (90 by default), report. No
// window: an instrument for taking the protocol apart.
int runProbe(const Args& args, obf2::FileSystem& files);

// `game::checkDrawable` with the command line's geometry and lod overrides.
obf2::game::DrawStage checkDrawable(obf2::FileSystem& files, const obf2::game::Registry& registry,
                                    const std::string& templateName, const Args& args);

}  // namespace obf2::app
