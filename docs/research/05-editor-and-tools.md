# Офіційний редактор і тули DICE

`Game Files/OtherFiles/bf2editor_and_tools` (145 МБ) виявився найкориснішим
джерелом документації з усього, що ми маємо.

## 1. Офіційні описи команд `.con`

`bf2editor/Help/CommandDescriptions.dat` — UTF-16LE, рядки через NUL,
парами «команда, опис». Це документація від самої DICE.

Витягнуто **546 команд** (з них 439 — `ObjectTemplate`) у
[../reference/con-command-descriptions.txt](../reference/con-command-descriptions.txt):

```
ObjectTemplate.GeometryPart      The ID number of the object part.
ObjectTemplate.HasMobilePhysics  Check for objects and parts that can move.
ObjectTemplate.PhysicsType       "None" if no collision.
```

Приємне підтвердження: `GeometryPart` — «ID номер частини об'єкта» — це
рівно той висновок, до якого ми дійшли реверсом BLENDINDICES. Тепер він
підтверджений незалежно.

```bash
python3 tools/extract_command_descriptions.py \
  "Game Files/OtherFiles/bf2editor_and_tools/bf2editor/Help/CommandDescriptions.dat" \
  docs/reference/con-command-descriptions.txt
```

Поруч лежать `ObjectEditor_Help.xls`, `UserGuide.doc` і теки
`Help/Workshop`, `Help/Tutorial` — ще не розібрані.

## 2. Вихідний код генератора навмешу

`NavMesh/Navmesh_SDK/` — **121 файл вихідного коду під LGPL**, який DICE
випустила разом із редактором. Це генератор AI-навмешу на базі GTS
(GNU Triangulated Surface).

`export.h` показує, що саме він пише:

```cpp
void export_qti (GtsSurface* s, const char* filename, ...);
void export_all_clusters (GtsSurface* s, bool binary, ...);
void export_surface_binary (GtsSurface* surface, const char* filename);
```

Формати `.qti`, `.cls`, `.vbf` — рівно ті, що лежать у `GTSData/output/`
кожного рівня. Тобто **формат AI-навігації документований офіційним
вихідним кодом**, і реверсити його не доведеться.

**Ліцензійне зауваження.** Код під LGPL, наш проєкт під MIT. Копіювати їхні
файли до себе не можна — але й не потрібно: ми читаємо їх як специфікацію
формату й пишемо власну реалізацію. Те саме правило, що й для мешів із
Project Dalian.

## 3. Меню — це gameswf

`SwiffPlayer_r.dll` містить шляхи до вихідників:

```
D:\DiceCanada\BoosterPack2\Code\BF2\External\gameswf\SwiffPlayer\SwiffPlayer.cpp
```

Тобто програвач Flash у BF2 — це **[gameswf](https://tulrich.com/geekstuff/gameswf.html)**,
відкрита C++ бібліотека Thatcher Ulrich, **у суспільному надбанні**
(public domain). Вона від початку робилася саме для інтерфейсів ігор.

Це змінює розклад щодо меню. У `Menu_client.zip` всього **5 `.swf`**
(`mainMenu.swf` — 2 МБ, решта дрібні) плюс 1072 PNG і 777 TGA, які до них
прив'язані. Оскільки гра крутила ці файли саме через gameswf, сумісність
гарантована конструктивно — на відміну від будь-якого стороннього плеєра.

Ціна: gameswf давно не розвивається, а її рендер написаний під OpenGL —
його довелося б перекласти на `obf2::gfx`. Альтернатива —
[Ruffle](https://ruffle.rs) (Rust, MIT/Apache-2.0, активно розвивається,
добре тримає ActionScript 2), але це FFI між Rust і C++ і жодних гарантій
щодо саме цих файлів.

## 4. Debug-збірки

У теці лежать `_r` і `_d` варіанти DLL (`dice_py_d.dll`, `msvcr71d.dll` —
відладковий CRT). Шляхи до PDB показують гілку `D:\bf2editor\`, тобто це
збірки редактора, а не гри. Символів у них немає, але рядків більше, ніж
у релізі.

## 5. Експортери для Maya

`maya/` (49 МБ) — офіційні плагіни експорту (`MayaParser.mll`) і MEL-скрипти
(`ModelTool.mel`, `LodTool.mel`, `ShaderTool.mel`, `ProgressiveMeshUI.mel`).
Це той конвеєр, яким робилися меші гри. Не розібрано; найімовірніше містить
опис того, як `.staticmesh` збирається з боку художника — корисно для
перевірки нашого парсера, але не критично, бо формат уже читається повністю.
