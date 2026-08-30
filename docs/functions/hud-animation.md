# Анімація HUD: поява і зникнення вузлів

HUD у Refractor 2 не статичний. Кожен вузол з'являється і зникає **у
часі**, і робить це не окремий код HUD, а той самий граф MemeFile, що й
меню (`docs/formats/hud-meme.md`).

## Що видно з бінара

Будівник (`C:\dice\...\Code\BF2\Menu\Bf2HudBuilder.cpp` — шлях у самому
бінарі) створює для вузла ланцюжок вузлів графа з іменами за шаблоном:

| Шаблон | Де в BF2.exe | Коли створюється |
|---|---|---|
| `%sCullNode` | 0x79b2c0, 0x79db80 | разом із вузлом; це його «показувати чи ні» |
| `%sAlphaShowEffect` | 0x79db80 (рядок 0x933690) | `addNodeAlphaShowEffect` |
| `%sMoveEffect` | 0x79b2c0 | `addNodeMoveShowEffect` |

Обидві команди спершу перевіряють, що такого вузла ще нема (інакше в лог
іде `already has an alphaShowEffect!` / `already has a MoveShowEffect!`,
рядки 0x933670 і 0x93347c), потім шукають `%sCullNode` і чіпляють новий
вузол до нього. Тобто **ефект — це дитина cull-вузла**: cull каже, куди
йти, ефект — за скільки.

`addNodeMoveShowEffect <кут> <відстань>` збирає два вузли-дані
(0x82dfc0 для float, 0x82d8c0 для int) і склеює їх у клас із RTTI-іменем
**`dice::meme::Bf2MoveEffect`** (0x937a44, vtable 0x937ad8). Поряд у
бінарі лежить окремий клас **`dice::meme::Bf2SinMoveEffect`** (0x937a60,
vtable 0x937b38) — і саме тому звичайний рух ми вважаємо **рівномірним**:
синусоїда в грі є, але це інший клас, який ця команда не створює.

Часи дає пара команд `setNodeInTime` / `setNodeOutTime` (по 231 виклику
в даних), обидві з одним аргументом типу float.

## Напрям руху — перевірений даними, не вгаданий

`HUD/HudSetup/HudElementsLevelsList.con`: панель голосування за карту
стоїть на y = 377..383 у базових 800x600 і має

```
hudBuilder.setNodeInTime  0.3
hudBuilder.setNodeOutTime 0.3
hudBuilder.addNodeMoveShowEffect -1.57 376
```

Кут -1.57 — це -pi/2. Екранна вісь y дивиться вниз, тож формула
`dy = sin(a) * distance` дала б старт на y ~ 1, тобто **над** екраном; а
`dy = -sin(a) * distance` дає y ~ 753, тобто панель виїжджає **знизу**,
з-за краю 600-піксельного екрана. Друге — те, що видно в грі.

Перевірка на іншому місці: `HudElementsPlayer.con` — смуга здоров'я
(ліворуч) має `3.14 53`, тобто dx = -53, приїжджає зліва; витривалість
(праворуч) має `0 53`, dx = +53, приїжджає справа. Правило те саме.

Отже: `offset = (cos a, -sin a) * distance * (1 - progress)`.

Усі 94 виклики в 18 файлах мають рівно два аргументи; кути в даних —
`3.14`, `0`, `-0.7`, `-1.57`.

## Що з цього зроблено

`src/hud/animation.h` + `animation.cpp`: `Animator` тримає хід 0..1 на
вузол, веде його до 1 за `inTime` і до 0 за `outTime` рівномірно;
`alpha`-ефект множить прозорість на хід, `move` — зсуває вузол за
формулою вище. Вузол, про який аніматор чує вперше, ставиться відразу в
кінцевий стан: інакше весь HUD в'їжджав би на старті рівня.

Вузол **без** жодного ефекту переходу не має: він просто з'являється і
зникає, як і раніше.

## Чого ще нема

* `addNodeVariableMoveShowEffect` (рядок 0x92bd84) — рух, у якого
  відстань бере зі змінної. У даних не трапляється.
* `dice::meme::Bf2SinMoveEffect` — хто його створює, ще не знайдено.
* Самі дії графа (`SetVariableSineAction` зі швидкістю 600,
  `SetVariableSoftAction` зі швидкістю 10 у `Menu/Ingame`) поки не
  виконуються: змінні HUD ми ставимо стрибком.

## Сторона команди: звідки береться значок

Це не HUD-дані, а рівень. `Init.con` кожного рівня має

```
gameLogic.setTeamName 1 "CH"
gameLogic.setTeamName 2 "US"
```

