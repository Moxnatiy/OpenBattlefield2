# Реєстр ігрових подій

Джерело: `dice::hfe::*Event::getType()` у 64-бітному Linux-сервері —
кожен клас повертає свою сталу. Список знято пакетно: адреси з
`info functions ::getType`, далі `x/2i` на кожну (`mov $N,%eax; ret`).

Номер типу їде в пакеті у 7 бітах: `GameEventManager::readGameEvent` бере
найменше N, при якому `(1<<N)-1` вміщує розмір реєстру, а тут 69 типів.

Кілька номерів мають по два класи — це події, які не їздять мережею, а
лише сповіщають гру всередині процесу (`DataBlockReadyEvent`,
`StringReceivedEvent`, `RadioMessageReceivedEvent`, `PostRemoteEvent`).

`StringManagerEvent` повертає нуль через `xor %eax,%eax`, тож у першому
проході його не було видно — але саме він приходить у хвості перших
пакетів після реєстрації.

| № | клас |
|---:|---|
| 0 | `StringManagerEvent` |
| 1 | `ChallengeEvent`, `DataBlockReadyEvent` |
| 2 | `ChallengeResponseEvent` |
| 3 | `ConnectionTypeEvent` |
| 4 | `DataBlockEvent` |
| 5 | `CreatePlayerEvent` |
| 6 | `CreateObjectEvent` |
| 7 | `DestroyObjectEvent` |
| 8 | `DestroyPlayerEvent` |
| 9 | `EnterVehicleEvent` |
| 10 | `ExitVehicleEvent` |
| 11 | `PostRemoteEvent`, `StringReceivedEvent` |
| 12 | `ChangePlayerNameEvent` |
| 13 | `HandleDropEvent`, `RadioMessageReceivedEvent` |
| 14 | `HandlePickupEvent` |
| 15 | `StringBlockEvent` |
| 16 | `JoinSquadEvent` |
| 17 | `LeaveSquadEvent` |
| 19 | `CommanderEvent` |
| 20 | `RadioMessageEvent` |
| 21 | `KilledByEvent` |
| 22 | `ChangeSquadNameEvent` |
| 23 | `SetPrivateSquadEvent` |
| 24 | `IssueSquadOrderEvent` |
| 25 | `InviteEvent` |
| 26 | `RankEvent` |
| 27 | `SetAcceptOrderEvent` |
| 28 | `SetSquadLeaderEvent` |
| 29 | `SpottedEvent` |
| 30 | `ArtilleryEvent` |
| 31 | `CreateKitEvent` |
| 32 | `StickyProjectileEvent` |
| 34 | `AmbientEffectAreaEvent` |
| 35 | `VoipOnOffEvent` |
| 36 | `CommanderCamEvent` |
| 37 | `SupplyDropEvent` |
| 38 | `VoipPlayerMuteEvent` |
| 39 | `VoteEvent` |
| 40 | `ToggleFreeCameraEvent` |
| 41 | `MedalEvent` |
| 42 | `UnlockEvent` |
| 43 | `MissileInitEvent` |
| 45 | `UAVEvent` |
| 46 | `ContentCheckEvent` |
| 47 | `TargetDirectionEvent` |
| 48 | `EndOfRoundEvent` |
| 49 | `PythonCommandEvent` |
| 50 | `RequestEvent` |
| 51 | `DropVehicleEvent` |
| 54 | `VoipSessionEvent` |
| 55 | `KickBanEvent` |
| 56 | `BeginRoundEvent` |
| 57 | `CreateSpawnGroupEvent` |
| 58 | `RemoveSpawnGroupEvent` |
| 59 | `UpdateTriggerEvent` |
| 60 | `GrapplingHookContainerCreateEvent` |
| 61 | `GrapplingHookContainerUpdateEvent` |
| 62 | `GrapplingHookContainerDetachEvent` |
| 63 | `GrapplingHookCreateEvent` |
| 64 | `GrapplingHookUpdateEvent` |
| 65 | `PlayerTearGassedEvent` |
| 66 | `SetNightVisionEvent` |
| 68 | `VerifyPlayerTeamEvent` |
| 69 | `FixPlayerTeamEvent` |

## Розкладка полів

Знімається пакетно: `tools/linuxded/bitfields.py <Клас>::deSerialize`.
Інструмент розбирає функцію в 64-бітному сервері й виписує всі виклики
`BitStream::readBits` разом із кількістю бітів — третій аргумент їде в
`%edx`, тож розмір поля видно прямо в коді.

Що вже перевірено на живому сервері:

| подія | поля |
|---|---|
| `StringManagerEvent` (0) | 1 біт; якщо нуль — на цьому все |
| `ConnectionTypeEvent` (3) | 3 |
| `DataBlockEvent` (4) | 1 біт вид; заголовок: u32 тип, u32 розмір; шматок: u8 довжина + байти |
| `CreatePlayerEvent` (5) | 3, 4, 1, 8, 16, 16, 1 бітів і 32 байти імені |
| `CreateObjectEvent` (6) | 32, 16, 2, 1, 8, 1, 1 бітів і шість 32-бітних чисел (позиція й поворот) |
| `DestroyPlayerEvent` (8) | 8 |
| `UnlockEvent` (42) | 2, 8, 4 |
| `VoipSessionEvent` (54) | 16 |
| `BeginRoundEvent` (56) | 32, 32 |
| `CreateSpawnGroupEvent` (57) | 8, 4, 1, 1, 1, 8, 8, 16 |

Перший пакет, який сервер шле після реєстрації, розбирається повністю:

```
CreatePlayerEvent  команда 2, номер 0, ім'я ' OpenBF2'
VoipSessionEvent   сеанс 13413
UnlockEvent        вид 1, гравець 0
StringManagerEvent порожня
StringManagerEvent порожня
```

Ім'я з пробілом попереду — не помилка розбору: на сервері без рейтингу
`GameServer::handleClientInfo` складає його як «тег клану + пробіл + ім'я»,
а тег у нас порожній.
