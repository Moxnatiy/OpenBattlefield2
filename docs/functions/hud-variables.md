# HUD variables: who declares them and which field they live in

Every variable the `.con` mentions in `setNodeShowVariable`,
`setNodeAlphaVariable`, `setTextNodeVariable` and the like is **registered**
somewhere — bound to a field of some object in the game. This table says
which one exactly.

There are two registries, and they must not be confused:

* **`HudInformationLayer`** (`code\bf2\game\HudInformationLayer.h`) —
  the classic HUD variables. Registered by 0x468dd0.
* **the graph's variable manager** — the global `DAT_0098734c`. Through it
  the objects that take part in the `MemeFile` animation are registered
  (the right corner, the map, item selection). Its table's methods: +0x10 bool,
  +0x1c int, +0x28 float, +0x34 string, +0x40 wide string; the matching
  readers are +0x18, +0x24, +0x30, +0x3c, +0x48 (visible in the wrappers
  0x785d00..0x785f10).

Taken with `tools/hud_variables.py` from the decompilation of the registration
functions — how to repeat it is described there too.

## What is not here

**When** a variable is set is not visible (rule 7). The table gives the
field's address; to know who writes into it, the update function of that
object has to be looked at. So the debt row "`CPInterfaceEnabled` — source not
found" is not closed by this: now the **field** is known (+0xa8), not the
place of the write.

Field offsets are given in **bytes**. Where the decompiler declared `param_1`
a pointer to a word, `param_1 + 0x2a` means the forty-second element, that is
byte 0xa8 — the tool makes that correction itself. Where the offset does not
reduce to a single number (a family in a loop), the column holds the original
expression.

## 0x468dd0 — `HudInformationLayer` — the main HUD registry

Variables: 198.

The largest of them all. The `.con` nodes come here for nearly everything the
combat HUD shows — tickets, the map, messages, the time to spawn, kits. It
registers through five instantiations of `register<T>` (0x466240, 0x466390,
0x4664e0, 0x466630, 0x466780); the line in
`code\bf2\game\HudInformationLayer.h` in the "kind" column is the proof of
which one it is.

Names with `%i` are **families**: the game creates them in a loop,
substituting the number. So the "field" column holds not a number but the
start of the row: `Kit%iShow` -> `+0x202 + number`.

