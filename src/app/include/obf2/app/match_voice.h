#pragma once
// The voice of a live round: the relay the server's match options name
// (`ServerMatchOptions::voice*`), the session id it hands us (`VoipSessionEvent`),
// our own voice on push to talk and everyone else's we have not muted
// (docs/functions/voice-transport.md, docs/functions/voip-mute.md).
//
// The keys are the game's own: `c_GIVoipPushToTalk` on B and
// `c_GIVoipUseLeaderChannel` on V (`mods/bf2/Settings/Controls.con`, lines 245
// and 239) — B talks to the squad, V from a squad leader to the commander.
#include <memory>

namespace obf2::session {
struct RemoteWorld;
}

namespace obf2::app {

class MatchVoice {
 public:
  MatchVoice();
  ~MatchVoice();

  // Once a frame: `squadTalk` and `commandTalk` are the two keys held,
  // `focused` whether the window has the keyboard (no capture and no playback
  // otherwise).
  void update(session::RemoteWorld& match, bool squadTalk, bool commandTalk, bool focused);
  // Tells the server our voice is off and closes the relay.
  void stop(session::RemoteWorld& match);
  bool available() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace obf2::app
