# План: серверна логіка та ігровий HUD

Джерело істини — **Linux-сервер BF2 1.5** (`Game Files/OtherFiles/linuxded/bin/amd-64/bf2`,
ELF x86-64, **не стрипнутий**: 36 076 функцій із повними C++-сигнатурами). Карта
символів вивантажена в `docs/reference/linuxded-symbols.txt` і
`docs/reference/linuxded-functions.txt`. DWARF у бінарі є лише для стартового
коду glibc, тож розкладки структур беремо не з нього, а з поведінки, `.con`-даних
і декомпіляції окремих функцій.

Порядок роботи не змінюється: конспект → тест → реалізація. Копіювати
декомпільований код не можна (CLAUDE.md, п. 6).

## Що вже є

- фіксований тік 30 Гц, петлевий канал, роздача об'єктів клієнту;
- рух солдата з константами `phy-soldier-*`, зіткнення з геометрією;
- контрольні точки: захоплення за сталим часом, поява на своїй точці;
- розбір `GamePlayObjects.con`: 4 точки, 27 спавнерів на Dalian Plant.

## Кістяк оригіналу, за яким рівняємось

`dice::hfe::ServerGameLogic` — машина станів, яку крутить `update(float)`:

```
uFirstPreGame -> uPreGame        (розігрів, чекаємо гравців)
uFirstPlaying -> uPlaying        (гра; всередині:)
                   uPlayingSpawning       — черга появи, хвилі
                   uPlayingTicketSystem   — квитки й витік
                   uPlayingInsideGameArea — межі бойової зони
                   uPlayingWinner         — умова перемоги
uFirstEndGame -> uEndGame        (підсумки, наступна карта)
```

Ключові методи, які нам треба відтворити: `spawnPlayer`, `reSpawnPlayer`,
`killPlayer`, `suicide`, `giveDamage`, `heal`, `resurrect`, `replenishAmmo`,
`selectTeam`, `selectKitAndUnlockLevel`, `getNewKit`, `handlePickup`,
`handleDrop`, `handleExplosion`, `checkPlayerTriggers`, `setTicket*`/`getTicket*`,
`getTicketLimitReachedId`, `loadNextLevel`, `restartMap`.

## Етапи

### S1. Стан гри та квитки
- [x] `GameStatus` і машина станів у `GameServer::tick` (PreGame → Playing → EndGame).
- [x] Квитки на команду: `gamelogic.setDefaultNumberOfTickets` читається з
      `GameLogicInit.con`, множник — `sv.ticketRatio` із `ServerSettings.con`.
- [x] Витік квитків за вагою площі + окремий «кінцевий» темп.
- [x] `enemyTicketLossWhenCaptured` — разова втрата при захопленні точки.
- [x] Умова перемоги за квитками (`endGame`).
- [ ] Пороги попереджень `setTicketLimit`/`ticketState` (10, 10 %, 20 %).
- [ ] Ліміт часу раунду.
- [ ] Смерть гравця — мінус квиток (потрібна система смерті, S4).

### S2. Контрольні точки 1:1
- [x] Параметри шаблону замість наших сталих: `timeToGetControl`,
      `timeToLoseControl`, `areaValueTeam1/2`, `unableToChangeTeam`,
      `onlyTakeableByTeam`, `enemyTicketLossWhenCaptured`.
- [x] Нейтралізація перед захопленням (прапор донизу, потім угору).
- [x] Вплив кількості гравців у радіусі на швидкість.
- [ ] `radiusOffset` і півсферичний радіус (`isHemisphere`).
- [ ] Гравець у техніці рахується лише як перший пасажир.
- [ ] Зв'язок точки зі спавнером техніки (`teamOnVehicle`, `teamFromClosestCP`).

### S3. Команди, набори, поява
- [ ] Команди 1 і 2 з назвами (`setTeamName`, `getTeamName`), автобаланс.
- [ ] `SpawnPoint`/`SpawnGroup` із `GamePlayObjects.con` — реальні точки появи
      замість центру контрольної точки.
- [ ] Набори (kits): `menuTeamManager.addKit/addTeam/addWeapon` (зараз без
      обробника), вибір набору при появі.
- [ ] Хвилі появи: `getDefaultTimeToNextAIWave`, черга `uPlayingSpawning`.
- [ ] Екран появи: вибір точки клієнтом → пакет серверу.

### S4. Здоров'я, шкода, смерть
- [ ] Здоров'я об'єкта й солдата, `giveDamage` з типом шкоди.
- [ ] Смерть → `killPlayer` → таймер появи → `reSpawnPlayer`.
- [ ] `suicide`, падіння з висоти, вихід за межі бойової зони.
- [ ] Матеріали й множники шкоди (потребує `materialManager`).

### S5. Техніка й спавнери
- [ ] Поява техніки зі спавнерів: шаблон за командою (`setObjectTemplate`).
- [ ] Таймер повернення знищеної техніки.
- [ ] Вхід/вихід із техніки (`PlayerControlObject`, entry points).

### S6. Рахунок і статистика
- [ ] Очки гравця: вбивства, смерті, захоплення, допомога.
- [ ] Табло (`Scoreboard` у HUD).

### S7. Мережа
- [ ] Нові пакети: стан гри, квитки, точки, здоров'я, рахунок.
- [ ] Клієнтський прогноз руху; звірка з сервером.
- [ ] UDP-транспорт (абстракція готова, працює лише петля).

## Ігровий HUD

Дані вже розбираються: `Menu/HUD/HudSetup/HudSetupMain.con` дає **1615 вузлів**
у 100+ групах. Головна група — `IngameHud` (60 вузлів), і майже все в ній —
вузли типу `split`, тобто посилання на інші групи.

### H1. Каркас
- [x] Розкриття `split`-вузлів: `hud::buildTree` іде деревом
      `IngameHud -> під-групи` з захистом від кільця.
- [x] Малювання HUD в ігровій сесії — другим проходом поверх кадру
      (`renderOverlay(..., clear=false)`).
- [x] Текстури інтерфейсу: шлях від `Menu/HUD/Texture/`, підтримка `.tga`
      (у stb лишалися тільки PNG — саме тому HUD був порожній).

### H2. Живі дані
- [x] Квитки обох команд (`FriendlyTicketsString`, `EnemyTicketsString`):
      підпис перебудовується лише коли змінився рядок.
- [ ] Решта значень: здоров'я, набій, назва точки, час.
- [ ] Логічні змінні показу (`AND`/`EQUAL` у даних) — зараз вважаються
      вимкненими, тож частина інтерфейсу не показується.
- [ ] Смуги (`Bar`): здоров'я, набій, захоплення точки.
- [ ] `ObjectMarker` і компас — потрібні позиції об'єктів від сервера.
- [ ] Мінімапа: текстура рівня + значки точок і гравців.

### H3. Екрани
- [ ] Екран появи (`SpawnMenu`, `SpawnInfo`) — вибір точки й набору.
- [ ] Табло (`Scoreboard`).
- [ ] Повідомлення (`gamelogic.messages.addMessage`, x288 у грі).

## Виміри готовності

- `command_audit`: зараз **460 232 / 498 035 (92.4 %)**, 243 унікальні команди
  без обробника. Кожен етап має підіймати це число.
- Тести в `tests/` на кожен шматок логіки (квитки, захоплення, шкода).
