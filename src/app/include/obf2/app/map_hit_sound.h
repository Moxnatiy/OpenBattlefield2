#pragma once

#include "obf2/app/menu_music.h"
#include "obf2/hud/manager.h"
#include "obf2/session/remote_world.h"

namespace obf2::app {

// One map frame: `RemoteWorld::stepMap` with the map's drawn width +0xb64
// (`hud::Manager::satelliteDrawnWidth`) and the world's size, then the kind-2
// sound. 0x782580 and 0x782710 ask the HUD sound player (0xa23dc0 vtbl +0x2c)
// for `HUD_Satellite_target` after every add, kept or not:
// `Common/Sound/Hud/Satellite_target.wav`, 2D, volume 0.42
// (`Sound/GameSounds.con`, `Sound.addSound HUD_Satellite_target`). One stream
// here, so several asks in a frame play it once.
inline void stepMap(session::RemoteWorld& remote, const hud::Manager& hud, MenuMusic& audio,
                    float effectsVolume, float dt) {
  constexpr float kVolume = 0.42f;  // Sound/GameSounds.con, ObjectTemplate.volume
  if (remote.stepMap(dt, hud.satelliteDrawnWidth(), hud.mapWorldSize()) > 0) {
    audio.open(remote.files, "Common/Sound/Hud/Satellite_target.wav", kVolume * effectsVolume,
               false);
  }
}

}  // namespace obf2::app
