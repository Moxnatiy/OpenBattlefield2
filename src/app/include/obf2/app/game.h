#pragma once
// One session of the game: the scene, the window, the HUD and the frame loop.
// main.cpp chooses the mode and runs sessions one after another; this is what
// each of them does.
#include "obf2/app/command_line.h"
#include "obf2/app/render_context.h"

namespace obf2 {
class FileSystem;
namespace session {
struct RemoteWorld;
}
}  // namespace obf2

namespace obf2::app {

// `shared`: the window and renderer of the whole run. `next`: where a level
// chosen in the menu is handed back, null when there is no menu. `remote`: the
// live server's session for `--connect`, null otherwise. Returns the process's
// exit code.
int runSession(const Args& args, obf2::FileSystem& files, Args* next,
               obf2::session::RemoteWorld* remote, RenderContext& shared);

}  // namespace obf2::app
