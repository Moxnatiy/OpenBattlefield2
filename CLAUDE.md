# OpenBattlefield2

Мета: clean-room реімплементація рушія Refractor 2 (Battlefield 2, 2005) —
відкритий движок, який читає оригінальні ассети користувача.

## Правила (важливо для економії токенів)

1. **Ніколи не читати цілі декомпіляції у контекст без потреби.** Працюємо через
   MCP `ghidra` (pyghidra-mcp): спочатку `search`/`list`, потім декомпіляція
   ОДНІЄЇ функції. Результат одразу конспектуємо в `docs/functions/<модуль>.md`
   і більше не перечитуємо бінар.
2. **Знання живуть у файлах, не в контексті.** Кожен розібраний формат/функція →
   короткий md-конспект (сигнатура, структури, псевдокод 10-20 рядків, offset).
3. **Batch-скрипти замість ручних кроків.** Масові операції (експорт списку
   функцій, пошук рядків, xref) — через `tools/ghidra_scripts/*.py` у headless,
   вивід у файл, у контекст лише `head`/`grep`.
4. **Не винаходити те, що вже зробили.** Перед роботою над форматом — звірити з
   `docs/research/00-prior-art.md`. Використовуємо те, що вже відкрите: власні
   дані гри (`*_server.zip`, `*_client.zip`, `python/`, `.con`/`.tweak`),
   специфікації Project Dalian (MIT) і BfMeshView. RE — лише для того, чого
   нема у відкритому вигляді.
5. **Нічого не розпаковувати на диск.** Архіви гри читаються на місці через
   `obf2::FileSystem` — так само, як це робить `fileManager` у Refractor 2.
   Теку `extract/` тримаємо порожньою.
6. **Clean-room.** Код у `src/` пишемо за конспектами поведінки, не копіюємо
   декомпільований вивід дослівно.

## Структура

- `Game Files/` — оригінальна інсталяція BF2 (не в git)
- `extract/` — розпаковані ассети (не в git)
- `ghidra_projects/` — Ghidra проєкт `OpenBF2` (не в git)
- `docs/research/` — ресерч, прототипи прийнятих рішень
- `docs/formats/` — специфікації файлових форматів
- `docs/functions/` — конспекти розібраних функцій BF2.exe / DLL
- `tools/` — скрипти (headless Ghidra, розпаковка, парсери)
- `src/core` — платформа, шляхи; `src/con` — мова .con; `src/vfs` — архіви
  гри; `src/gfx` — вікно й GPU; `src/app` — виконуваний `openbf2`

## Збірка

```bash
cmake --preset macos-arm64-debug && cmake --build --preset macos-arm64-debug
ctest --test-dir build/macos-arm64-debug --output-on-failure
```

Основна платформа — **arm64 macOS**. Windows-збірка відкладена (пресет
`windows-x64` лишається робочим, але в CI не ганяється), проте база має
лишатися портованою: код не має набувати macOS-залежностей.
Правила, щоб не зламати мультиплатформність:

- C++20 без компіляторних розширень (`CMAKE_CXX_EXTENSIONS OFF`) — MSVC їх не має;
- нічого платформозалежного поза `obf2/core/platform.h`;
- шляхи до ассетів — тільки через `normalizeAssetPath` (регістр + слеші);
- залежності — або вендорні single-file (`third_party/`), або через
  `find_package` з фолбеком на FetchContent (`cmake/sdl3.cmake`);
- графіка — тільки через `obf2::gfx`, ніяких прямих викликів Metal/GL.

## Тулчейн

- SDL3 3.4.14 + SDL_GPU (Metal тут, Vulkan/D3D12 на Windows) — див.
  `docs/research/01-render-backend.md`
- Ghidra 12.1.3 (`brew`, `/opt/homebrew/opt/ghidra/libexec`), JDK 21
- MCP: `pyghidra-mcp` (headless, stdio) — конфіг у `.mcp.json`
- `.venv/` — Python 3.12 (uv)
- radare2, wine, CrossOver — на місці
