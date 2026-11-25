#include <cmath>
#include <cstdio>

#include "check.h"
#include "obf2/session/remote_world.h"

using namespace obf2;

// The map node's timed marks (BF2.exe 0x781ff0, 0x77a900, 0x77a9a0) and its
// per-frame spin and satellite scan (0x77b280, 0x772680); docs/functions/hud-map.md.
int main() {
  FileSystem files;
  session::RemoteWorld remote({}, files);
  const auto at = [&](float seconds) {
    remote.world.setGameTick(static_cast<std::uint32_t>(std::lround(seconds / server::kTickTime)));
  };

  // One mark per object: the second add for the same id is dropped.
  at(0.0f);
  remote.addMapMark(7, 1, 12.0f, 0.0f);
  remote.addMapMark(7, 4, 12.0f, 0.0f, "x.tga");
  CHECK(remote.mapMarkOf(7) && remote.mapMarkOf(7)->kind == 1);
  at(11.9f);
  CHECK(remote.mapMarkOf(7) != nullptr);
  at(12.1f);
  CHECK(remote.mapMarkOf(7) == nullptr);
  // Once spent, the object takes a new mark.
  remote.addMapMark(7, 4, 12.0f, 0.0f, "x.tga");
  CHECK(remote.mapMarkOf(7) && remote.mapMarkOf(7)->kind == 4);

  // The flash runs first at full fade, then the duration fades out.
  at(100.0f);
  remote.addMapMark(9, 2, 0.5f, 0.3f);
  const auto* mark = remote.mapMarkOf(9);
  CHECK(mark && remote.mapMarkFade(*mark) == 1.0f);
  at(100.2f);
  CHECK(remote.mapMarkFade(*remote.mapMarkOf(9)) == 1.0f);
  at(100.55f);
  const float fade = remote.mapMarkFade(*remote.mapMarkOf(9));
  CHECK(fade > 0.4f && fade < 0.6f);
  at(100.9f);
  CHECK(remote.mapMarkOf(9) == nullptr);

  // The node's spin +0xabc: dt × 4 a frame, back to 0 past 2π (0x77b280).
  remote.mapSpin = 0.0f;
  remote.stepMap(1.0f, std::nullopt, 1024.0f);
  CHECK(std::fabs(remote.mapSpin - 4.0f) < 1e-5f && std::fabs(remote.mapSpinStep - 4.0f) < 1e-5f);
  remote.stepMap(1.0f, std::nullopt, 1024.0f);
  CHECK(remote.mapSpin == 0.0f);

  // The satellite scan (case 0x29, 0x772680): started by HUD event 41 only once
  // the reload is spent; the line runs (150 + w) / 11 per second.
  net::bf2::RemoteEvent scan;
  scan.category = 2;
  scan.number = 41;
  remote.receiveCommanderHud(scan);
  CHECK(remote.satelliteScan.on && remote.satelliteScan.active == 11.0f &&
        remote.satelliteScan.reload == 16.0f);
  remote.stepMap(1.0f, 150.0f, 1024.0f);
  CHECK(std::fabs(remote.satelliteScan.line - 300.0f / 11.0f) < 1e-3f);
  for (int i = 0; i < 11; ++i) remote.stepMap(1.0f, 150.0f, 1024.0f);
  CHECK(!remote.satelliteScan.on);
  remote.satelliteScan.reload = 5.0f;
  remote.receiveCommanderHud(scan);
  CHECK(!remote.satelliteScan.on);  // still reloading
  for (int i = 0; i < 6; ++i) remote.stepMap(1.0f, 150.0f, 1024.0f);
  CHECK(remote.satelliteScan.reload == 0.0f);
  remote.receiveCommanderHud(scan);
  CHECK(remote.satelliteScan.on && remote.satelliteScan.elapsed == 0.0f);
  if (obf2test::g_failures != 0) return 1;
  std::puts("OK");
  return 0;
}