| variable | kind | field |
|---|---|---|
| `KitName%iString` | layer wide (h:111) | puVar4 |
| `KitIcon%iPath` | layer string (h:110) | puVar4 |
| `KitWeaponIcon%iPath` | layer string (h:110) | puVar4 |
| `KitAltWeaponIcon%iPath` | layer string (h:110) | puVar4 |
| `KitUnlockArrow%iShow` | layer bool (h:107) | (int)param_1 + iVar1 + 0x26a |
| `KitUnlock%iShow` | layer bool (h:107) | (int)param_1 + iVar1 + 0x271 |
| `Kit%iSprintAbility` | layer float (h:109) | puVar4 |
| `Kit%iUnlockBlinkAlpha` | layer float (h:109) | puVar4 |
| `Kit%iShow` | layer bool (h:107) | (int)param_1 + iVar1 + 0x202 |
| `PlayerKitIcon%iSelectShow` | layer bool (h:107) | (int)param_1 + iVar1 + 0x240 |
| `Kit%iAbilityIcon%iShow` | layer bool (h:107) | puVar4 |
| `Kit%iAbilityIcon%iPathString` | layer string (h:110) | puVar4 |
| `Squad%iString` | layer wide (h:111) | +0x410 |
| `SquadLeader%iString` | layer wide (h:111) | +0x410 |
| `Radio%iString` | layer wide (h:111) | +0x410 |
| `RadioVehicle%iString` | layer wide (h:111) | +0x410 |
| `Commander%iString` | layer wide (h:111) | +0x410 |
| `PlayerWeaponIcon%iPathString` | layer string (h:110) | +0x410 |
| `PlayerWeaponIcon%iSelectShow` | layer bool (h:107) | puVar5 |
| `PlayerWeaponIcon%iActiveShow` | layer bool (h:107) | puVar5 |
| `PlayerWeaponIcon%iShow` | layer bool (h:107) | puVar5 |
| `ShowIngameHud` | layer bool (h:107) | +0x198 |
| `OrderMenuPosX` | layer int (h:108) | +0x188 |
| `OrderMenuPosY` | layer int (h:108) | +0x18c |
| `CreateSquadActiveIconPath` | layer string (h:110) | +0x20c |
| `JoinSquadActiveIconPath` | layer string (h:110) | +0x210 |
| `LeaveSquadActiveIconPath` | layer string (h:110) | +0x214 |
| `KickMemberActiveIconPath` | layer string (h:110) | +0x218 |
| `InvitePlayerActiveIconPath` | layer string (h:110) | +0x21c |
| `MapVoteActiveIconPath` | layer string (h:110) | +0x224 |
| `RematchVoteActiveIconPath` | layer string (h:110) | +0x22c |
| `MutinyVoteActiveIconPath` | layer string (h:110) | +0x230 |
| `KickVoteActiveIconPath` | layer string (h:110) | +0x228 |
| `TimeInfoShow` | layer bool (h:107) | +0x1a9 |
| `SayAllShow` | layer bool (h:107) | +0x1ab |
| `SayTeamShow` | layer bool (h:107) | +0x1ad |
| `SaySquadShow` | layer bool (h:107) | +0x1ac |
| `ChatListShow` | layer bool (h:107) | +0x1ae |
| `EnemyTicketsShow` | layer bool (h:107) | +0x234 |
| `FriendlyTicketsShow` | layer bool (h:107) | +0x235 |
| `PlayerScoreShow` | layer bool (h:107) | +0x238 |
| `MapZoom` | layer int (h:108) | +0x194 |
| `CloseToFlag` | layer bool (h:107) | +0x1af |
| `CloseToNeutralCP` | layer bool (h:107) | +0x1b2 |
| `CloseToAlliedCP` | layer bool (h:107) | +0x1b0 |
| `CloseToAxisCP` | layer bool (h:107) | +0x1b1 |
| `CPInterfaceEnabled` | layer bool (h:107) | +0xa8 |
| `TimePreCPIsTaken` | layer float (h:109) | +0xb0 |
| `TimePreCPIsNeutral` | layer float (h:109) | +0xac |
| `FriendlyCPs` | layer float (h:109) | +0xb4 |
| `EnemyCPs` | layer float (h:109) | +0xb8 |
| `FriendlyNeutralCPs` | layer float (h:109) | +0xc0 |
| `EnemyNeutralCPs` | layer float (h:109) | +0xbc |
| `EnemyFriendlyCPs` | layer float (h:109) | +0xc4 |
| `FriendlyEnemyCPs` | layer float (h:109) | +0xc8 |
| `FriendlyCPString` | layer string (h:110) | +0xcc |
| `EnemyCPString` | layer string (h:110) | +0xd0 |
| `NeutralCPString` | layer string (h:110) | +0xd4 |
| `MedicInRadius` | layer bool (h:107) | +0x1b3 |
| `RepairInRadius` | layer bool (h:107) | +0x1b4 |
| `AmmoInRadius` | layer bool (h:107) | +0x1b5 |
| `CloseToMine` | layer bool (h:107) | +0x1b6 |
| `ShowRankUpIcon` | layer bool (h:107) | +0x1b7 |
| `RankUpIconPathString` | layer string (h:110) | +0x1bc |
| `ShowMedalIcon` | layer bool (h:107) | +0x1b8 |
| `MedalIconPathString` | layer string (h:110) | +0x1c0 |
| `IngameHelpShow` | layer bool (h:107) | +0x1c4 |
| `IngameHelpString` | layer wide (h:111) | +0x1c8 |
| `HitDirectionShow` | layer bool (h:107) | +0x1a8 |
| `HitDirectionAngle` | layer float (h:109) | +0x1a0 |
| `SinglePlayerActive` | layer bool (h:107) | +0x2d7 |
| `VictoryShow` | layer bool (h:107) | +0x1cc |
| `VictoryRankShow` | layer bool (h:107) | +0x1cd |
| `MapShow` | layer bool (h:107) | +0x1ce |
| `MapFullSize` | layer bool (h:107) | +0x1d7 |
| `MapMinSize` | layer bool (h:107) | +0x1d8 |
| `MapFullSizeAndSpawnShow` | layer bool (h:107) | +0x1da |
| `MapFullSizeAndNotSpawnShow` | layer bool (h:107) | +0x1d9 |
| `MapStatic` | layer bool (h:107) | +0x1d1 |
| `MapBorderShow` | layer bool (h:107) | +0x1cf |
| `MapBorderAlternateShow` | layer bool (h:107) | +0x1d0 |
| `MapMenuShow` | layer bool (h:107) | +0x1e2 |
| `SuicideButtonShow` | layer bool (h:107) | +0x1db |
| `SquadLeaderMenuShow` | layer bool (h:107) | +0x1e3 |
| `CommanderMenuShow` | layer bool (h:107) | +0x1e4 |
| `ChoiceMenuShow` | layer bool (h:107) | +0x1e5 |
| `CommanderShow` | layer bool (h:107) | +0x1e6 |
| `SpawnShow` | layer bool (h:107) | +0x1d2 |
| `SpawnInfoShow` | layer bool (h:107) | +0x1d3 |
| `SquadInterfaceShow` | layer bool (h:107) | +0x1e7 |
| `RadioInterfaceShow` | layer bool (h:107) | +0x1e8 |
| `RadioVehicleInterfaceShow` | layer bool (h:107) | +0x1e9 |
| `SquadLeaderInterfaceShow` | layer bool (h:107) | +0x1ea |
| `CommanderInterfaceShow` | layer bool (h:107) | +0x1eb |
| `SpottedInterfaceShow` | layer bool (h:107) | +0x1ec |
| `ScoreboardShow` | layer bool (h:107) | +0x1d4 |
| `VoipListShow` | layer bool (h:107) | +0x1d5 |
| `LevelsListShow` | layer bool (h:107) | +0x1d6 |
| `DemoShow` | layer bool (h:107) | +0x1ed |
| `DemoRecInterfaceShow` | layer bool (h:107) | +0x1f0 |
| `DemoCameraInterfaceShow` | layer bool (h:107) | +0x1ee |
| `DemoPlayerNameShow` | layer bool (h:107) | +0x1ef |
| `DemoPlayerNameString` | layer string (h:110) | +0x1f4 |
| `DemoPausePlayString` | layer wide (h:111) | +0x1f8 |
| `DemoCameraString` | layer wide (h:111) | +0x1fc |
| `SquadOptionsShow` | layer bool (h:107) | +0x1dd |
| `JoinOptionShow` | layer bool (h:107) | +0x1de |
| `LeaveOptionShow` | layer bool (h:107) | +0x1e0 |
| `KickOptionShow` | layer bool (h:107) | +0x1df |
| `InviteOptionShow` | layer bool (h:107) | +0x1e1 |
| `MembersShow` | layer bool (h:107) | +0x200 |
| `KitsShow` | layer bool (h:107) | +0x201 |
| `PlayerKitIconShow` | layer bool (h:107) | +0x1ba |
| `PlayerRankIconShow` | layer bool (h:107) | +0x1b9 |
| `BarTopLeftShow` | layer bool (h:107) | +0x236 |
| `BarTopRightShow` | layer bool (h:107) | +0x237 |
| `ObjectiveIconShow` | layer bool (h:107) | +0x1dc |
| `EnemyTicketsString` | layer string (h:110) | +0x500 |
| `FriendlyTicketsString` | layer string (h:110) | +0x504 |
| `ScoreString` | layer string (h:110) | +0x508 |
| `TimeToSpawnString` | layer wide (h:111) | +0x50c |
| `SpawnInfoString` | layer wide (h:111) | +0x510 |
| `DeathMessage` | layer wide (h:111) | +0xa0 |
| `DisconnectMessage` | layer wide (h:111) | +0xa4 |
| `LevelTime` | layer string (h:110) | +0x514 |
| `ActiveLevel` | layer wide (h:111) | +0x518 |
| `Team1Selected` | layer bool (h:107) | +0x239 |
| `Team2Selected` | layer bool (h:107) | +0x23a |
| `PlayerTeam` | layer int (h:108) | +0x23c |
| `FriendlyFlagIconPathString` | layer string (h:110) | +0x51c |
| `EnemyFlagIconPathString` | layer string (h:110) | +0x520 |
| `Team1FlagIconPathString` | layer string (h:110) | +0x524 |
| `Team2FlagIconPathString` | layer string (h:110) | +0x528 |
| `FriendlyTeamNameString` | layer string (h:110) | +0x52c |
| `EnemyTeamNameString` | layer string (h:110) | +0x530 |
| `Team1NameString` | layer wide (h:111) | +0x534 |
| `Team2NameString` | layer wide (h:111) | +0x538 |
| `RoundRestartString` | layer string (h:110) | +0x53c |
| `FriendlyTeamWinsString` | layer string (h:110) | +0x540 |
| `EnemyTeamWinsString` | layer string (h:110) | +0x544 |
| `WinHeadlineString` | layer wide (h:111) | +0x548 |
| `EnemyVictoryAlpha` | layer float (h:109) | +0xdc |
| `FriendlyVictoryAlpha` | layer float (h:109) | +0xd8 |
| `SetupShow` | layer bool (h:107) | +0x1aa |
| `MenuBackgroundRed` | layer float (h:109) | +0xe8 |
| `MenuBackgroundGreen` | layer float (h:109) | +0xec |
| `MenuBackgroundBlue` | layer float (h:109) | +0xf0 |
| `MenuBackgroundAlpha` | layer float (h:109) | +0xf4 |
| `CrossHairColorRed` | layer float (h:109) | +0x178 |
| `CrossHairColorGreen` | layer float (h:109) | +0x17c |
| `CrossHairColorBlue` | layer float (h:109) | +0x180 |
| `CrossHairColorAlpha` | layer float (h:109) | +0x184 |
| `MenuMapAlpha` | layer float (h:109) | +0xe0 |
| `MenuMapIconAlpha` | layer float (h:109) | +0xe4 |
| `FriendlyTextRed` | layer float (h:109) | +0xf8 |
| `FriendlyTextGreen` | layer float (h:109) | +0xfc |
| `FriendlyTextBlue` | layer float (h:109) | +0x100 |
| `FriendlyTextAlpha` | layer float (h:109) | +0x104 |
| `EnemyTextRed` | layer float (h:109) | +0x108 |
| `EnemyTextGreen` | layer float (h:109) | +0x10c |
| `EnemyTextBlue` | layer float (h:109) | +0x110 |
| `EnemyTextAlpha` | layer float (h:109) | +0x114 |
| `FriendlyBrightRed` | layer float (h:109) | +0x118 |
| `FriendlyBrightGreen` | layer float (h:109) | +0x11c |
| `FriendlyBrightBlue` | layer float (h:109) | +0x120 |
| `FriendlyBrightAlpha` | layer float (h:109) | +0x124 |
| `EnemyBrightRed` | layer float (h:109) | +0x128 |
| `EnemyBrightGreen` | layer float (h:109) | +0x12c |
| `EnemyBrightBlue` | layer float (h:109) | +0x130 |
| `EnemyBrightAlpha` | layer float (h:109) | +0x134 |
| `FriendlyTeamTopRed` | layer float (h:109) | +0x138 |
| `FriendlyTeamTopGreen` | layer float (h:109) | +0x13c |
| `FriendlyTeamTopBlue` | layer float (h:109) | +0x140 |
| `FriendlyTeamTopAlpha` | layer float (h:109) | +0x144 |
| `EnemyTeamTopRed` | layer float (h:109) | +0x148 |
| `EnemyTeamTopGreen` | layer float (h:109) | +0x14c |
| `EnemyTeamTopBlue` | layer float (h:109) | +0x150 |
| `EnemyTeamTopAlpha` | layer float (h:109) | +0x154 |
| `FriendlyTicketRed` | layer float (h:109) | +0x158 |
| `FriendlyTicketGreen` | layer float (h:109) | +0x15c |
| `FriendlyTicketBlue` | layer float (h:109) | +0x160 |
| `FriendlyTicketAlpha` | layer float (h:109) | +0x164 |
| `EnemyTicketRed` | layer float (h:109) | +0x168 |
| `EnemyTicketGreen` | layer float (h:109) | +0x16c |
| `EnemyTicketBlue` | layer float (h:109) | +0x170 |
| `EnemyTicketAlpha` | layer float (h:109) | +0x174 |
| `TotalNoOfPlayers` | layer string (h:110) | +0x590 |
| `YesVotes` | layer string (h:110) | +0x594 |
| `NoVotes` | layer string (h:110) | +0x598 |
| `ServerNameString` | layer string (h:110) | +0x57c |
| `ServerIPString` | layer string (h:110) | +0x580 |
| `ServerPortString` | layer string (h:110) | +0x584 |
| `ServerIsFavourite` | layer bool (h:107) | +0x58c |
| `LocalScore` | layer string (h:110) | +0x2d8 |
| `ShowNightVisionGauge` | layer bool (h:107) | +0x59c |
| `FriendlyTicketBleed` | layer float (h:109) | +0x8c |
| `EnemyTicketBleed` | layer float (h:109) | +0x94 |
| `ServerInfoSelected` | layer bool (h:107) | +0x2d6 |

