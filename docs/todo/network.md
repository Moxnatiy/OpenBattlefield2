# The network part: what the server says and sends, and what we do with it

The map of the whole conversation with an original BF2 1.50 server, item by item:
what is **done**, what is **partial**, what is **missing**. Kept current — every
pass that touches the network changes a line here.

Sources: OpenBF2's reconstruction (`reference/OpenBF2`, GPL-3.0: hints only, our
code is written from these notes) with its `BF2.exe` addresses, the Linux server
where checked (addresses given), and the live server. Measures: the end-of-run
report (`delivery:`, `round:`), `capture_scan <capture> [--all | --remote]`, and
the `NOT HANDLED:` / `NOT DONE:` / `EVENT WALK BROKE` lines of a live log.

Legend: ✅ done · 🟡 partial (what is missing is said) · ❌ missing ·
➖ not ours to do (the server's side, the editor's, a single-player path).

**"Read" and "used" are different things.** Everything the server sends is now
read to its length — no packet goes astray (a 658-packet live capture: 0 broken
walks, 0 astray). What is left is mostly *acting* on what is read: HUD screens,
sounds, the commander, the chat. Those rows say which module would act.

---

## 1. Transport (`net::bf2` protocol, `session/remote_world`)

| item | state | where / note |
|---|---|---|
| connection request, accept, denied, acknowledge | ✅ | `writeConnectRequest`, `readPacket` |
| challenge and its response | ✅ | `ChallengeEvent` (1) read in `readPacket`, `challengeResponseEvent` sent |
| ClientInfo block (block 1, sent) | ✅ | `buildClientInfo`, as data-block events |
| connection type (`ConnectionTypeEvent`, 3) | ✅ | from the profile's `General.con` |
| content check (`ContentCheckEvent`, 46) | ✅ | fingerprints by the challenge number of block 2 |
| ping request / response | ✅ | the response carries the last data number (`SWConnection::sendPingResponse`) |
| disconnect, with its reason, three times | ✅ | `writeDisconnect` (reason 13, `GameClient::disconnect`) |
| the server's disconnect / deny reasons on screen | 🟡 | logged; no message box (the menu's `setMessageId`) |
| the next map on the same connection (`LoadLevel` cycle) | ✅ | a `MapInfo` (block 2) while a level is up ends the level's session; the connection stays, the next level opens in the same window and the join starts over from `NELoadComplete` (`JoinSequence::restartForLevel`). Live: a server-made change and an rcon `admin.runNextLevel`, both rejoined and spawned |
| PunkBuster | ➖ | the server runs without it |

## 2. BitStream (`net/bitstream`)

