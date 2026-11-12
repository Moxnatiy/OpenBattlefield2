#pragma once
// The bridge the menu movie talks to the engine through: SwiffPlayer.dll's
// seventeen objects (`_global.Logic`, `_global.dice.bf2.Logic`, …), the
// settings objects, `dice.General` and `mx.lang.Locale`
// (docs/functions/menu-bridge.md). Moved over from tools/ruffle_bf2_bridge.rs;
// the readings and their addresses are the same.
#include <string>
#include <vector>

#include "obf2/swf/player.h"

namespace obf2::swf {

// Put the bridge into gameswf's `_global`. `host` and `commands` must outlive
// the movie.
void installBridge(const Host* host, std::vector<std::string>* commands);

// Hand out the captions `mx.lang.Locale.addDelayedInstance` deferred — after a
// frame, once the components have written their keys (bridge.cpp).
void flushDelayedCaptions();

}  // namespace obf2::swf
