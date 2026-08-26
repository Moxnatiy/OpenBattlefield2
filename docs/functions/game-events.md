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

## Умовні поля

Плаский список читань бреше там, де поля лежать за умовою. Обидві наші
помилки були саме такі, тож інструмент навчено показувати будову:

```bash
tools/linuxded/bitfields.py --blocks CreateObjectEvent::deSerialize
```

```
блок 0x4237d0   32, 16, 2, 1     -> 0x4238a4 (за умовою)
блок 0x423862   8
блок 0x4238a4   1                -> 0x423910 (за умовою)
блок 0x4238d9   1                -> 0x423970 (за умовою)
блок 0x423910   32, 32, 32       -> 0x4238d9
блок 0x423970   32, 32, 32       -> 0x423881
```

Звідси видно, що в `CreateObjectEvent` дві **взаємно виключні** гілки, а
полярність переходу треба дивитися в самому коді (`cmpl $0x1; jne` —
тобто гілка з вісьмома бітами йде, коли прапорець дорівнює одиниці):

```
32  шаблон
16  мережевий номер
 2  поле
 1  прапорець
     якщо 1: 8 бітів, і на цьому все
     якщо 0: 1 біт -> [позиція 3x32], 1 біт -> [поворот 3x32]
```

Поки цього не врахували, розбір збивався на наступній події в пакеті.
Після виправлення весь потік світу читається без жодного невідомого
типу: 50 об'єктів із позиціями, 24 точки появи.

## Розкладка полів усіх подій

Знято пакетно: `tools/linuxded/bitfields.py <Клас>::deSerialize` для
всього реєстру — 63 події за десять секунд. Числа — розміри полів у
бітах, у порядку читання.

Кілька подій пишуть більше, ніж читають (`CreatePlayerEvent` — два
зайвих біти, `CreateObjectEvent` — три між позицією і поворотом):
порівняння з `::serialize` це показує, і на дроті треба зважати саме
на запис.

| № | клас | поля (бітів) |
|---:|---|---|
| 0 | `StringManagerEvent` | 1, 6, рядок, 1, 1, 1, 8 |
| 1 | `ChallengeEvent` | 80, 8, рядок |
| 2 | `ChallengeResponseEvent` | 584, 32, 32, 1, 31, 31 |
| 3 | `ConnectionTypeEvent` | 3 |
| 4 | `DataBlockEvent` | 1, 32, 32, 8, рядок |
| 5 | `CreatePlayerEvent` | 3, 4, 1, 8, 16, 16, 1, 256 |
| 6 | `CreateObjectEvent` | 32, 16, 2, 1, 8, 1, 1, 32, 32, 32, 32, 32, 32 |
| 7 | `DestroyObjectEvent` | 16 |
| 8 | `DestroyPlayerEvent` | 8 |
| 9 | `EnterVehicleEvent` | 8, 16, 1 |
| 10 | `ExitVehicleEvent` | 8, 1 |
| 11 | `PostRemoteEvent` | 4, 32, 32, 8, рядок |
| 12 | `ChangePlayerNameEvent` | 8, 256 |
| 13 | `HandleDropEvent` | 8, 16, 16, 32, 32, 32 |
| 14 | `HandlePickupEvent` | 8, 16, 16 |
| 15 | `StringBlockEvent` | 1, 8, 8, рядок |
| 16 | `JoinSquadEvent` | 8, 8, 8 |
| 17 | `LeaveSquadEvent` | 8, 8, 8, 1 |
| 19 | `CommanderEvent` | 4, 8, 1, 15, 15 |
| 20 | `RadioMessageEvent` | 8, 8, 2, 8, 5, 3 |
| 21 | `KilledByEvent` | 8, 8, 1, 16, 32, 32, 8 |
| 22 | `ChangeSquadNameEvent` | 8, 8, 8, 8, рядок |
| 23 | `SetPrivateSquadEvent` | 8, 8, 1 |
| 24 | `IssueSquadOrderEvent` | 8, 8, 8, 1, 16, 8, 1, 32, 32, 32, 32, 32, 32, 32, 32, 32 |
| 25 | `InviteEvent` | 8, 8, 8, 1 |
| 26 | `RankEvent` | 2, 6, 32, 8 |
| 27 | `SetAcceptOrderEvent` | 8, 8, 1, 8, 8 |
| 29 | `SpottedEvent` | 16, 16, 32, 32, 8, 32, 32 |
| 30 | `ArtilleryEvent` | 2, 32, 32, 32, 16, 32 |
| 31 | `CreateKitEvent` | 32, 16, 32, 32, 32, 4, 4 |
| 32 | `StickyProjectileEvent` | 16, 16, 8, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 8, 8 |
| 34 | `AmbientEffectAreaEvent` | 16, 8, 16, 32, 32, 32 |
| 35 | `VoipOnOffEvent` | 8, 1 |
| 36 | `CommanderCamEvent` | 32, 32, 32 |
| 37 | `SupplyDropEvent` | 32, 32, 32 |
| 38 | `VoipPlayerMuteEvent` | 1, 8, 8 |
| 39 | `VoteEvent` | 8, 8, 8, 8, 8, 8, 8, 8, 8, 32 |
| 40 | `ToggleFreeCameraEvent` | — |
| 41 | `MedalEvent` | 32, 8, 4 |
| 42 | `UnlockEvent` | 2, 8, 4 |
| 43 | `MissileInitEvent` | 16, 8, 4, 1 |
| 45 | `UAVEvent` | 32, 32, 8, 1 |
| 46 | `ContentCheckEvent` | 128, 128, 128 |
| 47 | `TargetDirectionEvent` | 16, 8, 32, 32, 32, 32, 32, 32 |
| 48 | `EndOfRoundEvent` | 32, 32, 32 |
| 49 | `PythonCommandEvent` | 32, 8, 4, 32 |
| 50 | `RequestEvent` | 8, 8, 3 |
| 51 | `DropVehicleEvent` | 32, 32, 8, 1 |
| 54 | `VoipSessionEvent` | 16 |
| 55 | `KickBanEvent` | 8, 1, 8 |
| 56 | `BeginRoundEvent` | 32, 32 |
| 57 | `CreateSpawnGroupEvent` | 8, 4, 1, 1, 1, 8, 8, 16 |
| 58 | `RemoveSpawnGroupEvent` | 8, 16 |
| 59 | `UpdateTriggerEvent` | 32, 16, 32, 32, 32, 32, 32, 32, 8, 8 |
| 60 | `GrapplingHookContainerCreateEvent` | 8, 16, 8, 32 |
| 61 | `GrapplingHookContainerUpdateEvent` | 8, 8, 32, 32 |
| 62 | `GrapplingHookContainerDetachEvent` | 8, 32, 32, 32 |
| 63 | `GrapplingHookCreateEvent` | 16, 8, 32, 32, 32, 32, 32, 32, 32 |
| 64 | `GrapplingHookUpdateEvent` | 16, 8, 1 |
| 65 | `PlayerTearGassedEvent` | 8, 32, 32, 32 |
| 66 | `SetNightVisionEvent` | 8, 1 |
| 68 | `VerifyPlayerTeamEvent` | 8, 8, 8, 8 |
| 69 | `FixPlayerTeamEvent` | 8, 8, 8, 8 |