## 0x789480 — The player's object: health, the left region, the squad

Variables: 82.

Registered **once**, when the HUD is created. Besides the ordinary variables
there are four "references" here — through them the object's fields become
cells of the `Menu/Ingame` graph (`BottomLeft_XPos`, `BottomLeft_nextXPos`,
`BottomLeft_alpha1`, `BottomLeft_alpha2` and their pairs). That is also the
answer to the old question of why the corner layers are positioned nowhere in
the `.con`: their position is driven by the graph, not by the interface's
description.

The **group names** themselves (`BottomLeftStatic`, `TopLayerHud`) are not in
the game's binaries at all — they lie in the string dictionary of `Menu/Ingame`
(docs/formats/hud-meme-graph.md), so searching `BF2.exe` will not find them.

| variable | kind | field |
|---|---|---|
| `BottomLeftHealthAlpha` | float | +0x18c |
| `BottomLeftVehicleAlpha` | float | +0x190 |
| `BottomLeftHealthFadedAlpha` | float | +0x194 |
| `BottomLeftVehicleFadedAlpha` | float | +0x198 |
| `UsingParachute` | bool | +0xb8 |
| `PlayerStamina` | float | +0x1ac |
| `NightVisionGaugeValue` | float | +0x1b0 |
| `VehicleStamina` | float | +0x1b4 |
| `PlayerHealth` | float | +0x1a8 |
| `VehicleArmor` | float | +0x1b8 |
| `PlayerHealthString` | string | +0x270 |
| `VehicleArmorString` | string | +0x26c |
| `VehicleAngle` | float | +0x1c8 |
| `VehicleTurretAngle` | float | +0x1cc |
| `VehicleBanking` | float | +0x1d0 |
| `VehicleElevation` | float | +0x1d4 |
| `VehicleElevationSpeedAngle` | float | +0x1d8 |
| `PlayerAngle` | float | +0x1bc |
| `PlayerBanking` | float | +0x1c0 |
| `PlayerElevation` | float | +0x1c4 |
| `AltitudeString` | string | +0x284 |
| `SpeedString` | string | +0x280 |
| `TorqueString` | string | +0x288 |
| `AngleOfAttack` | int | +0x1f0 |
| `Torque` | float | +0x1e4 |
| `TorqueAngle` | float | +0x1ec |
| `VehiclePassengersShow` | bool | +0x24e |
| `VehicleIconShow` | bool | +0x24f |
| `VehicleIconPathString` | string | +0x27c |
| `GunnerAngle` | float | +0x1e0 |
| `WarningIconShow` | bool | +0x254 |
| `WarningIconPath` | string | +0x268 |
| `PlayerStaminaShow` | bool | +0x245 |
| `VehicleStaminaShow` | bool | +0x249 |
| `PlayerHealthShow` | bool | +0x24b |
| `PlayerStaminaBlink` | bool | +0x246 |
| `NightVisionGaugeBlink` | bool | +0x248 |
| `VehicleStaminaBlink` | bool | +0x24a |
| `PlayerHealthBlink` | bool | +0x24c |
| `VehicleArmorShow` | bool | +0x250 |
| `VehicleArmorBlink` | bool | +0x251 |
| `TurretIconShow` | bool | +0x252 |
| `NightVisionGaugeShow` | bool | +0x247 |
| `ReferenceCrossShow` | bool | +0x264 |
| `LocalKitNameString` | string | +0x274 |
| `VehicleNameString` | string | +0x278 |
| `PlayerHealthColorRed` | float | +0x1f4 |
| `PlayerHealthColorGreen` | float | +0x1f8 |
| `PlayerHealthColorBlue` | float | +0x1fc |
| `PlayerHealthColorAlpha` | float | +0x200 |
| `PlayerSprintColorRed` | float | +0x214 |
| `PlayerSprintColorGreen` | float | +0x218 |
| `PlayerSprintColorBlue` | float | +0x21c |
| `PlayerSprintColorAlpha` | float | +0x220 |
| `NightVisionColorRed` | float | +0x234 |
| `NightVisionColorGreen` | float | +0x238 |
| `NightVisionColorBlue` | float | +0x23c |
| `NightVisionColorAlpha` | float | +0x240 |
| `VehicleSprintColorRed` | float | +0x224 |
| `VehicleSprintColorGreen` | float | +0x228 |
| `VehicleSprintColorBlue` | float | +0x22c |
| `VehicleSprintColorAlpha` | float | +0x230 |
| `VehicleArmorColorRed` | float | +0x204 |
| `VehicleArmorColorGreen` | float | +0x208 |
| `VehicleArmorColorBlue` | float | +0x20c |
| `VehicleArmorColorAlpha` | float | +0x210 |
| `CloseToMedic` | bool | +0x290 |
| `CloseToRepair` | bool | +0x291 |
| `CloseToAmmo` | bool | +0x292 |
| `SquadInfoBarShow` | bool | +0x295 |
| `ShowCommanderIcon` | bool | +0x296 |
| `ShowSquadIcon` | bool | +0x297 |
| `SquadInfoText` | wide string | +0x298 |
| `SquadInfoIconPath` | string | +0x29c |
| `KilledState` | bool | +0x253 |
| `GuiIndex` | int | +0xa0 |
| `OccupyingPlayerName%iString` | string | ? |
| `OccupyingPlayerPos%iX` | int | ? |
| `OccupyingPlayerPos%iY` | int | ? |
| `OccupyingPlayer%iColorRed` | float | ? |
| `OccupyingPlayer%iColorGreen` | float | ? |
| `OccupyingPlayer%iColorBlue` | float | ? |

