# Що робить інтерфейс: перелік консольних команд

Кнопка в HUD не має власної логіки. Вона виконує **звичайну консольну
команду**, задану в даних:

```
hudBuilder.createButtonNode Kit0NotSelected SelectKit0 10 73 246 69
hudBuilder.setButtonNodeConCmd "spawnManager.setPlayerKit 0"
```

Тобто «бекенд» інтерфейсу — це не окрема система, а рівно той перелік
команд, який гра вішає на кнопки. Його й треба реалізувати, а не
вигадувати логіку заново.

Три команди задають дію:

| команда | скільки | що |
|---|---|---|
| `setButtonNodeConCmd` | 381 | ліва кнопка |
| `setButtonNodeAltConCmd` | 30 | права |
| `setListNodeConCmd` | 15 | рядок списку (перед командою йде номер) |

Разом **426 викликів, 191 різна команда, 22 об'єкти**. Знімається
`tools/hud_commands.py`.

## Весь перелік, за об'єктами

| об'єкт | викликів | методи |
|---|---|---|
| `sound` | 181 | `playSound` |
| `CommanderMenu` | 65 | `deselect`, `sendRadioMessage`, `singleClick`, `doubleClick`, `rightClick` |
| `MiniMap` | 20 | `setPaintKit`, `setPaintVehicle`, `setZoom`, `setPaintAllKits`, `toggleShowKits`, `setPaintAllVehicles` |
| `Commander` | 19 | `sendOrder`, `accept`, `satellite`, `toggleArtilleryState`, `toggleUAVState`, `toggleSupplyState`, `commanderResign`, … |
| `spawnManager` | 17 | `setPlayerKit`, `selectNextUnlock`, `setPlayerTeam`, `commitSuicide` |
| `game` | 15 | `radioMessage`, `simulationRate` |
| `SquadLeader` | 13 | `sendOrder`, `sendRequest` |
| `SquadMenu` | 13 | `setShowInviteList`, `setSquadCreateSelect`, `createSquad`, … |
| `Radio` | 12 | `sendSpottedMessage`, `setSpottedMenuActive` |
| `RadioInterface` | 11 | `selectOrder` |
| `RadioVehicleInterface` | 11 | `selectOrder` |
| `SquadLeaderInterface` | 11 | `selectOrder` |
| `hudManager` | 7 | `addFavouriteServer`, `setDone`, `enableSay*ChatBox` |
| `demo` | 6 | `togglePlayerDemo`, `toggleCameraDemo`, … |
| `hudItems` | 5 | `setBool` |
| `levelsList` | 4 | `setVoteMapShow`, `singleClick` |
| `CommanderInterface` | 4 | `selectOrder` |
| `SquadInterface` | 4 | `selectOrder` |
| `scoreboard` | 3 | `setToggleShow` |
| `Scoreboard` | 2 | `singleClick` |
| `SpawnManager` | 2 | `toggleMembers` |
| `Minimap` | 1 | `setZoom` |

Регістр першої літери в даних неусталений (`Scoreboard` і `scoreboard`,
`MiniMap` і `Minimap`, `SpawnManager` і `spawnManager`) — консоль гри
його не розрізняє, тож і ми не маємо.

## Найближче: екран появи

Щоб він запрацював повністю, потрібно всього сім методів:

| команда | що робить |
|---|---|
| `spawnManager.setPlayerKit <0..6>` | вибрати клас |
| `spawnManager.setPlayerTeam <1\|2>` | вибрати команду |
| `spawnManager.selectNextUnlock <0..6>` | стрілка розблокування |
| `spawnManager.commitSuicide` | самогубство |
| `SpawnManager.toggleMembers <0\|1>` | вкладки KIT / SQUAD |
| `hudManager.setDone 1` | кнопка DONE |
| `sound.playSound <ім'я>` | звук натискання |

Механізм натискання в нас уже є — `hud::buttonAt` шукає кнопку під
курсором, а `engine.console().executeLine` виконує її команду; так
працює головне меню. Лишається під'єднати його до екрана появи й
написати ці сім обробників.
