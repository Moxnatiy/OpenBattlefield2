# DICE's official editor and tools

`Game Files/OtherFiles/bf2editor_and_tools` (145 MB) turned out to be the
most useful source of documentation we have.

## 1. Official `.con` command descriptions

`bf2editor/Help/CommandDescriptions.dat` — UTF-16LE, NUL-separated
strings, in "command, description" pairs. This is documentation from DICE
themselves.

**546 commands** were extracted (439 of them `ObjectTemplate`) into
[../reference/con-command-descriptions.txt](../reference/con-command-descriptions.txt):

```
ObjectTemplate.GeometryPart      The ID number of the object part.
ObjectTemplate.HasMobilePhysics  Check for objects and parts that can move.
ObjectTemplate.PhysicsType       "None" if no collision.
```

A pleasant confirmation: `GeometryPart` — "the ID number of the object
part" — is exactly the conclusion we reached by reversing BLENDINDICES. It
is now confirmed independently.

```bash
python3 tools/extract_command_descriptions.py \
  "Game Files/OtherFiles/bf2editor_and_tools/bf2editor/Help/CommandDescriptions.dat" \
  docs/reference/con-command-descriptions.txt
```

Next to it sit `ObjectEditor_Help.xls`, `UserGuide.doc` and the
`Help/Workshop`, `Help/Tutorial` directories — not looked at yet.

## 2. The navmesh generator's source code

`NavMesh/Navmesh_SDK/` — **121 source files under LGPL**, released by DICE
together with the editor. It is an AI navmesh generator built on GTS (GNU
Triangulated Surface).

`export.h` shows exactly what it writes:

```cpp
void export_qti (GtsSurface* s, const char* filename, ...);
void export_all_clusters (GtsSurface* s, bool binary, ...);
void export_surface_binary (GtsSurface* surface, const char* filename);
```

The `.qti`, `.cls` and `.vbf` formats are exactly the ones in every level's
`GTSData/output/`. So **the AI navigation format is documented by official
source code** and will not have to be reversed.

**A licensing note.** That code is LGPL, our project is MIT. Copying their
files into our tree is not allowed — and not needed: we read them as a
specification of the format and write our own implementation. The same
rule as for meshes from Project Dalian.

## 3. The menu is gameswf

`SwiffPlayer_r.dll` contains source paths:

```
D:\DiceCanada\BoosterPack2\Code\BF2\External\gameswf\SwiffPlayer\SwiffPlayer.cpp
```

So BF2's Flash player is
**[gameswf](https://tulrich.com/geekstuff/gameswf.html)**, Thatcher
Ulrich's open C++ library, **in the public domain**. It was built for game
interfaces in the first place.

That changes the outlook for the menu. `Menu_client.zip` holds only **5
`.swf` files** (`mainMenu.swf` is 2 MB, the rest are small) plus 1072 PNGs
and 777 TGAs bound to them. Since the game played those files through
gameswf, compatibility is guaranteed by construction — unlike with any
third-party player.

The price: gameswf has not been developed for a long time and its renderer
is written for OpenGL, which would have to be ported to `obf2::gfx`. The
alternative is [Ruffle](https://ruffle.rs) (Rust, MIT/Apache-2.0, actively
developed, good at ActionScript 2), but that means FFI between Rust and
C++ and no guarantees about these particular files.

## 4. Debug builds

The directory holds `_r` and `_d` variants of the DLLs (`dice_py_d.dll`,
`msvcr71d.dll` — the debug CRT). The PDB paths point at the
`D:\bf2editor\` branch, so these are builds of the editor, not of the
game. They carry no symbols, but more strings than the release.

## 5. Maya exporters

`maya/` (49 MB) — the official export plugins (`MayaParser.mll`) and MEL
scripts (`ModelTool.mel`, `LodTool.mel`, `ShaderTool.mel`,
`ProgressiveMeshUI.mel`). This is the pipeline the game's meshes were made
with. Not examined; most likely it describes how a `.staticmesh` is
assembled from the artist's side — useful for checking our parser, but not
critical, since the format already reads in full.