| primitive | state |
|---|---|
| bits low-first, bytes, strings | ✅ |
| `CompressedVector` {32,20,16,12} | ✅ |
| `HighCompressedVector` {32,12,10,8} | ✅ |
| `CompressedVector2` (our `WideVector`) {28..8}, level 7 = the base | ✅ read and write |
| `NormalAngle`, `NormalVector` (dead zone of one step) | ✅ read and write |
| `UnitQuaternion3Comp` (w's sign, x y z) | ✅ read and write |
| `UnitQuaternion` (w x y z, normalized) | ✅ read and write |
| `shrinkNormalVector` | ➖ the writer's preview, not on the wire |

The quaternion readers are new; the object state still reads its quaternion by
hand (`bf2_events.cpp`, `kObjectQuaternionBits`) — to move onto
`readUnitQuaternion` when that reader is next touched.

## 3. Delivery (`net/bf2_link`)

| item | state |
|---|---|
| numbered data packets, our acknowledgement mask | ✅ |
| the server's acknowledgement → delivered / lost per packet | ✅ |
| one event block per packet, 15 unconfirmed at most, lost block resent whole | ✅ |
| a repeated or stale server packet dropped | ✅ |
| 20 packets a second while there are no actions | ✅ (our timer; the engine's is its transmit tick) |
| the received event blocks executed in block order | ✅ `Link::receiveBlock` (`GameEventManager::processReceivedPacket`): a block that overtook a lost one is held until its resend, then both are handed on oldest first; the world and the session take that ordered sequence, events before ghosts. A repeat (up to 16 behind) is dropped — that window is **chosen, not recovered**. The report's `received blocks:` line counts them; a live run on gpm_coop had one block out of order, held and released |
| the event `delay` field of `PostRemoteEvent` | 🟡 read, not applied (always 0 from the server so far) |
| `eventReceivedByRemote` (event 20, end-of-round guard ack) | ❌ |

## 4. Join, round, spawn

| item | state |
|---|---|
| `NELoadComplete`, content check, `NEDatabaseComplete` as the answer | ✅ |
| `NEStartSimulation`, the game status (playing / pre-game / end) | ✅ read, logged |
| `BeginRoundEvent` (56): waiting, countdown, on | ✅ read; ❌ the HUD's countdown not shown |
| `NEReset` answered, the round reset on our side | ✅ |
| spawn: DONE sends the group only; kit at the click; team only on a change | ✅ |
| `NEPlayerSpawned`, `NEPlayerDead` | ✅ |
| the time to spawn (`spawnAtTick`, 0x20 of the player state) | ✅ counted down on the spawn screen |
| the server's "pick another spawn point" (network 6 to the client) | ❌ logged as `NOT HANDLED: network event 6`; the original posts `ECGame 7` and reopens the screen |
| `NEPlayerDead`'s second byte (the flag `killPlayer` takes) | 🟡 not read |
| end of round (`EndOfRoundEvent` 48) | ✅ winner and rounds remaining drive the shared results screen |
| map change | ✅ an empty `MapInfo` unloads, the named one loads (`GameClient::handleDataBlock` case 2); `WorldView::leaveLevel` drops the old level's objects, groups and soldier, players and squads stay |
| `VerifyPlayerTeam` (68) / `FixPlayerTeam` (69) | ❌ walked; the original corrects its idea of our team |

## 5. Players and the world

| item | state |
|---|---|
| `CreatePlayerEvent` (5) | ✅ |
| `DestroyPlayerEvent` (8) | ✅ the player leaves our list (new) |
| `ChangePlayerNameEvent` (12) | ❌ walked |
| `CreateObjectEvent` (6) | ✅ template, position, rotation |
| `DestroyObjectEvent` (7) | ✅ the object leaves our world (new) |
| `EnterVehicleEvent` (9) / `ExitVehicleEvent` (10) | 🟡 the soldier is put in or out; seats not read, a passenger not drawn |
| `CreateKitEvent` (31), `HandlePickupEvent` (14) | ✅ |
| `HandleDropEvent` (13) | 🟡 decoded. When the kit template is known, the client places its ground mesh at the reported position and removes it on pickup. A drop for a kit that was never created on this client has no mesh yet. |
| `KilledByEvent` (21) | ❌ walked (the kill message, the killer's name) |
| `RankEvent` (26), `MedalEvent` (41), `UnlockEvent` (42) | 🟡 decoded; rank and unlocks update player state, medal notices are displayed. Live progression backend verification pending. |
| `VoipSessionEvent` (54), `VoipOnOffEvent` (35), `VoipPlayerMuteEvent` (38) | 🟡 session negotiation and voice transport implemented. Live retail audio verification and per-player mute pending |
| `KickBanEvent` (55) | ❌ walked |
| `StringBlockEvent` (15), `StringManagerEvent` (0) | 🟡 walked (0's length fixed — Linux 0x431cb0); strings not kept |
| `PythonCommandEvent` (49) | ❌ walked |

## 6. Data blocks (`DataBlockEvent`, 4)

| block | state |
|---|---|
| 0 `ServerInfo` | 🟡 the game name, the welcome lines, voice and voting flags; the rest (spawn time, time limit, ticket ratio, team ratio, bots …) not taken |
| 1 `ClientInfo` | ✅ sent |
| 2 `MapInfo` net buffer | ✅ the level and mode the client loads, slots, commander, challenge number; an empty name is the unload |
| 4 lightmap request | ➖ for a lightmap rendering node |
| 5 `MapList` | ✅ the whole rotation and the current index (`parseMapList`), kept in the session; the map's size comes from it. It used to be read as "the level" from its first entry — wrong for any rotation of more than one map |
| 6 remote console command (sent) | ❌ |
| 7 remote console reply | ❌ |
| 8 `EndOfRoundInfo` | ✅ decoded stats, player score rows, and ticket history. Shared native results screen. |
| 9 | ❌ purpose not established (OpenBF2 frontier too) |

## 7. Remote events (`PostRemoteEvent`, 11)

**Category 6, network** (docs/functions/network-events.md "The table"):

| # | name | state |
|---:|---|---|
| 1 | DataBlockReady | 🟡 sent behind `--block-ready` only |
| 2 | LoadComplete | ✅ |
| 3 | StartSimulation | ✅ |
| 4 | DatabaseComplete | ✅ |
| 5 | Reset | ✅ |
| 6 | SelectSpawnGroup | ✅ sent; ✅ the server's 6 ("choose again") reopens our choice, so the next DONE sends a group again (counted in the report's `round:` line). Not seen live yet; what the original's HUD shows on it (`ECGame 7`) is not established |
| 7 | SelectTeam | ✅ |
| 8 | SelectKit | ✅ sends the kit plus a locally selected level from the server-provided unlocks |
| 9 | PlayerSpawned | ✅ |
| 10 | PlayerDead | ✅ (its flag byte not read) |
| 11 | StringReceived (chat) | Event 15 assembles incoming and outgoing messages. The retail server selects recipients. |
| 12 | Suicide | ✅ |
| 13 | RadioMessageReceived | Radio calls use reliable event 20. |
| 14–16 | trace | ➖ |
| 17 | NetworkableDestroyed | ➖ local to the engine |
| 18 | RemoteConsoleCommand | ❌ |
| 19 | RemoteConsoleFeedback | ❌ |
| 20 | EndOfRoundGuard | ✅ nothing to send: the server counts it acknowledged when the packet carrying it is (`PostRemoteEvent::eventReceivedByRemote`), which our acknowledgement does (§3) |

**Category 0, game logic**: 1 status ✅, 6 reset ✅, 8 a refused team change ❌,
14 revived 🟡 the retail LAN server confirmed an OpenBF2 medic reviving an allied bot, and an allied bot reviving OpenBF2's existing soldier. The client moved that same soldier after the revive. The revive event switches the pose from ragdoll to prone. **Category 2, HUD**: the ~70 `ECHudHE*` events are named and logged
once per kind (`hudEventName`). Squad notifications, invitations and spotting are
handled. Several other events, including rank and medal notifications, still
need HUD handling.

## 8. The ghost stream

| item | state |
|---|---|
| the header, records walked by their 11-bit length | ✅ never breaks the packet |
| record kinds 0, 1, 3 | ✅ (kind 3 for our own soldier kept, for others erased — see CLAUDE.md debt) |
| the controlled-object state | ✅ |
| a record's class | 🟡 from `CreatePlayer` / `CreateSpawnGroup`, the score manager's fixed id 0x0201, a control point's creation place; otherwise **guessed from a full record's mask** — the engine knows it by the object's template class, and the level's template numbers are not reversed (ours diverge inside `Objects_server.zip` already: our 5720 is an effect, the server's the first control point) |

The networkable classes the server ghosts (OpenBF2 `*Networkable::setNetUpdate`):

| class | state | what it would give |
|---|---|---|
| `SoldierNetworkable` | ✅ all 21 bits walked, the main ones named | — |
| `Player` | ✅ | — |
| `SpawnGroup` | ✅ | — |
| `SimpleObjectNetworkable` | 🟡 the head (position, velocities, rotation); the tail by length | damage, seats' weapons |
| `ControlPointNetworkable` | ✅ read whole (`control_point_state.h`): team, neutral, take-over and its rate, capture allowed ×2, flag team, capturing state; the owners drive both maps' flags. A point is known by its `CreateObjectEvent` standing at the level's control point — **[CHOSEN, not recovered]**: the engine knows it by its template's class, and our numbering of the level's templates is not reversed. Live 4 points, every record exact | — |
| `ScoreManager` | ✅ the static networkable 0x0201, read whole (`score_state.h`); tickets, bleed and ticket state on the HUD (docs/functions/score-manager.md). Live 7 of 7 records exact | — |
| `PlayerControlObjectNetworkable` | ❌ | a vehicle's seats, its weapons |
| `DestroyableObjectNetworkable`, `DestroyableWindowsBundleNetworkable` | ❌ | destroyed bridges, windows |
| `GenericProjectileNetworkable` | ✅ read whole (`projectile_state.h`, `BF2.exe` 0x636ac0; docs/functions/projectiles.md): position, rotation, the detonation (number, unit vector, target), the armor's hit points; told by the create event's template class. Live 1302 of 1302 records exact; grenades and missiles drawn by their meshes. Not done: the detonation's effect and damage, `predict`'s interpolation, our local projectile's hand-over | — |
| `KitNetworkable` | ➖ | retail sends kit identity through events and reports ammunition in the owner's controlled-soldier snapshot after pickup (docs/functions/network-events.md) |
| `ItemNetworkable` | ❌ | loose items |
| `RotationalBundleNetworkable` | 🟡 | FAAV, M1A2, and T-90 gun mounts use the decoded seat and tank layouts. Other vehicle descriptors, rotors, and rates still pending |
| `EngineNetworkable`, `SpringNetworkable` | ❌ | engines and wheels |
| `FreeCameraNetworkable` | ❌ | spectator camera |
| `OverheadCameraNetworkable` | 🟡 | identified camera templates read position, team and enabled state. Live camera capture pending. |
| `TargetObjectNetworkable`, `TriggerableTargetNetworkable` | ❌ | targets, triggers |
| `NetworkManager` | ❌ | purpose not established |

## 9. What the client sends (beyond the join)

| request | state |
|---|---|
| player actions (`PlayerActionManager`, three per packet) | ✅ |
| suicide | ✅ |
| squads: join, leave, name (create), lock | ✅ (leave and lock: answers not yet watched) |
| squads: invitations and leadership | Event 25 sends and declines an invitation. Accept joins the squad after HUD event 16. A leader transfers to the next retail successor by leaving and rejoining an open squad. |
| squad orders and support | Event 24 carries orders and support requests. The HUD and maps use received orders. A squad leader adopts an accepted commander order and sends a reply through events 50 and 27. |
| commander | Role, camera, squad orders, support replies and the five asset requests are wired. See [commander multiplayer](../functions/commander-multiplayer.md) for server dependencies and verification. |
| radio and spotting | Event 20 uses stock voice message IDs. Event 29 requests a spot. HUD 43 confirms the marker for the spotting team. |
| outgoing chat | Event 15 packages the text and the All, Team, or Squad channel for retail server routing. |
| votes (`VoteEvent`: kick, map) | 🟡 requests, ballots, and results wired. Live two-client verification pending. |
| free camera toggle | 🟡 requires the server option and replicated player flag. Camera position is local. |
| voice | 🟡 squad and command push-to-talk, relay transport, and playback implemented. Live retail verification pending. |

---

## Next, in order of what the player sees

1. Vehicles: seat switching and turret aim, firing and ammo feedback for the
   FAAV, M1A2, and T-90 are implemented (docs/functions/vehicle-seats.md).
   The exterior camera reads the seat's template and the local M1A2 and T-90
   driver gets short-horizon hull prediction. The Havoc and Su-34 pilot and
   observer checks passed on hosted Gulf of Oman Conquest. Pending work
   includes cockpit views, other ground-vehicle prediction, aircraft motion
   prediction and weapon descriptors, and the Vodnik gunner's seating height.
2. Projectiles: M67 detonation plays its effect and sound from the server record
   (docs/functions/projectiles.md). Other projectile effects, retail prediction,
   and the local-to-server projectile handoff are pending.
3. The HUD events (category 2) one screen at a time.
4. Verify the results screen and next-round deployment with two clients on a retail server.
5. The template numbers of the level's own templates (and where ours diverge
   from the server's before them): the ghost class from the template class for
   every object, not only the ones known by id or place.
