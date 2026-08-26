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
