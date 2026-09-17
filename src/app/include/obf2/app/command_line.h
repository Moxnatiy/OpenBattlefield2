#pragma once
// The command line: what the run is, and every switch the measurements use.
//
// The engine reads its own arguments in `Code/BF2/Game/Main/BF2EngineSetup.cpp`
// and turns them into settings before anything is loaded; this is that step.
// Most of the flags below are measuring instruments — a camera put where the
// original's is, a click on a schedule, a turn of a known size — and each says
// next to it what it is for.
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "obf2/core/math.h"

namespace obf2::app {

struct Args {
  std::filesystem::path modDir = "Game Files/mods/bf2";
  std::string meshPath;  // empty -> an ordinary engine run
  std::string objectName;
  std::string levelName;
  int geometryIndex = -1;  // -1 = choose by the number of lods
  int lodIndex = 0;
  int frames = 0;  // 0 = run until the window is closed
  std::string screenshot;
  // --screenshot-at <frame>:<file>, as many as asked for.
  std::vector<std::pair<int, std::string>> screenshotAt;
  std::string screen;  // menu | loading — for deterministic screenshots
  bool hosted = false; // --hosted: the world comes from a local server
  std::optional<obf2::Vec3f> focus;  // where the camera looks
  float mouseX = -1.0f, mouseY = -1.0f;  // --mouse: place the cursor for a screenshot
  // --topdown: strictly from above, +X to the right, -Z up. Needed to check the
  // world's orientation against the level's own minimap.
  bool topDown = false;
  // A camera put where we say and pointed where we say, and a way to take the
  // interface off the picture. Both are for looking: comparing a frame of ours
  // against the original means standing in the same spot, and the HUD covers
  // most of what is being compared. `tools/bf2_run.sh` puts the original at the
  // same place.
  std::optional<obf2::Vec3f> camera;
  // --collision-near x/y/z: the placed objects within 15 m of a point, with the
  // collision mesh each one's template and children name and what of it loaded —
  // the measure for "the server stops us at a wall our world does not have".
  std::optional<obf2::Vec3f> collisionNear;
  float cameraYaw = 0.0f;    // degrees; 0 looks along +Z, as the engine counts
  float cameraPitch = 0.0f;  // degrees; positive is up
  bool noHud = false;
  // --no-lightmaps: draw without the levels' baked light maps. For telling a
  // wrong light map apart from a dark one — the two look alike on screen and
  // only an A/B says which.
  bool noLightmaps = false;
  // --own-box: draw a placeholder at our own soldier's position too. It is not
  // needed in the game itself, but without it the other players' placeholder
  // cannot be checked on an empty server.
  bool showOwnBox = false;
  // --draw-predicted: draw other soldiers from the predicted point, as before
  // `carryRemoteSoldier`. A measuring switch — for holding the two side by side,
  // not a way to play.
  bool drawPredicted = false;
  // --watch-soldier: the camera looks at the nearest other soldier (ours, for
  // screenshots of how other players are drawn).
  bool watchSoldier = false;
  // --mouse-scale: how many axis units one mouse pixel gives. The link
  // "pixels -> axis" lives in the client's `ControlMap` and is **not reversed
  // yet**, so this number is NOT measured — it plays the role of sensitivity and
  // stays a setting. Everything after it (axis -> angle, axis -> wire) already
  // comes from the binary, so the camera and the server do not diverge whatever it is.
  float mouseScale = 0.02f;
  // --record <file>: save everything the server sent in the same format
  // `loadCapture` reads (u32 length, then the bytes). Captured traffic is the
  // only thing the protocol parsing can be checked against by a test.
  std::string recordTo;
  // --exec "<line>": run a console command as soon as the spawn screen is ready.
  // This screen's buttons have no logic of their own anyway — they run console
  // commands (`setButtonNodeConCmd`), so this is the same path a click takes,
  // only without pointing the mouse at a pixel. The flag may be repeated.
  //
  std::vector<std::string> execLines;
  // --flash <file.swf>: show the menu's movie instead of our screen.
  // The game's menu is Flash, played by Ruffle (docs/research/11-ruffle-menu.md).
  std::string flashSwf;
  std::string connectTo;      // --connect <host[:port]>: a real BF2 server
  // --probe: protocol parsing only, without a window. Without it --connect opens
  // the world, as a client should.
  bool probe = false;
  // --no-content: skip the content check. Needed to find out whether it is what
  // makes the server break the connection.
  bool skipContent = false;
  bool skipDatabase = false;
  bool startSimulation = false;
  bool blockReady = false;
  std::string connectPassword;
  std::string playerName = "OpenBF2";  // --name: the name we join under
  std::string calibrate;               // --calibrate <file>: match template numbers
  // --ordinal: the line number in the fingerprint files. The server picks it when
  // it loads the level; where a real client learns it has not been found yet, so
  // for now we set it by hand.
  // -1 = take it from the block the server sends.
  int ordinal = -1;
  int team = 1;                        // --team, --kit, --group: the spawn choice
  int kit = 0;
  int spawnGroup = 1;
  // Whether `--group` was actually given. The number above has a default because
  // the headless probe has no spawn screen to choose with; in a windowed run the
  // screen is the chooser, and a default acting on its own asked to spawn as team
  // one at group one before the player had touched anything — and then DONE did
  // nothing, because the join chain was already spent.
  bool spawnGroupGiven = false;
  std::vector<std::string> animationPaths;  // --anim: several are allowed, they blend
  std::string skeletonPath;    // --skeleton: a .ske; the soldier's skeleton by default
  int frame = 0;               // --frame: which frame to show
  bool click = false;  // --click: one click at the --mouse position
  // --click-at <frame>:<x>:<y> — clicks on a schedule, several are allowed.
  // The menu leads the player through several steps (pick a profile -> log in),
  // and one click is not enough to check it.
  struct ScheduledClick {
    int frame;
    float x, y;
  };
  // --exec-at <frame>:<console line> — a console command on a schedule, several
  // are allowed. What `--click-at` is for the menu this is for a live server: a
  // spawn, a suicide and a respawn in one unattended run, so a bug that only
  // shows on the second life can be measured without a person at the keyboard.
  struct ScheduledLine {
    int frame;
    std::string line;
  };
  std::vector<ScheduledLine> scheduledLines;
  std::vector<ScheduledClick> clicks;
  // --look-at <frame>:<frames>:<dx>:<dy> — mouse pixels added on every frame of a
  // range. A turn of known size without a hand on the mouse: what the server's
  // own yaw says afterwards is the measure of the look chain.
  struct ScheduledLook {
    int frame;
    int frames;
    float dx, dy;
    int sprint = 0;  // --move-at only
  };
  std::vector<ScheduledLook> looks;
  // --move-at <frame>:<frames>:<forward>:<right>[:<sprint>] — the movement keys held
  // over a range, as -1..1, and sprint held when the last number is 1. The same idea
  // for running: where the server puts us after it.
  std::vector<ScheduledLook> moves;
  // --jump-at <frame> — the jump key pressed for that one frame.
  std::vector<int> jumps;
  // --trace-own-state: every controlled state of our soldier (session::Settings).
  bool traceOwnState = false;
  // --trace-frames <frame>:<frames> — per frame, the camera and every other
  // soldier drawn: the measure for "the camera jerks, the box hops".
  int traceFrom = -1;
  int traceFrames = 0;
  // --hud-screen <group>: show a screen normally visible only while a key is
  // held. Needed for screenshots and for checking by eye.
  std::string hudScreenName;
  // --hud-rects: write out the rectangles of every node drawn. The format is the
  // same as in the original's frame dump, so they can be compared
  // (tools/hud_coverage.py).
  bool hudRects = false;
  // --hud-vars: list every variable the HUD's tree asks for and say which of them
  // nobody fills in. A name nobody writes is a node that never appears, and it
  // fails silently — so the debt is printed as a list.
  bool hudVars = false;
  float distance = 0.0f;             // 0 = choose it from the bounds
  // The game is made for 4:3, and for now we keep to that: 1600x1200 is exactly
  // twice the base 800x600, so the HUD lands with nothing left over.
  // A wide screen will be a separate job.
  int width = 1600;
  int height = 1200;
};


// Unknown flags are ignored and a malformed value is reported on stderr: a run
// with one bad switch still measures what the others asked for.
Args parseArgs(int argc, char** argv);

}  // namespace obf2::app
