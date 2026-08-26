# Система анімацій солдата

Уся вона **описана в даних гри**, а не в коді: `soldiers/Common/Animations/
AnimationSystem3p.inc` (728 рядків) плюс `ValueHolders.inc` (66). Це
звичайний `.con`, тож читає його наш же інтерпретатор.

Для Dalian Plant це дає: **78 анімацій, 57 бандлів, 62 тригери, 31
діапазон**, і рівно один корінь — `completeTree`.

## Дерево

```
completeTree
  root
    pose            [PoseTrigger]  -> stand | crouch | prone | swim
  specialMoves      -> proneToStill, stillToProne, reviveOnBack ...
  postRoot -> faceRoot -> face_neutral, face_anger [MessageTrigger] ...
  hit               -> hitFrontHead [RandomTrigger] ...
  die               -> standDie, crouchDie, face_dead ...
```

## Як обходиться (реверс)

`Trigger::update` спершу питає дітей, а потім додає свої бандли. Типи
відрізняються лише умовою:

**`PoseTrigger::update` (0x08353040)** — бере дитину **за номером пози**:
```
index = поза; якщо index >= кількість дітей: index = кількість - 1
якщо діти[index]->update() дало false -> нічого не застосовувати
```
Порядок дітей у даних — `stand, crouch, prone, swim`, тобто номери поз
збігаються з тими, що у фізиці (`SoldierResponsePhysics::getSoldierHeight`
для пози 3 бере висоту присідання — це якраз плавання).

**`MovementTrigger::update` (0x08353d70)** — спершу маски повідомлень
(вимагається одна, забороняється інша), потім `isWithinRange(швидкість)`,
і лише тоді звичайний обхід.

**`MovementTrigger::isWithinRange` (0x08353d00)**:
```
якщо діапазону немає -> так
a == b -> так
a >= 0 -> межі [a, b], інакше [b, a]
поза межами -> ні
```
Тобто **перші два числа `AnimationValueHolder.values` — це межі**, а третє
рушій використовує окремо. Від'ємні діапазони записані навпаки
(`3p_turn -1 -3 -10`), і саме знак першої межі їх розрізняє.

## Числа сходяться з фізикою

| Діапазон | Значення | Порівняння |
|---|---|---|
| `3p_stand_walk` | 0.1 .. 1.5 | `phy-soldier-walk-speed` = **1.5** |
| `3p_stand_run` | 0.1 .. 3.9 | `phy-soldier-run-speed` = **3.9** |
| `3p_sprint` | 5.5 .. 6.3 | `phy-soldier-sprint-speed` = 7 |

Межі анімацій — це ті самі швидкості, що ми раніше витягли з рушія
(`docs/functions/soldier-physics.md`). Дві незалежні дороги дали одні й ті
самі числа.

## Перевірити

```bash
anim_info "Game Files/mods/bf2" objects/soldiers/Common/Animations/AnimationSystem3p.inc 0 0
anim_info "Game Files/mods/bf2" objects/soldiers/Common/Animations/AnimationSystem3p.inc 0 3.9
```

На нулі вибирається `stand_rightFootBack` (`3p_stand.baf`), на 3.9 до
нього додається `stand_run` — рівно як і має бути.

## Чого ще немає

Умови, які ми поки не моделюємо: повідомлення (`MessageTrigger`),
випадковий вибір (`RandomTrigger`), простій (`IdleTrigger`), напрямок
(`ForwardTrigger`/`SideTrigger` дивляться не на модуль швидкості, а на її
складову). Через це у вибірці трапляються зайві бандли на кшталт
`skydive` — вони відсіюються саме тими умовами.

Також немає часу: бандли мають `fadeInTime`/`fadeOutTime` й свою довжину,
а програвання з переходами веде `BundlePlayer`.
