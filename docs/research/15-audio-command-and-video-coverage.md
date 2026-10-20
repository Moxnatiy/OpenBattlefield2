# Audio, commander and squad UI, and Bink coverage

This note compares the current implementation with additional Battlefield 2
asset, client, and server research. It is an implementation aid, not a claim
that OpenBattlefield2 already provides these features. The audio and HUD
observations below come from a separate native-port research effort; its Rust
implementation and independent network protocol are not drop-in code for this
C++ engine. No game assets, recordings, captures, or machine-specific details
are included.

## Current OpenBattlefield2 coverage

| Area | Existing work | Remaining gap |
|---|---|---|
| Audio | The engine reads profile audio settings; sound commands are inventoried in the HUD and data notes. | The HUD's [`sound.playSound` handler](../../src/hud/src/manager.cpp) is a no-op, and the technical-debt list says `Sound.con` commands such as `objecttemplate.soundFilename` have no playback handler. There is no implemented game audio path yet. |
| Commander and squads | The player and spawn records expose squad and commander state. The HUD map knows the commander layout mode, and the button-command inventory already lists `Commander`, `SquadMenu`, `SquadLeader`, and their interface commands. | Full commander and squad panels, their command dispatch, and the server-backed interactions are unfinished. Existing map placement and parsed roster state are useful foundations, not complete screens. |
| Intro movies | Startup follows the original movie sequence and the Meme class inventory records `BfBinkNode` fields. | The `.bik` video is not decoded; durations are nominal. The existing notes preserve playback order but explicitly do not reproduce Bink. |

See the current [audio debt](../TODO.md), [HUD command inventory](../functions/hud-commands.md), [commander map behavior](../functions/hud-map.md), [Meme classes](../formats/meme-classes.md), and [startup sequence](03-startup-and-menu.md).

## Audio authoring and useful coverage targets

An independent scan of the stock data found separate game and menu sound
registries (50 game registrations and 29 menu registrations in the inspected
set). The data also distinguishes local voice samples from radio samples.
Those counts describe the inspected stock data, not every mod or expansion.

Sound tweaks author volume, pitch, minimum distance, half-volume distance,
reverb level, and sound radius. For example, the inspected M4 definition sets
first-person fire to a non-spatial sample at volume 0.71, while third-person
fire is spatial at volume 1 with a 2 m minimum and 4 m half-volume distance.
These values describe authored settings; they do not recover the retail
mixer's rolloff, voice priority, channel limits, Doppler, or obstruction rules.

Useful sound classes to implement and validate separately:

- **Weapons:** first-person and third-person fire, optional outdoor fire,
  reload, and deployment sounds. The local shot can select an outdoor sample
  using a three-ray test; random variant weights and burst/loop timing remain
  unresolved. Mounted-weapon bindings also need independent coverage.
- **Vehicles:** authored engine layers, gear-change one-shots, occupied-seat
  ambience and damaged ambience, plus wheel or track contact loops selected
  by surface. A separate port has mapped authored engine layers for 30 stock
  vehicle profiles, but several sound inputs remain inferred or incomplete,
  including helicopter rotor behavior and some external-vehicle sounds.
- **Menu and music:** menu button cues are registered separately from game
  sounds. Menu music is named by the mod descriptor, level music by each map
  descriptor, and round-end cues have faction-specific names. A separate port
  loads `buttonPress`, `menuButtonOpen`, and `inputFieldPress` for activation,
  hover, and text-field input, respectively, and reads the first recording for
  each cue. It also reads the names requested by implemented in-game HUD
  buttons.
  Its front-end has rendered menu cues and music to PCM from retail data; that
  validates its data and callback path, not speaker output or acoustic parity
  or complete menu coverage.
- **Environment and impacts:** authored material impacts, per-level EAX
  presets, and reverb-send values provide concrete data for tests. The separate
  port uses an independent reverb DSP; it does not claim equivalence with the
  original audio library.

