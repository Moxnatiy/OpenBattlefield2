# Netcode: перший зріз

Статус: **BitStream реалізовано** — `src/net`. Протокол розібрано на рівні
примітивів і заголовків; логіка з'єднання ще попереду.

## Головна знахідка: лінукс-сервер має символи

`bf2-linuxded-1.5.3153.0-installer.sh` розпаковується без запуску (це
makeself-архів; корисне навантаження починається після 375-го рядка). Усередині:

```
bin/ia-32/bf2    16 МБ   ELF 32-bit, with debug_info, not stripped
bin/amd-64/bf2   18 МБ   ELF 64-bit, with debug_info, not stripped
```

**68 921 символ, 16 830 функцій, повний DWARF** — і та сама версія 1.5, що й
`BF2.exe`. Це незрівнянно краще за реверс Windows-бінаря без символів:

```
dice::hfe::io::BitStream::writeUnsigned(unsigned int, unsigned int, unsigned int)
dice::hfe::io::BitStream::writeCompressedVector(Vec3 const&, Vec3 const&, float)
dice::hfe::io::BitStream::writeUnitQuaternion3Comp(Quat const&, int)
dice::hfe::io::PacketBuffer::pushBack(dice::hfe::io::Packet&)
dice::hfe::io::NetServer::getServerAddress() const
```

Бінар імпортовано в Ghidra (`ghidra_projects/OpenBF2`, програма `bf2`), DWARF
дає не лише імена, а й типи та розкладку структур.

**Висновок:** усю подальшу роботу над netcode вести на лінукс-сервері, а
`BF2.exe` лишити для звірки клієнтської частини.

## Що вміє BitStream в оригіналі

Повний перелік методів із символів:

| Група | Методи |
|---|---|
| Базове | `writeBits`, `readBits`, `writeUnsigned`, `writeSigned`, `skipBits` |
| Вектори | `write/readCompressedVector`, `...Vector2`, `...HighCompressedVector` |
| Нормалі | `write/readNormalVector`, `shrinkNormalVector` |
| Кватерніони | `write/readUnitQuaternion`, `...3Comp` |
| Стан | `setCompressionVector`, `resetCompressionVector`, `m_relativeCompressionEnabled` |

Три статичні таблиці — `m_compressionVectorBitTable`,
`m_compressionVectorBitTable2`, `m_highCompressionVectorBitTable` — задають,
скільки біт іде на компоненту вектора. Їх значення лежать у секції даних
і читаються напряму, без декомпіляції.

Тобто позиції передаються **відносно опорного вектора** зі змінною
точністю, а обертання — трикомпонентними кватерніонами. Це і є причина,
чому трафік BF2 такий щільний.

## Що вже зроблено

`obf2::net::BitStream` реалізує базовий рівень: `writeBits`/`readBits`,
рядки, службові заголовки. Розкладка бітів перевірена тестом і збігається
з [Refractor-2-BitStream-Emulator](https://github.com/matthias-hoste/Refractor-2-BitStream-Emulator)
(робоче рукостискання з реальним сервером, C#):

**У кожному байті молодші біти йдуть першими.** Запис `0b101` у трьох бітах
дає байт `0x05`, а не `0xA0`. Якщо переплутати — протокол розсиплеться на
першому ж пакеті, тому це найважливіший тест у наборі.

Службові заголовки:

```
основний:    4 біти тип + 8 біт підтип
розширений:  6 біт тип + 6 біт id + 32 біти порядковий номер
```

Типи пакетів (з того ж проєкту, підтверджені роботою з живим сервером):

| Код | Пакет |
|---:|---|
| 1 | ConnectionRequest |
| 2 | ConnectionAccept |
| 3 | ConnectionDenied |
| 4 | ConnectionAcknowledge |
| 5 | Disconnect |
| 7 | PingRequest |
| 8 | PingResponse |
| 15 | Data |

На відміну від оригіналу, наш читач **перевіряє межі**: пакет приходить
з мережі, і вихід за буфер тут неприпустимий. Тести це покривають окремо.

## Реплікація частково лежить у відкритих даних

`Common/Networkables.con` описує, як саме об'єкти синхронізуються:

```
NetworkableInfo.createNewInfo HandFireArmsInfo
NetworkableInfo.setPredictionMode PMNone
NetworkableInfo.setForceNetworkableId 1

NetworkableInfo.createNewInfo ProjectileInfo
NetworkableInfo.setPredictionMode PMLinear
NetworkableInfo.setBasePriority c_NIGhostAlways
```

Тобто режим передбачення (`PMNone`, `PMLinear`), пріоритет і сталі
мережеві id задані **даними**, а не кодом. Це той самий підхід, що й з
рівнями: частину протоколу можна відновити без жодного реверсу.

## Далі

1. Компресія векторів і кватерніонів — таблиці бітів читаються з даних ELF.
2. Рукостискання: ConnectionRequest → Accept → Acknowledge.
3. `NetworkableInfo` як реєстр реплікації поверх `ObjectTemplate`.
4. Сумісність із оригінальними серверами — **рішення не ухвалене**, див.
   `docs/TODO.md`.