## 0x7a62c0 — The bottom right corner: weapon, ammo, the sight

Variables: 121.

The object itself is taken apart separately — docs/functions/hud-bottom-right.md.

| variable | kind | field |
|---|---|---|
| `BottomRightDirection` | bool | +0x18 |
| `BottomRightAlpha` | float | +0x10 |
| `BottomRightFadedAlpha` | float | +0x14 |
| `MissileTargetLockId` | int | +0x98 |
| `TargetDirection` | float | +0x178 |
| `PrimaryAmmo` | float | +0xb4 |
| `SecondaryAmmo` | float | +0xc4 |
| `HitIndicatorIconAlpha` | float | +0xfc |
| `BombFuel` | float | +0xd0 |
| `FiringIndex` | int | +0xe4 |
| `CrosshairUpPos` | int | +0x10c |
| `CrosshairDownPos` | int | +0x110 |
| `CrosshairLeftPos` | int | +0x114 |
| `CrosshairRightPos` | int | +0x118 |
| `TVBlink` | bool | +0x12c |
| `SecondaryHeatShow` | bool | +0xec |
| `SecondaryHeatValue` | float | +0xf0 |
| `PrimaryHeatShow` | bool | +0xed |
| `PrimaryHeatValue` | float | +0xf4 |
| `PrimaryGrenadeLoadShow` | bool | +0xbf |
| `SecondaryGrenadeLoadShow` | bool | +0xc0 |
| `SecondaryReloadTime` | float | +0xc8 |
| `SecondaryReloadTimeShow` | bool | +0xcc |
| `SecondaryEternalShow` | bool | +0xcd |
| `SecondaryEternalMagsShow` | bool | +0xce |
| `PrimaryEternalShow` | bool | +0xbd |
| `PrimaryEternalMagsShow` | bool | +0xbe |
| `PrimaryReloadTimeShow` | bool | +0xbc |
| `PrimaryReloadTime` | float | +0xb8 |
| `PrimarySingleFireShow` | bool | +0xc1 |
| `SecondarySingleFireShow` | bool | +0xcf |
| `PrimaryAmmoBarShow` | bool | +0xac |
| `PrimaryClipsShow` | bool | +0xad |
| `PrimaryAmmoShow` | bool | +0x12d |
| `PrimaryAmmoBlink` | bool | +0x12e |
| `PrimaryHeatBlink` | bool | +0x12f |
| `PrimaryAmmoIconShow` | bool | +0x130 |
| `PrimaryAmmoIconPathString` | string | +0x150 |
| `PrimaryAmmoString` | string | +0x144 |
| `PrimaryClipStringShow` | bool | +0x134 |
| `PrimaryClipBlink` | bool | +0x135 |
| `PrimaryClipPathString` | string | +0x14c |
| `PrimaryClipString` | string | +0x148 |
| `PrimaryClips` | float | +0x140 |
| `PrimaryClip1Show` | bool | +0x131 |
| `PrimaryClip2Show` | bool | +0x132 |
| `PrimaryClip3Show` | bool | +0x133 |
| `PrimaryAmmoBarFrontPathString` | string | +0x154 |
| `PrimaryAmmoBarBackPathString` | string | +0x158 |
| `SecondaryAmmoBarShow` | bool | +0xae |
| `SecondaryClipsShow` | bool | +0xaf |
| `SecondaryAmmoShow` | bool | +0x136 |
| `SecondaryAmmoBlink` | bool | +0x137 |
| `SecondaryHeatBlink` | bool | +0x138 |
| `SecondaryAmmoIconShow` | bool | +0x139 |
| `SecondaryAmmoIconPathString` | string | +0x16c |
| `SecondaryAmmoString` | string | +0x160 |
| `SecondaryClipStringShow` | bool | +0x13e |
| `SecondaryClipBlink` | bool | +0x13f |
| `SecondaryClipPathString` | string | +0x168 |
| `SecondaryClipString` | string | +0x164 |
| `SecondaryClips` | float | +0x15c |
| `SecondaryClip1Show` | bool | +0x13b |
| `SecondaryClip2Show` | bool | +0x13c |
| `SecondaryClip3Show` | bool | +0x13d |
| `SecondaryAmmoBarFrontPathString` | string | +0x170 |
| `SecondaryAmmoBarBackPathString` | string | +0x174 |
| `PrimaryWeaponNameString` | string | +0x17c |
| `SecondaryWeaponNameString` | string | +0x180 |
| `LaserTargetRed` | float | +0xd4 |
| `LaserTargetGreen` | float | +0xd8 |
| `LaserTargetBlue` | float | +0xdc |
| `LaserTargetAlpha` | float | +0xe0 |
| `PrimaryAmmoColorRed` | float | +0x30 |
| `PrimaryAmmoColorGreen` | float | +0x34 |
| `PrimaryAmmoColorBlue` | float | +0x38 |
| `PrimaryAmmoColorAlpha` | float | +0x3c |
| `PrimaryClipColorRed` | float | +0x40 |
| `PrimaryClipColorGreen` | float | +0x44 |
| `PrimaryClipColorBlue` | float | +0x48 |
| `PrimaryClipColorAlpha` | float | +0x4c |
| `SecondaryAmmoColorRed` | float | +0x50 |
| `SecondaryAmmoColorGreen` | float | +0x54 |
| `SecondaryAmmoColorBlue` | float | +0x58 |
| `SecondaryAmmoColorAlpha` | float | +0x5c |
| `SecondaryClipColorRed` | float | +0x60 |
| `SecondaryClipColorGreen` | float | +0x64 |
| `SecondaryClipColorBlue` | float | +0x68 |
| `SecondaryClipColorAlpha` | float | +0x6c |
| `PrimaryHeatColorRed` | float | +0x70 |
| `PrimaryHeatColorGreen` | float | +0x74 |
| `PrimaryHeatColorBlue` | float | +0x78 |
| `PrimaryHeatColorAlpha` | float | +0x7c |
| `SecondaryHeatColorRed` | float | +0x80 |
| `SecondaryHeatColorGreen` | float | +0x84 |
| `SecondaryHeatColorBlue` | float | +0x88 |
| `SecondaryHeatColorAlpha` | float | +0x8c |
| `FlaresReloadTime` | float | +0x188 |
| `FlaresReloadTimeShow` | bool | +0x184 |
| `CrosshairIconShow` | bool | +0xe8 |
| `CrosshairActive` | bool | +0xe9 |
| `HitIndicatorIconShow` | bool | +0xeb |
| `WrenchRepairingShow` | bool | +0xee |
| `TargetDistanceString` | wide string | +0x190 |
| `TargetCoordXString` | string | +0x194 |
| `TargetCoordYString` | string | +0x198 |
| `TargetCoordZString` | string | +0x19c |
| `CrosshairIconPathString` | string | +0x1a0 |
| `FireRateIconShow` | bool | +0x18c |
| `FireRateIconPathString` | string | +0x1a4 |
| `AbilityIconPath` | string | +0x1b4 |
| `AbilityBarShow` | bool | +0x1b0 |
| `AbilityBar` | float | +0x1ac |
| `AbilityValue` | float | +0x1b8 |
| `AbilityValueShow` | bool | +0x1b1 |
| `VoiceOverIconShow` | bool | +0x1bc |
| `VoiceOverMuted` | bool | +0x1c0 |
| `VoiceOverTalkIconShow` | bool | +0x1bd |
| `VoiceOverDistIconShow` | bool | +0x1be |
| `VoiceOverCommanderIconShow` | bool | +0x1bf |
| `HasMissileConnection` | bool | +0x1c1 |

