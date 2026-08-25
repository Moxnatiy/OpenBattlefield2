# Prior art — що вже зроблено спільнотою (станом на 2026-08)

## Реімплементації рушія

| Проєкт | Що це | Статус | Ліцензія |
|---|---|---|---|
| [Project Dalian](https://github.com/chronic8000/ProjectDalian) | Clean-room C++20 recreation Refractor 2, читає ассети з твоєї інсталяції BF2 | v0.5.19-alpha; фази 0-7 готові: PBR-рендер, скелетна анімація, балістика, польотна модель, базовий мультиплеєр | MIT |
| [BattlefieldRespawn](https://github.com/rigred/BattlefieldRespawn) | Data-compatible recreation Refractor/Refractor 2 (сумісний з Project Reality) | ранній | — |
| [breadflowerdos](https://github.com/kiwidoggie/breadflowerdos) | "Decompilation" BF2/2142, modern C++ recreation | ранній | — |

**Висновок:** Project Dalian уже верифікував байт-у-байт: `.staticmesh`,
`.bundledmesh`, `.skinnedmesh`, `.ske`/`.baf` (скелети+анімація), collision
meshes, DDS, zip-архіви, інтерпретатор `.con`/`.tweak`. MIT → можна легально
переиспользовать/звірятись. **Не переRE-имо ці формати з нуля** — беремо їхні
специфікації як стартову точку, RE витрачаємо на те, чого в них нема
(геймплейна логіка, netcode, AI/ботів, скриптинг).

## Мережа / сервіси

- [Refractor-2-BitStream-Emulator](https://github.com/matthias-hoste/Refractor-2-BitStream-Emulator) — емуляція мережевого трафіку R2, база для BF2.
- [Refractor-2-game-engine-extension](https://github.com/BattlefieldRedux/Refractor-2-game-engine-extension) — C++ DLL, ін'єкція через патч IAT у BF2.exe. Корисно для рантайм-інструментації (hook + лог викликів) — швидший спосіб зрозуміти логіку, ніж статична декомпіляція.
- BF2Hub / OpenSpy — заміна GameSpy-мастера.

## Формати / інструменти

- [BfMeshView](http://www.bytehazard.com/bfstuff/bfmeshview/) — відкритий переглядач/редактор мешів BF2, має найповніший опис структур мешів.
- [Classic Battlefield Modding Wiki](https://classic-battlefield-modding.fandom.com/) — головна community-довідка по `.con`, тексутрах, модінгу.
- BF2 має вбудований Python 2.3 — значна частина серверної геймплейної логіки лежить у `python/` як відкриті `.py` (не потребує RE взагалі).

## Що з цього випливає для нашого плану

1. Спершу інвентаризація: скільки логіки взагалі у відкритому вигляді (`python/`, `.con`, `.tweak`) — це «безкоштовні» знання.
2. Формати ассетів — брати з Dalian/BfMeshView, лише верифікувати парсерами.
3. Ghidra потрібна головно для: рендер-пайплайна, фізики/балістики, netcode-протоколу, ботів (`.ai`), та кількох «магічних» бінарних форматів.
4. Динамічний аналіз (hook DLL під Wine/CrossOver) — часто дешевший за статичний.
