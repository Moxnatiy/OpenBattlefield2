# BF2 data formats

| Format | Status | Where |
|---|---|---|
| `.con` / `.tweak` | ✅ implemented, 0 errors over the whole corpus | [con.md](con.md), `src/con` |
| `ObjectTemplate` (registry) | ✅ 10 182 templates, 55 classes, part hierarchy | [object-template.md](object-template.md), `src/game` |
| Level (terrain, water, placement) | ✅ Dalian Plant whole: 51 patches, 906 objects | [level.md](level.md), `src/level` |
| Vehicle assembly | ✅ BundledMesh + the template tree, BTR-90 and AH-1Z whole | [vehicle-assembly.md](vehicle-assembly.md), `src/game/scene.cpp` |
| The game's zip archives | ✅ read in place, mounted the way `fileManager` does | `src/vfs` |
| `.staticmesh` / `.bundledmesh` / `.skinnedmesh` | ✅ 1635 of the game's 1635 meshes, rendered | [mesh.md](mesh.md), `src/mesh` |
| `.ske` / `.baf` (skeleton, animation) | ⬜ the specification is in Project Dalian + BfMeshView | — |
| `.collisionmesh` | ⬜ same | — |
| `.dds` | ✅ 2229 of the game's 2230 textures, rendered | [dds.md](dds.md), `src/texture` |
| `.dif` fonts + `.utxt` localisation | ✅ text formats, menu and loading screen | [font-and-localization.md](font-and-localization.md), `src/font`, `src/loc` |
| netcode: BitStream and headers | 🟡 the primitives are done, the connection is ahead | [../research/04-netcode.md](../research/04-netcode.md), `src/net` |
| netcode protocol | ⬜ needs RE (Ghidra / BitStream Emulator) | — |
| `.fx` shaders (the game's own) | 🟡 read, not yet followed: fog, mesh detail maps and roads differ | [shaders.md](shaders.md), `src/gfx` |

Graphics backend: SDL3 + SDL_GPU, the decision is in
[../research/01-render-backend.md](../research/01-render-backend.md).