і саме цей рядок гра підставляє в шаблони замість `%s`:

| Шаблон | Адреса рядка | Де заповнюється |
|---|---|---|
| `Ingame/Flags/Icons/Minimap/%s/miniMap_CP.tga` | 0x925af8 | 0x74fb70 |
| `Ingame/Flags/Icons/Minimap/%s/miniMap_CPBase.tga` | 0x925ac4 | 0x74fb70 |
| `Ingame/Flags/Icons/Minimap/%s/miniMap_flag.tga` | 0x925b5c | 0x74fb70 |
| `Ingame/Flags/Icons/Hud/Score/%s/scoreBoard_Flag.tga` | 0x931030 | 0x787260 |
| `Levels/%s/Hud/Minimap/ingameMap.tga` | — | 0x74fb70 |

Підставляється результат `gameLogic->vtbl[0x48](номер команди)` — за
0x74fc58 виклик із 1, за 0x74fca0 з 2. Для нульової (нічийної) сторони в
0x74fb70 стоїть окремий рядок з готовим `Neutral` (0x925b28).

Назви команд по всіх 22 рівнях гри — рівно **CH, EU, MEC, US**, а теки
значків у `Menu_client.zip` — **Ch, Eu, Mec, US, Neutral**. Тобто тека і
є назвою сторони; жодного власного відображення «команда -> сторона» в
грі немає, і робити його не треба.

Для Dalian_plant це означає, що **перша команда китайська, а друга
американська** — у нас було навпаки, і прапорці на карті стояли не на
своїх точках.

### Підпис вкладки

`Team1NameString` / `Team2NameString` заповнює 0x787260 через 0x787110,
і та функція — просто перелік:

```
name = gameLogic.teamName(team)
"MEC" -> HUD_TEXT_MENU_SPAWN_ARMY_MEC
"US"  -> HUD_TEXT_MENU_SPAWN_ARMY_USMC
"CH"  -> HUD_TEXT_MENU_SPAWN_ARMY_CHINA
інше, непорожнє -> "HUD_TEXT_MENU_SPAWN_ARMY_" + name
порожнє -> порожній рядок
```

Тобто EU потрапляє в загальну гілку і дає
`HUD_TEXT_MENU_SPAWN_ARMY_EU`. Та сама функція ставить і пару
`FriendlyFlagIconPathString` / `EnemyFlagIconPathString` — її перші два
аргументи це команда гравця і протилежна.

## Рухомі кутові ділянки

Широка плашка під здоров'ям — це вузол

```
hudBuilder.createPictureNode BottomLeftAnimateHud BottomLeftBar -103 -2 400 39
hudBuilder.setPictureNodeTexture Ingame/Bars/healthBackGround.tga
hudBuilder.setNodeAlphaVariable  MenuBackgroundAlpha
```

Змінної показу в нього **немає взагалі**, а `MenuBackgroundAlpha` типово
0.7 (стала 0x3f333333 за 0x46928a, і той самий 0.7 стоїть у повзунку в
`HudElementsPlayer.con`). Тобто самим прапорцем його не сховати — і в
оригіналі його ховає інше: **від'їзд усієї ділянки**.

У `Menu/Ingame` X цих ділянок — не стала, а змінна графа, і у файлі
збережене саме сховане положення:

| Змінна | У файлі | Що це |
|---|---|---|
| `BottomLeft/BottomLeft_XPos` | -295 | сховано |
| `BottomLeft/BottomLeft_nextXPos` | -295 | сховано |
| `BottomRight/BottomRight_XPos` | 503 | сховано |
| `BottomRight/BottomRight_newXPos` | 503 | сховано |
| `BottomRight/BottomRight_oldXPos` | 201 | показано |

Веде їх `SetVariableSineAction` зі швидкістю 600, під умовою
`AniPos && (BottomRight_alpha == BottomRight_oldAlpha || BottomRight_direction)`.

Плашка 400x39 накриває і солдатську частину зліва, і транспортну справа
(`BottomLeftSecondaryHealth` у `Vehicles/HudElementsVehicleBasic.con`
починається з x = 149) — тому на екрані появи вона й виглядала як
розтягнутий транспортний варіант. При X = -295 вона цілком за краєм
екрана.

**Джерело не знайдене:** хто саме пише `BottomLeft_nextXPos` і
`BottomRight_direction`. Прив'язку видно (0x789480 зв'язує їх із полями
об'єкта HUD за шаблоном «група + ім'я вузла»), але місце запису — ні.
Поки ділянки їдуть за тією ж умовою, що й сам бойовий HUD.
