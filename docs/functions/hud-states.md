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

| стан | вмикає |
|---|---|
| 0 | `ShowIngameHud`, `MapShow`, `MapBorderShow` — звичайний бій |
| 1 | `ShowIngameHud`, `MapBorderAlternateShow`, `SpawnShow`, `KitsShow` — **екран появи** |
| 2 | `MapShow` |
| 3 | `SquadInterfaceShow` |
| 4 | `RadioInterfaceShow` |
| 5 | `RadioVehicleInterfaceShow` |
| 6 | `SpottedInterfaceShow` |
| 7 | `SquadLeaderInterfaceShow` |
| 8 | `CommanderInterfaceShow` |
| 9 | `ScoreboardShow`, `LevelsListShow` — табло |
| 11 | (нічого не вмикає — усе гасить) |
| 15 | `CommanderShow` |
| 16 | `CommanderRadioShow` |
| 17 | `MapShow`, `SpawnShow`, `MembersShow` |
| 18 | `MembersShow`, `SpawnShow` |
| 19 | `MapMenuShow` |
| 20 | `SquadLeaderMenuShow` |
| 21 | `CommanderMenuShow` |
| 26 | `InviteListShow` |
| 27 | `ChoiceMenuShow` |
| 29 | `SetupShow` |
| 30 | `DemoCameraInterfaceShow` |
| 31 | `DemoRecInterfaceShow` |

Позиції 10, 12–14, 22–25, 28 ведуть до спільного порожнього обробника
`0x786f59`.

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