## 0x780180 — The map node

Variables: 11.

The names are assembled from the node's name: `%s` -> `Minimap`, `Commander`
and so on. The second part of the offsets here is `?` — the decompiler did not
show the call's second argument, so the field is not visible from this
function.

| variable | kind | field |
|---|---|---|
| `%sDelayedMapAngle` | float | ? |
| `%sSatelliteTimeUntilReloaded` | float | ? |
| `%sSatelliteActive` | bool | ? |
| `%sSatelliteReloading` | bool | ? |
| `%sUAVTimeUntilReloaded` | float | ? |
| `%sUAVState` | bool | ? |
| `%sUAVActive` | bool | ? |
| `%sUAVReloading` | bool | ? |
| `%sShowKitsOnMap` | bool | ? |
| `%sZoomDisplay` | int | ? |
| `%sMapFilter%iActive` | bool | ? |

## 0x792210

Variables: 18.

| variable | kind | field |
|---|---|---|
| `ArtilleryState` | bool | +0x8 |
| `CommanderNoArtilleryShow` | bool | +0x9 |
| `ArtilleryArmor` | float | +0x18 |
| `CommanderNoSatelliteShow` | bool | +0xa |
| `SatelliteArmor` | float | +0x14 |
| `CommanderNoUAVShow` | bool | +0xc |
| `UAVArmor` | float | +0x10 |
| `ArtilleryTimeUntilReloaded` | float | +0x3c |
| `ArtilleryReloading` | bool | +0x44 |
| `CommanderRadioShow` | bool | +0x89 |
| `VehiclesAvailable` | float | +0x7c |
| `CommanderNoVehicleDropShow` | bool | +0xb |
| `SupplyTimeUntilReloaded` | float | +0x54 |
| `SupplyState` | bool | +0x4c |
| `SupplyReloading` | bool | +0x5c |
| `VehicleDropTimeUntilReloaded` | float | +0x6c |
| `VehicleDropState` | bool | +0x70 |
| `VehicleDropReloading` | bool | +0x64 |

