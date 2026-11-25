# Radio, the commander and votes on a live server

What `session::RemoteWorld` does with the round's radio messages, the commander's
HUD events and the server's votes. The marks these leave on the map are in
hud-map.md ("Timed marks on objects"); the mute is in voip-mute.md.

## Radio messages

* The table is the game's data: `Common/Sound/VoiceMessages.con`, read once
  (`loadRadioMessages`). `gameLogic.messages.addMessage <name>` opens an entry,
  the first `addRadioVoice` after it gives its caption (argument 2), its sample
  (argument 1, up to the first comma) and, when present, the sample heard by the
  sender himself (argument 3) with its flags (argument 4).
  `gameLogic.messages.addHelpMessage <name> <sample>` fills the help voice-overs.
* A `RadioMessageEvent` names the entry by its index in that table.
* A teammate's message (not our own) marks the speaker's object on the map, or his
  vehicle when seated: kind 1, 12 s, no flash. The path is 0x4441d0 → channel
  message type 3 (0x444702, 0x4c515b) → 0x440c00 case 3, which posts HUD event 60.
  The map's case 0x3c (0x782990) adds the mark.
* A teammate's message also goes to the chat, as the entry's caption.
* Its sample is queued in `radioSamples` for the application to play.
  * The folder is `Common/Sound/<army>/`. The folders are the data's (Chinese,
    MEC, EU, English). Which army speaks from which is taken from their names,
    **not read** in the binary.
  * Flag 2 plays the local sample even for a teammate. Flag 1 plays it for an
    enemy's message too.
* The help voice-over (`helpVoicePath`) is `common/sound/<GSLanguage>/<sample>`
  (0x4edb6c..0x4edc6c). It falls back to `English` when
  `<language>/Help_VO/HUD_HELP_BATTLERECORDER_controlsQ.ogg` is missing
  (0x4edc49, 0x4edc64).

## Specialist calls

HUD events 46..50 (`ECHudHECallForMedic` … `ECHudHEAutoCallForRevive`). The
server forwards a `SpottedEvent` as remote event (2, n) with the caller's object
id in the first two bytes (Linux `SpottedEvent::executeServer` 0x430321).

* A teammate's call marks him (kind 4) when our kit can answer it.
* Our own call marks the teammates who can answer it.

The details are in hud-map.md.

## The commander

| HUD event | what we do | source |
|---|---|---|
| 33 `ECHudHECommanderAccept` (8 bytes, our player) | the vehicle drop's allocation, the float at +4 | `hudEventName`; the float's meaning inferred |
| 34 `ECHudHECommanderDecline` | the offer ends | `hudEventName` |
| 35 `ECHudHECommanderInvitation` | the offer, when its value is our player | `hudEventName` |
| 40 (12 bytes) | a UAV at the position (three floats) | map case 0x28 (0x782990) |
| 41 (empty) | the satellite scan: active 11 s, reload 16 s | case 0x29; `getSatelliteScanActiveTime` / `ReloadTime`, Linux 0x47782b, 0x477837 |
| 42 (16 bytes) | artillery at the position; byte 12 = 0 is a refusal | the byte's meaning inferred |
| 74 (20 bytes) | a vehicle drop at +8, the allocation at +4 | inferred from the layout |
| 75 (4 bytes) | the vehicle drop's allocation | inferred |
| 83 (12 bytes) | a supply drop at the position | inferred |

**Not measured:**

* how long an asset's mark lasts: 30 s for the UAV, 20 s for the rest;
* the 60 s reload we hold on an asset;
* the 30 m that ties a dispatch to the squad request it answers;
* the 10 s we wait for that dispatch;
* the overhead camera's height before the server places it: a quarter of the
  map, at least 100 m.

The client's own timers for these are not read.

What we send:

* `CommanderEvent` (apply, accept, decline, resign, the camera modes);
* `CommanderCamEvent`;
* the commander's tools as `IssueSquadOrderEvent`s;
* the support reply: an order response and a radio message, `*_accept` /
  `*_deny` from the same table.

## Votes

`VoteEvent`: issue, update (the counts), passed, failed, cancelled, and the
server's three refusals.

* We keep the open map vote and the open kick vote (`mapVote`, `kickVote`).
* What the original's HUD shows for a vote is **not read**. The vote is logged.
* The scoreboard's manage page starts a kick vote on a row, or votes yes or no
  on the open one (columns 7 and 8, hud-scoreboard.md).
* Five minutes between our own votes: **not measured**. The server's own
  limit is not read.

## `communicate`

The radio, the commander and the votes take a line of words:

* `vote map|kick …`;
* `commander apply|accept|decline|resign|status|camera|soldier|requests|approve|deny|<tool>`;
* `all|team|squad <text>`;
* `switch <team>`;
* `accept|decline` (a commander order to our squad);
* `radio <call>`;
* `spot`, `order <kind>`, `request <asset>`.

The comm roses (`Squad.con`, `SquadLeader.con`) say which radio message each
call sends. Each call's channel is written beside it.

What the engine picks a spot or an order with (manager slot 0x14c, 1000 m for
an order) is not ported. Ours, **not measured**:

* the collision world's first contact, then the terrain in 2 m steps;
* a 350 m reach for a spot;
* a target sphere of 0.95 m over a soldier's pivot (radius 0.85), or 0.8 m
  over a vehicle's (radius 2).

Tests: `test_session` (`testVotesAndMute`, `testCommanderOrderAcceptAndDecline`,
`testCommanderSupportWaitsForServerDispatch`).
