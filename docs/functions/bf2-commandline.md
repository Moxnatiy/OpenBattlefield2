# Командний рядок BF2.exe

Знято з самого бінара, не з форумів. У `BF2.exe` є власна таблиця
прапорців: будівник за адресами `0x409cd0..0x40a660` заповнює її трійками
`(номер, ім'я, опис)`, а розбір крутиться навколо `jmp *0x40bbe8(,%eax,4)`
за адресою `0x40a697` — тобто **номер прапорця є індексом у таблиці
переходів**. Верхня межа перевіряється тут же: `cmp $0x49, %eax`.

## Таблиця (номер — ім'я — власний опис гри)

| № | прапорець | опис |
|---|---|---|
| 0x00 | `dedicated` | Start in dedicated server mode |
| 0x01 | `multi` | Allow starting multiple BF2 instances |
| 0x02 | `joinServer` | Join a server by ip address or hostname |
| 0x03 | `hostServer` | *(без опису — прихований)* |
| 0x04 | `playerName` | Set the player name |
| 0x05 | `password` | Set the server password when joining a server |
| 0x06 | `checkForAvailablePatch` | |
| 0x07 | `checkForPatch` | |
| 0x08 | `config` | Sets path to the ServerSettings.con file to use |
| 0x09 | `mapList` | Sets the path to the MapList.con file to use |
| 0x0a | `lowPriority` | Run the game with slightly lower priority |
| 0x0b | `loadLevel` | Set the level to load |
| 0x0d | `ai` | |
| 0x0e | `wx` | Position game window on the screen at certain x-position |
| 0x0f | `wy` | Position game window on the screen at certain y-position |
| 0x10 | `szx` | Set resolution witdth *(друкарська помилка їхня)* |
| 0x11 | `szy` | Set resolution height |
| 0x14 | `fullscreen` | Start game in full screen mode |
| 0x15 | `noSound` | Start game without sound |
| 0x27 | `demo` | Sets the con-file with demo options |
| 0x2c | `maxPlayers` | Sets max players. |
| 0x2d | `gameMode` | Sets the game mode. |
| 0x2e | `modPath` | Set the mod path (default mods/bf2) |
| 0x39 | `help` | Displays this help |
| 0x3a | `?` | Same as +help |
| 0x3b | `ranked` | Allows gamespy snapshot sending |
| 0x3d | `playerPassword` | Set the player password |
| 0x41 | `playNow` | use playnow functionality |
| 0x42 | `port` | specifies the network port to be used |
| 0x43 | `pbPath` | Set the path to use for PunkBuster… |
| 0x46 | `restart` | Used when restarting executable. *(пропускає заставки)* |
| 0x47 | `rsconfig` | Sets path to the ReservedSlots.con file to use |
| 0x48 | `skipDXCheck` | Skips DirectX version check. Use with caution. |
| 0x49 | `dropDynamicSpawns` | Don't re-add dynamic spawn groups as round (re)starts. |

**`menu` серед прапорців немає.** Ім'я, яке кочує форумами, у таблиці
відсутнє — гра його просто не знає.

## Як прапорці стають налаштуваннями

Обробники короткі й однакові за будовою:

* `hostServer` (0x03, `0x40a7b8`) — аргументу не бере, лише зводить
  локальний прапорець. **Типове значення — вже увімкнене** (`0x409cc8`),
  тож окремо його передавати не треба;
* `loadLevel` (0x0b, `0x40a888`) — кладе назву рівня в буфер кадру;
* наприкінці розбору (`0x40ba44`) прапорець «хост» вирішує, куди піде
  назва: хост → `GSLoadLevel`, інакше → `GSJoinAddress`.

## Чому гра пропускає меню

Перевірка стоїть одним місцем, `0x401faa..0x401fcb`. Меню **не**
показується, якщо справдилося бодай одне:

* `GSLoadLevel` непорожній (його кладе `+loadLevel`);
* `GSJoinAddress` непорожній (`+joinServer`);
* `playNow` дорівнює 1;
* `GSDedicated` увімкнений.

Тоді замість реєстрації меню йде виклик `0x404aa0(0, 1)`.

Робочий рядок, перевірений на живій грі (відкриває
`Levels/Dalian_plant/{client,server}.zip`):

```
BF2.exe +fullscreen 0 +szx 1024 +szy 768 +restart 1
        +playerName defaultPlayer
        +loadLevel dalian_plant +gameMode gpm_cq +maxPlayers 16
```

## Внутрішні перемикачі (у довідку не потрапили)

`GSDumpAllConFiles`, `GSCustomConFile`, `GSFileChangeMonitor`,
`GSDisableShaderCache`, `GSDebugGhostManager`, `GSDebugNetwork`,
`hack-ignore-asserts`, `swiffDebug`, `disable-swiff`, `keepAINav`,
`gameName`, `coll-load-debugmeshes`, `phy-convert-collision-meshes`.
Історію введених команд гра сама пише в `Logs/BfCommandHistory.con`.

## Профіль

Локальний профіль гра бере з `Documents/Battlefield 2/Profiles`:
`Global.con` містить `GlobalSettings.setDefaultUser "0001"`, а сам
профіль лежить у теці `0001`. Вікно входу, яке видно при старті з меню,
це вхід до **онлайн-акаунта**, а не вибір локального профілю — з
`+loadLevel` воно не з'являється, бо меню взагалі не піднімається.