## 0x78dcb0

Variables: 12.

| variable | kind | field |
|---|---|---|
| `TeamTopLabelString` | string | +0x8 |
| `NrOfUnassigned` | wide string | +0xc |
| `NrOfSquads` | wide string | +0x10 |
| `SquadMemberListShow` | bool | +0xa0 |
| `SquadRenameShow` | bool | +0xe9 |
| `SquadCreateShow` | bool | +0xe8 |
| `SquadCreateLockedShow` | bool | +0xea |
| `SquadCreateTagString` | string | +0xec |
| `NewSquadName` | string | +0xf0 |
| `InviteListShow` | bool | +0xa1 |
| `IsInSquad` | bool | +0xa2 |
| `IsSquadLeader` | bool | +0x9c |

## 0x74dd10 — HUD state and messages

Variables: 9.

| variable | kind | field |
|---|---|---|
| `HudState` | int | +0x250 |
| `DisconnectMessageActive` | bool | +0xbd |
| `PauseMessageActive` | bool | +0xbe |
| `CommanderTracking` | bool | +0x2b0 |
| `SetSpawnPoint` | bool | +0x228 |
| `CommanderRightNameColorRed` | float | +0x350 |
| `CommanderRightNameColorGreen` | float | +0x358 |
| `CommanderRightNameColorBlue` | float | +0x354 |
| `InvalidVehicleDropArea` | bool | +0x35c |

