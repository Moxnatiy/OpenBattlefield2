# Формати даних BF2

| Формат | Статус | Де |
|---|---|---|
| `.con` / `.tweak` | ✅ реалізовано, 0 помилок на всьому корпусі | [con.md](con.md), `src/con` |
| `ObjectTemplate` (реєстр) | ✅ 10 182 шаблони, 55 класів, ієрархія частин | [object-template.md](object-template.md), `src/game` |
| Рівень (терен, вода, розстановка) | ✅ Dalian Plant цілком: 51 патч, 906 обʼєктів | [level.md](level.md), `src/level` |
| Складання техніки | ✅ BundledMesh + дерево шаблонів, БТР-90 і AH-1Z цілком | [vehicle-assembly.md](vehicle-assembly.md), `src/game/scene.cpp` |
| zip-архіви гри | ✅ читаються на місці, монтування як у `fileManager` | `src/vfs` |
| `.staticmesh` / `.bundledmesh` / `.skinnedmesh` | ✅ 1635 з 1635 мешів гри, рендериться | [mesh.md](mesh.md), `src/mesh` |
| `.ske` / `.baf` (скелет, анімація) | ⬜ специфікація є у Project Dalian + BfMeshView | — |
| `.collisionmesh` | ⬜ те саме | — |
| `.dds` | ✅ 2229 з 2230 текстур гри, рендериться | [dds.md](dds.md), `src/texture` |
| netcode-протокол | ⬜ потребує RE (Ghidra / BitStream Emulator) | — |

Графічний бекенд: SDL3 + SDL_GPU, рішення — [../research/01-render-backend.md](../research/01-render-backend.md).
