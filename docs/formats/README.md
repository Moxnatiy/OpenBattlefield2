# Формати даних BF2

| Формат | Статус | Де |
|---|---|---|
| `.con` / `.tweak` | ✅ реалізовано, 0 помилок на всьому корпусі | [con.md](con.md), `src/con` |
| zip-архіви гри | ✅ читаються на місці, монтування як у `fileManager` | `src/vfs` |
| `.staticmesh` / `.bundledmesh` / `.skinnedmesh` | ✅ 1635 з 1635 мешів гри, рендериться | [mesh.md](mesh.md), `src/mesh` |
| `.ske` / `.baf` (скелет, анімація) | ⬜ специфікація є у Project Dalian + BfMeshView | — |
| `.collisionmesh` | ⬜ те саме | — |
| `.dds` | ⬜ стандартний формат | — |
| netcode-протокол | ⬜ потребує RE (Ghidra / BitStream Emulator) | — |

Графічний бекенд: SDL3 + SDL_GPU, рішення — [../research/01-render-backend.md](../research/01-render-backend.md).