## 0x79de60 — Item selection (the kit wheel)

Variables: 5.

| variable | kind | field |
|---|---|---|
| `ShowItemSelect` | bool | +0x9 |
| `ItemSelectActive` | int | +0xc |
| `Item%iIcon` | string | ? |
| `ItemSlot%iBackActiveAlpha` | float | ? |
| `ItemSlot%iActiveAlpha` | float | ? |

## 0x7aa5e0

Variables: 6.

| variable | kind | field |
|---|---|---|
| `ShowLoadingScreen` | bool | +0x4 |
| `LoadingBarValue` | float | +0x8 |
| `LoadingPicturePathString` | string | (undefined4 *)(param_1 + 0xc) |
| `LoadingScreenMapName` | string | +0x10 |
| `LoadingScreenDebug` | string | +0x14 |
| `LoadingScreenBuildNr` | string | +0x18 |

## 0x7a2280

Variables: 4.

| variable | kind | field |
|---|---|---|
| `ToggleSquads` | bool | +0x364 |
| `ToggleScore` | bool | +0x365 |
| `ToggleManage` | bool | +0x366 |
| `TeamVoteOnly` | bool | +0x367 |

## 0x7cb220 — A generic node: `%sXPos`, `%sYPos`, `%sShow`

Variables: 3.

| variable | kind | field |
|---|---|---|
| `%sXPos` | int | ? |
| `%sYPos` | int | ? |
| `%sShow` | bool | ? |

## 0x7cd230

Variables: 1.

| variable | kind | field |
|---|---|---|
| `%sLockedShow` | bool | ? |

