# Стани HUD

`hudBuilder` тільки будує дерево, а вирішує, що з нього видно, окрема
машина станів у коді. Це відповідь на питання «чому екран появи в грі
не тримають клавішею»: він не прив'язаний до клавіші взагалі — це
**стан**.

## Де вона

Дві функції в BF2.exe працюють через таблиці переходів на 32 позиції:

* `0x7862ce` — бере стан віртуальним викликом `*0x1e4(%edx)`, таблиця
  `0x786f88`;
* `0x786751` — бере стан аргументом, таблиця `0x787008`.

Кожен обробник — довгий ланцюжок однакових трійок:

```
push  $0 або $1          значення
sub   $0x1c, %esp        будує рядок
push  $0x89f194          ім'я змінної
call  *0x87f46c          конструктор рядка
call  *0xc(%ebx)         setVariable(ім'я, значення)
```

Обробники стоять один за одним і **провалюються** далі: кожен вхід
вимикає все, що нижче за ним, а потім вмикає своє. Тому 32 позиції
таблиці дають лише 24 різні входи.

## Таблиця

Знята з бінара скриптом `tools/hud_states.py` (він читає таблицю переходів
і розбирає ланцюжки блоків), а не переписана з рук:

| стан | обробник | вмикає | гасить |
|---|---|---|---|
| 0 | 0x786761 | `ShowIngameHud`, `MapShow`, `MapBorderShow` | 4: `ScoreboardShow`, `SpawnShow`, `CommanderInterfaceShow`, `CommanderShow` |
| 1 | 0x78683e | `ShowIngameHud`, `MapBorderAlternateShow`, `SpawnShow`, `KitsShow`, `MapMenuShow` | 2: `ScoreboardShow`, `MembersShow` |
| 2 | 0x78682d | `MapShow` | 0: — |
| 3 | 0x786a2d | `SquadInterfaceShow` | 0: — |
| 4 | 0x786a4f | `RadioInterfaceShow` | 0: — |
| 5 | 0x786a60 | `RadioVehicleInterfaceShow` | 0: — |
| 6 | 0x786a71 | `SpottedInterfaceShow` | 0: — |
| 7 | 0x786a1c | `SquadLeaderInterfaceShow` | 0: — |
| 8 | 0x786a3e | `CommanderInterfaceShow` | 0: — |
| 9 | 0x786adf | `ScoreboardShow`, `LevelsListShow` | 0: — |
| 10 | 0x786f59 | *(порожній)* | |
| 11 | 0x786b09 | `SetupShow` | 21: `ShowIngameHud`, `SpawnShow`, `RadioInterfaceShow`, `SpottedInterfaceShow`, `RadioVehicleInterfaceShow`, `SquadInterfaceShow`, `SquadLeaderInterfaceShow`, `CommanderInterfaceShow`, `MapMenuShow`, `SquadLeaderMenuShow`, `CommanderMenuShow`, `ChoiceMenuShow`, `CommanderRadioShow`, `ScoreboardShow`, `LevelsListShow`, `RenameSquadShow`, `VictoryShow`, `VictoryRankShow`, `VoipListShow`, `InviteListShow`, `CommanderShow` |
| 12 | 0x786f59 | *(порожній)* | |
| 13 | 0x786f59 | *(порожній)* | |
| 14 | 0x786f59 | *(порожній)* | |
| 15 | 0x786aa4 | `CommanderShow` | 1: `SpawnShow` |
| 16 | 0x786988 | `CommanderRadioShow` | 0: — |
| 17 | 0x7869c0 | `MapShow`, `SpawnShow`, `MembersShow` | 1: `KitsShow` |
| 18 | 0x786999 | `MembersShow`, `SpawnShow` | 1: `KitsShow` |
| 19 | 0x786944 | `MapMenuShow` | 0: — |
| 20 | 0x786955 | `SquadLeaderMenuShow` | 0: — |
| 21 | 0x786966 | `CommanderMenuShow` | 0: — |
| 22 | 0x786f59 | *(порожній)* | |
| 23 | 0x786f59 | *(порожній)* | |
| 24 | 0x786f59 | *(порожній)* | |
| 25 | 0x786f59 | *(порожній)* | |
| 26 | 0x786ace | `InviteListShow` | 0: — |
| 27 | 0x786977 | `ChoiceMenuShow` | 0: — |
| 28 | 0x786f59 | *(порожній)* | |
| 29 | 0x786d22 | `SetupShow` | 0: — |
| 30 | 0x786a82 | `DemoCameraInterfaceShow` | 0: — |
| 31 | 0x786a93 | `DemoRecInterfaceShow` | 0: — |

