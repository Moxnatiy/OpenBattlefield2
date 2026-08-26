# HUD-змінні: що саме рушій подає в інтерфейс

Джерело: `BF2.exe`, `FUN_00789480` (реєстрація змінних інтерфейсу, викликається
один раз при створенні HUD). Функція нічого не рахує — вона лише прив'язує
**ім'я змінної** до поля структури стану, з якого HUD потім читає значення.
Тому це готовий список того, що клієнт має вміти заповнювати.

Типи видно з того, який слот таблиці викликається:
`+0x10` — прапорець (показ/блимання), `+0x1c` — ціле, `+0x28` — дріб,
`+0x34` — рядок, `+0x40` — локалізований рядок.

## Гравець

| Змінна | Тип | Що це |
|---|---|---|
| `PlayerHealth` | дріб | здоров'я 0..1 — заповнення смуги |
| `PlayerHealthString` | рядок | те саме числом |
| `PlayerStamina` | дріб | витривалість (біг) |
| `PlayerHealthShow`, `PlayerStaminaShow` | прапорець | показувати смугу |
| `PlayerHealthBlink`, `PlayerStaminaBlink` | прапорець | блимання при малому значенні |
| `PlayerHealthColorRed/Green/Blue/Alpha` | дріб | колір смуги здоров'я |
| `PlayerSprintColorRed/Green/Blue/Alpha` | дріб | колір смуги витривалості |
| `PlayerAngle`, `PlayerBanking`, `PlayerElevation` | дріб | орієнтація |
| `UsingParachute` | прапорець | парашут розкрито |
| `KilledState` | прапорець | гравця вбито |
| `LocalKitNameString` | рядок | назва набору |
| `CloseToMedic`, `CloseToRepair`, `CloseToAmmo` | прапорець | значки допомоги поруч |
| `NightVisionGaugeValue/Show/Blink` + кольори | — | прилад нічного бачення |

## Техніка

`VehicleArmor` (+`String`, `Show`, `Blink`, кольори), `VehicleStamina`,
`VehicleAngle`, `VehicleTurretAngle`, `VehicleBanking`, `VehicleElevation`,
`VehicleElevationSpeedAngle`, `GunnerAngle`, `Torque`, `TorqueAngle`,
`TorqueString`, `AngleOfAttack`, `AltitudeString`, `SpeedString`,
`VehicleIconShow`, `VehicleIconPathString`, `VehicleNameString`,
`VehiclePassengersShow`, `TurretIconShow`, `WarningIconShow`, `WarningIconPath`.

## Загін і пасажири

`SquadInfoBarShow`, `SquadInfoText` (локалізований), `SquadInfoIconPath`,
`ShowCommanderIcon`, `ShowSquadIcon`, а також десять наборів
`OccupyingPlayerName%iShow/String`, `OccupyingPlayerPos%iX/Y`,
`OccupyingPlayer%iColorRed/Green/Blue` — імена гравців у техніці.

## Анімовані шари

Окремо реєструються пари «змінна -> анімація»:
`BottomLeft_alpha1/alpha2/nextAlpha1/nextAlpha2` -> `BottomLeft/Alpha`,
`BottomLeft_XPos`/`BottomLeft_nextXPos` -> `BottomLeft`.

Це і є відповідь на питання, чому кутові шари (`BottomLeftStatic`,
`BottomRightAnimate`, `TopLayer`) ніде не позиціонуються в `.con`: їхнє
положення й прозорість веде анімаційна система рушія. Самих імен груп
(`BottomLeftStatic`, `TopLayerHud`) **немає в жодному бінарі гри** — рушій
складає їх із частин, тож пошуком за рядком їх не знайти. Щоб відтворити
розкладку 1:1, треба розібрати менеджер HUD, а не шукати сталі.
