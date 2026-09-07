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

### Висунуте положення лівої ділянки — не виміряне

Праворуч воно є: знімок кадру оригіналу дає **336.5**, і три різні вузли
на ньому сходяться. Зверніть увагу, що це **не** те саме, що
`BottomRight_oldXPos = 201` у файлі, — отже 201 це якийсь інший стан
(найпевніше, розширений під транспорт), а 336.5 — звичайний піший бій.

Ліворуч виміряного немає. У файлі для лівої ділянки збережене **тільки
сховане** положення: і `BottomLeft_XPos`, і `BottomLeft_nextXPos` там
-295. Значення -1, яке стояло в коді, було взяте з нерухомого шару
`BottomLeftStatic` — до рухомого воно стосунку не має, і при ньому
плашка `healthBackGround` (400x39, у вузлі зсунута на -103) тягнеться до
x = 296, накриваючи місце під транспортні смуги. На екрані це виглядає
як HUD техніки на піхоті.

Дзеркалити праву сторону не можна: ділянки різної ширини (600 проти 400)
і з різним вмістом, тож будь-яке перенесення числа було б підгонкою.

**Що для цього треба:** один знімок кадру оригіналу (`Ctrl+Shift+D`) у
звичайному піхотному бою. У ньому потрібен прямокутник з текстурою
`healthBackGround` — його ліва межа і дає X ділянки:
`X = ліва_межа + 400 + 103`, бо координати в дампі центровані
(`екран = 400 + x`), а сам вузол зсунутий на -103.

## Кутові панелі рухає граф, а не наш хід за inTime

Це головна розбіжність із оригіналом, і вона не в кривій, а в **механізмі**.

`Menu/Ingame` (`tools/meme_dump.py Ingame`) містить не лише вузли, а й
змінні з діями над ними:

```
класи:  SetVariableAction, SetVariableSoftAction, SetVariableSineAction,
        ActionListAction, CullVariableActionNode, AlphaFadeEffect, ...

змінні: BottomLeft/BottomLeft_XPos      BottomLeft/BottomLeft_nextXPos
        BottomRight/BottomRight_XPos    BottomRight/BottomRight_NextPos
        BottomRight/Alpha/BottomRight_alpha ... _nextAlpha, _newAlpha, _oldAlpha
        AniPos, BottomLeftAnimate, BottomRightAnimate
```

Тобто кутові панелі HUD їдуть так: у файлі лежить **змінна** з поточним
X, друга — з цільовим, і дія веде першу до другої. Ніякого
`setNodeInTime` в цьому ланцюжку немає — той керує показом **елементів**
(`addNodeMoveShowEffect`, `addNodeAlphaShowEffect`), а не кутовими
шарами.

Наш `Animator` веде **все** рівномірним ходом за `inTime`/`outTime`. Для
елементів це може бути близько до правди, для кутових панелей — ні.

### Що вже відомо про `SetVariableSoftAction`

`BF2.exe`, фабрика класу за **0x833190**:

```
об'єкт на 0x10 байтів
  +0x0   вказівник на клас (0x94cf98)
  +0x4   змінна, яку ведемо            (нуль при створенні)
  +0x8   ціль                          (нуль при створенні)
  +0xc   **Speed**, типове 100.0        (0x42c80000)
```

Ім'я поля не здогад: серіалізація класу (0x8329f0) записує `+0xc` під
іменем `"Speed"`. У даних `Menu/Ingame` поруч зі змінними стоять
числа 10 — це і є швидкість для кутових панелей.

Сусідній `SetVariableSineAction` — окремий клас (фабрика 0x8331c0,
об'єкт на 0x14 байтів, теж зі `Speed` типово 100.0).

### Чого бракує

**Формули оновлення.** Таблиця класу (0x94cf98, 16 записів) містить
конструктор, ім'я типу, розмір, лічильник посилань і серіалізацію —
але **не** метод, який щокадру рухає змінну: у `SetVariableSoftAction`
і `SetVariableSineAction` усі записи, крім серіалізації, збігаються.
Отже дії виконуються не через цю таблицю, і місце виклику ще не
знайдене.

Доти ми **не вгадуємо криву**: «швидкість 10» можна прочитати і як
`x += (ціль - x) * 10 * dt`, і як рух зі сталою швидкістю 10 одиниць за
секунду — це різні анімації, і різниця видна на око. Мірило: панель
проходить свій шлях (-295 -> робоче X) за той самий час, що в оригіналі,
знятий `Ctrl+Shift+D`.

## Що саме анімується — прочитано з `Menu/Ingame`

`tools/meme_read.py Ingame` розбирає файл цілком (3673 з 3673 байтів), і
всі числа беруться звідти, а не зі знімків:

**Кутові ділянки — рух.** Обидві веде `SetVariableSineAction` зі
швидкістю **600**:

```
SetVariableSineAction {Speed: 600}
  Variable: FloatData «BottomLeft/BottomLeft_XPos»     -295
  Data:     FloatData «BottomLeft/BottomLeft_nextXPos» -295

SetVariableSineAction {Speed: 600}
  Variable: FloatData «BottomRight/BottomRight_XPos»    503
  Data:     ToggleData «BottomRight/BottomRight_NextPos»
              Toggle: BoolData «BottomRight_direction»
              Data 1: FloatData «BottomRight_newXPos»   503
              Data 2: FloatData «BottomRight_oldXPos»   201
```

Сховане положення праворуч — 503, і воно справді з файлу.

**А от 201 — не висунуте положення.** Це початкове значення змінної, яку
гра переписує під час роботи, так само як ліворуч переписує
`BottomLeft_nextXPos`. Висунуте виміряне з дампу кадру оригіналу
(`Ctrl+Shift+D`) і дорівнює **336.5**: на ньому сходяться три різні
вузли (BottomRightBar 301 -> 637.5, ShotSelect 449 -> 785.5, безіменний
16x10 431 -> 767.5), і воно стале в усіх трьох знятих кадрах.

Я спробував замінити його на 201 «бо так у файлі» — і це була помилка,
яку вже раз робили: з 201 плашка набоїв сидить на 135 пікселів лівіше,
ніж в оригіналі. **Вимір оригіналу сильніший за початкове значення
змінної у файлі**, бо змінну гра переписує.

Ліворуч у файлі обидва поля -295: висунуте положення туди не записане,
його пише сама гра у `BottomLeft_nextXPos`. Ця змінна зареєстрована
кодом HUD (`BF2.exe`, 0x789480 — там-таки `BottomLeft_XPos`,
`BottomLeft_nextXPos`, `BottomLeft_alpha1/2`, `BottomLeft_nextAlpha1/2`),
але **місце запису ще не знайдене**, тож ліве висунуте положення
лишається невиміряним.

**Кутові ділянки — прозорість.** Її веде інша дія — `SetVariableSoftAction`
зі швидкістю **10**, чотири штуки:

```
SetVariableSoftAction {Speed: 10}  BottomLeft_alpha1  <- BottomLeft_nextAlpha1
SetVariableSoftAction {Speed: 10}  BottomLeft_alpha2  <- BottomLeft_nextAlpha2
SetVariableSoftAction {Speed: 10}  BottomRight_alpha  <- ToggleData(newAlpha 1.0, oldAlpha)
```

Тобто в оригіналі ділянка не просто їде — вона ще й **проступає**, і
двома різними кривими: рух `Sine`, прозорість `Soft`. У нас прозорість
кутових ділянок не анімується взагалі.