Позиції 10, 12–14, 22–25 і 28 ведуть до спільного порожнього обробника
0x786f59.

## Як перехід влаштований насправді

`HudObject::setState(новий)` — це **0x786260**, і в ній два switch підряд:

1. перший — за **поточним** станом (`[0xa10890]->vtbl[0x1e4]()`): гасить
   те, що показував старий стан;
2. другий — за **новим** (таблиця 0x787008): вмикає своє.

Тобто перехід не лишає хвостів не тому, що обробники провалюються, а
тому, що старий стан спершу прибирає за собою. У нас це зроблено так
само: `applyHudState` спершу знімає всі змінні станів, потім ставить
потрібні.

Стан 11 — окремий: він гасить одразу 21 екран і не вмикає нічого. Це
«прибрати все».

## Похідні змінні: що рушій рахує щокадру

Крім станів, змінні HUD пишуть ще дві функції, і **вони працюють
щокадру**, а не раз при старті рівня. Саме через них у грі за екраном
появи не видно ні смуг здоров'я, ні набоїв.

### 0x466930 — розмір карти

| Змінна | Звідки |
|---|---|
| `MapFullSize` (поле 0x1d7) | `[0xa10890]->vtbl[0x250]()->+0x68c` |
| `MapMinSize` (0x1d8) | те саме, `+0x68d` |
| `MapBorderAlternateShow` (0x1d0) | заперечення `MapMinSize` (0x4669ae, `SETZ`) |
| `MapFullSizeAndSpawnShow` (0x1da) | `MapFullSize && SpawnShow` (0x466935, 0x466950) |
| `MapFullSizeAndNotSpawnShow` (0x1d9) | `MapFullSize && !SpawnShow` |

Тобто `MapFullSizeAndSpawnShow`, яку ми раніше вмикали «щоб було видно
DONE», — це не окрема змінна, а просто «і» двох інших.

### 0x78d0f0 — бойовий набір за поточним гравцем

```
гравець = [0xa08f60]->vtbl[0x30]()
якщо гравця немає                       -> нічого не чіпати
якщо !гравець->vtbl[0x68]()             -> 0x78d2d9
або [0xa10890]->vtbl[0x34c](гравець)    -> 0x78d2d9
інакше                                  -> 0x78d154: PlayerHealthShow = 1
```

`0x78d2d9` гасить одразу `SquadInfoBarShow` (0x295), `ShowCommanderIcon`
(0x296), `ShowSquadIcon` (0x297), поле 0xb8, `PlayerHealthShow` (0x24b) і
`PlayerStaminaShow` (0x245).

`PlayerStaminaShow` до того ж рахується окремо в 0x78acf1 — з порівняння
самої витривалості (поле 0x1ac) зі сталою: смуга з'являється, коли
витривалість не повна.

Набої вмикає оновлення зброї: `PrimaryAmmoBarShow` за 0x7a5bae,
`PrimaryClipsShow` за 0x7a5bb5, `PrimaryAmmoShow` за 0x7a8a18.

## Змінна HUD — це поле об'єкта

Жодного словника змінних у грі немає. Під час запуску `registerVariable`
прив'язує ім'я до **поля** об'єкта HUD, і рушій далі пише саме в поле.
Перевантажень чотири: через таблицю віртуальних методів (`[edx+0x10]`) і
три прямі виклики — 0x466240, 0x4664e0, 0x466630.

`tools/hud_fields.py` дістає весь перелік із бінара (252 змінні з їхніми
зсувами), а `--writers` показує ще й місця, де в поле пишуть — сталу чи
обчислене значення. Саме так знайдено все, що вище.

## Що з цього випливає

* **Екран появи — це стан 1**, а не утримання Enter. Разом зі `SpawnShow`
  він вмикає й `KitsShow`: саме тому в грі одразу видно стовпчик класів,
  і саме тому ми мусили вмикати обидві змінні вручну.
* **Табло — стан 9**, і воно вмикає ще й `LevelsListShow`.
* Кожен стан **гасить** усі змінні нижче себе у ланцюжку, тож переходи
  не лишають хвостів від попереднього екрана.
* `HudState` із `.con` (`setNodeLogicShowVariable EQUAL HudState 0`) —
  це та сама величина; у даних вона трапляється лише двічі, бо решту
  роботи робить не умова у вузлі, а сам перехід стану.
