# Формати даних BF2

| Формат | Статус | Де |
|---|---|---|
| `.con` / `.tweak` | ✅ реалізовано, 0 помилок на всьому корпусі | [con.md](con.md), `src/con` |
| zip-архіви гри | ✅ читаються на місці, монтування як у `fileManager` | `src/vfs` |
| `.staticmesh` / `.bundledmesh` / `.skinnedmesh` | ⬜ специфікація є у Project Dalian + BfMeshView | — |
| `.ske` / `.baf` (скелет, анімація) | ⬜ те саме | — |
| `.collisionmesh` | ⬜ те саме | — |
| `.dds` | ⬜ стандартний формат | — |
| netcode-протокол | ⬜ потребує RE (Ghidra / BitStream Emulator) | — |
