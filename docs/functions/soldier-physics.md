# Фізика солдата: сталі й модель зіткнень

Усе нижче зчитано з Linux-сервера, а не вгадано.

## Звідки взялися числа

Рушій реєструє свої змінні через `dice::hfe::Vars::getFloat(ім'я, типове)`.
У коді це виглядає як пара інструкцій — адреса рядка й значення float, —
тож усі типові значення можна витягти механічно:

```bash
python tools/dwarf/dump_vars.py "Game Files/OtherFiles/linuxded/bin/ia-32/bf2" soldier
```

Дані гри можуть їх перевизначати (`objects/soldiers/common/common.con`
ставить `phy-soldier-deceleration 0.4` замість типових 0.2), і тоді
виграють дані — рівно як в оригіналі.

## Форма солдата — стовпчик сфер

`SoldierResponsePhysics::getSoldierHeight` (0x08378330) повертає висоту й
**кількість сфер** за позою:

| Поза | Висота | Сфер |
|---|---|---|
| стоїть | `coll-soldier-stand-height` = **1.7** | **5** |
| присів | `coll-soldier-crouch-height` = **1.4** | **3** |
| лежить | `coll-soldier-prone-height` = **0.8** | **1** |

Крок між центрами: `(висота - 2 * радіус) / (кількість - 1)`.
Радіус — `coll-soldier-radius` = **0.25**.

Для стійки це дає сфери на висотах 0.25, 0.55, 0.85, 1.15, 1.45. Саме
через стовпчик сфер солдат може зійти на сходинку: нижня сфера впирається
в підйом і виштовхується **вгору**, а не вбік. Одна сфера «на рівні
грудей» такого зробити не може в принципі.

Файл в оригіналі — `Physics/SoldierResponse.cpp` (видно з рядка налагодження).

## Сталі

| Змінна | Значення | Що це |
|---|---|---|
| `coll-soldier-radius` | 0.25 | радіус сфери |
| `coll-soldier-stand/crouch/prone-height` | 1.7 / 1.4 / 0.8 | висота за позою |
| `coll-soldier-pivot-height` | 1.0 | висота точки обертання |
| `coll-soldier-collision-test-count` | 8 | скільки разів виштовхувати за такт |
| `coll-soldier-extend-ray` | 0.9 | наскільки подовжити промінь під ноги |
| `phy-soldier-feet-level` | -0.04 | де вважається «підлога» відносно ніг |
| `phy-soldier-feet-contact-normal` | 0.5 | мінімальний Y нормалі, щоб поверхня тримала (нахил до 60°) |
| `phy-soldier-walk-speed` | 1.5 | крок |
| `phy-soldier-run-speed` | 3.9 | біг |
| `phy-soldier-sprint-speed` | 7 | спринт |
| `phy-soldier-crouch-speed` | 2 | присівши |
| `phy-soldier-crawl-speed` | 0.8 | лежачи |
| `phy-soldier-swim-speed` | 2.1 | плавом |
| `phy-soldier-swimcrawl-speed` | 3.645 | плавом швидко |
| `phy-soldier-inair-speed` | 2 | керування в повітрі |
| `phy-soldier-start-float` | 0.99 | частка висоти у воді, з якої солдат спливає |
| `phy-soldier-stop-float` | 0.9 | і з якої знову стає на дно |
| `phy-soldier-sprint-limit` | 0.5 | запас витривалості |
| `phy-soldier-sprint-dissipation-time` | 10 | за скільки секунд витрачається |
| `phy-soldier-sprint-recover-time` | 120 | і за скільки відновлюється |
| `phy-soldier-friction` / `elasticity` / `resistance` | 1 / 0 / 0.02 | відгук зіткнення |
| `soldier-drown-damage` | 8 | шкода під водою за секунду |
| `soldier-prone-inwater-limit` | 1.1 | глибина, за якої не можна лежати |

Повний вивід — `docs/reference/soldier-vars.txt`.

## Множники повороту

`Soldier::handlePlayerInput` множить ввід огляду на два глобальні
множники (лінукс-сервер, 0x54f63c і 0x54f666):

```
поворот_x = ввід_x * g_soldierLookAroundX
поворот_y = ввід_y * g_soldierLookAroundY
```

Обидва читаються з `Vars` у статичному ініціалізаторі (0x5476f8 і
0x54770a) з **типовим значенням 5.0** (стала за 0xb34b64):

* `phy-soldier-look-factor-x`
* `phy-soldier-look-factor-y`

У даних гри цих змінних немає, тож лишається типове 5.0.

Окремо, у `Settings/Controls.con` є `ControlMap.mouseSensitivity` — 1.7
для піхотної розкладки і 3 для іншої.

### Чутливість миші — з клієнта

`ControlMap` тримає чутливість полем **+0x50**, а чутливість клавіатури —
**+0x54**. Видно це там, де рушій **записує** `Controls.con` назад
(`BF2.exe`, 0x6b03ed і далі): він пропускає рядок, коли значення дорівнює
типовому, і саме те порівняння називає типове значення:

```
006b03ed CMP dword ptr [EBX + 0x50], 0x3f800000   ; mouseSensitivity, типове 1.0
006b0401 PUSH 0x912504                            ; "ControlMap.mouseSensitivity "
006b041c CMP dword ptr [EBX + 0x54], 0x3dcccccd   ; keyboardSensitivity, типове 0.1
```

У даних гри `mouseSensitivity` задано двом розкладкам: 1.7 піхотній і 3
гелікоптерній. Але мишу в огляд солдата переводить **не вона**:
`c_PIMouseLookX/Y` висять на миші в `defaultPlayerInputControlMap`
(`Settings/Controls.con`, 248–251), а там `mouseSensitivity` не задано —
отже діє типова **1.0**.

**Чого ще бракує.** Ланцюг «пікселі → значення осі → кут» має ще одну
ланку: множник 5.0 не може бути градусами на такт (у знятому трафіку вісь
миші доходить до 169). Треба знайти, що робить із добутком
`Soldier::handlePlayerInput` **у клієнті** — множить на час кадру, ділить
на сталу чи додає як швидкість. Доки цього немає, наш множник лишається
**невиміряним**, і повний оберт мишею не дає повного оберту.