For this engine, a useful first implementation slice is to replace the
no-op HUD sound handler, read a named menu cue through the existing asset
reader, and emit it through a bounded mixer. Then add authored weapon and
vehicle bindings as separate steps. Keep each result labelled as authored
data, observed playback, inferred behavior, or unknown; data loading alone
does not establish retail sound behavior.

## Commander and squad screens

The retail HUD scripts expose enough structure to guide the missing panels.
The squad view contains rows for up to nine squads of six, commander/apply and
create entries, member lists, and join, leave, and lock controls. HUD states
3, 7, and 8 select the squad-member, squad-leader, and commander order roses.
The squad-leader rose offers Move, Attack, Destroy, Defend, and Repair orders,
alongside requests for commander tools. These are source/script observations;
they do not prove that every action is accepted by every server mode.

An independent stock-server audit found these useful native event layouts:

| Event | Body | Confirmed meaning |
|---|---|---|
| Commander (19) | kind: 4 bits, team: 8 bits, player: sign plus 15-bit magnitude | 0 accept, 1 decline, 2 resign, 3 server role update, 4 apply |
| Issue squad order (24) | squad: 8 bits, team: 8, order: 8, object-present: 1, object ID: 16 or nine 32-bit vector words, route: 8, accepted: 1 | Move 4, Attack 5, Defend 6; routes 0/1/2 are leader/commander/request |
| Set order acceptance (27) | squad: 8 bits, team: 8, accepted: 1, route: 8, order: 8 | Reports the acceptance state; it does not carry the target or a unique order ID |

For point orders, the native event contains three vector slots. A client must
preserve the server's vector serialization instead of assuming one contiguous
XYZ triple: a point `(x, y, z)` is written as `(x, 0, 0, y, 0, 0, z, 0, 0)`.
Commander role changes should be confirmed from replicated player state; an
Apply request or a queued response alone does not grant authority. Proposal
acceptance also depends on the current player identity and role.

The separate port has an original-script squad panel with a Commander/APPLY
row, create entry, member list, and join/leave/create actions. Static layout
comparison places the panel's layout and colors within about one pixel of
inspected retail frames. Lock toggles after creation, invite management,
double-click behavior, the scoreboard MANAGE tab, and commander map artwork,
filters, and zoom remain incomplete. Automated server exercises also
confirmed the stock commander-selection delay (15 seconds), acceptance, squad
removal on becoming commander, and commander-tool requests. These checks
support screen structure and server flow; they do not establish a human-play
graphical sequence, the commander overhead-camera behavior, or complete
retail-rule coverage. Its separate command map uses a different protocol and
must not be mistaken for OpenBattlefield2 compatibility.

The existing [`hud-commands.md`](../functions/hud-commands.md) inventory is a
good checklist for finishing this area: connect the existing button and list
commands to the decoded roster and native events, then confirm role and order
changes from server updates before changing the visible state.

## Bink video boundary

OpenBattlefield2's startup path preserves the Bink intro order but does not
decode or play those video streams. The `BfBinkNode` property list (background
picture, fade time, and play data) describes the HUD class interface, not the
video codec. A related safe-Rust, clean-room Bink 1 streaming decoder recognizes
container revisions b–k and decodes video revisions c–k; revision-b video is
unimplemented and Bink 2 is rejected. That decoder is not integrated
into OpenBattlefield2 or the separate BF2 client. BF2 compatibility remains
unknown until the supplied movie revisions are inventoried and playback timing
and audio synchronization are checked. This is a possible implementation lead,
not a compatibility claim or a decoder contribution in this PR.

## Evidence limits

The additional audio findings combine stock configuration inspection,
client-side static analysis, and a separate native-port implementation. HUD
layout comparisons and automated server exercises have narrower scopes than a
full retail playthrough. The independent port's DSP, policies, renderer, and
network protocol are not Refractor implementations. The upstream code links
above describe only the compared OpenBattlefield2 revision; reassess them as
the project changes.
